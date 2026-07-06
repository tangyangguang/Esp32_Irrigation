#include "domain/DisplayService.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <Wire.h>
#include <stdio.h>
#include <string.h>

#include "Pins.h"
#include "domain/LocalControl.h"
#include "domain/ZoneManager.h"
#include "storage/SystemConfigStore.h"

namespace {

static constexpr uint8_t kCandidateAddresses[] = {0x27, 0x3F};
static constexpr uint32_t kRefreshMs = 1000UL;
static constexpr uint32_t kRetryMs = 10000UL;
static constexpr uint8_t kBacklight = 0x08;
static constexpr uint8_t kEnable = 0x04;
static constexpr uint8_t kRegisterSelect = 0x01;

uint8_t g_address = 0;
bool g_ready = false;
uint32_t g_lastRefreshMs = 0;
uint32_t g_lastProbeMs = 0;
char g_lastLine1[17] = {};
char g_lastLine2[17] = {};

bool writeByte(uint8_t value) {
    if (g_address == 0) {
        return false;
    }
    Wire.beginTransmission(g_address);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool pulseEnable(uint8_t value) {
    if (!writeByte(value | kEnable)) return false;
    delayMicroseconds(1);
    if (!writeByte(value & ~kEnable)) return false;
    delayMicroseconds(50);
    return true;
}

bool write4(uint8_t value, bool rs) {
    const uint8_t out = (value & 0xF0) | kBacklight | (rs ? kRegisterSelect : 0);
    return pulseEnable(out);
}

bool send(uint8_t value, bool rs) {
    return write4(value & 0xF0, rs) && write4(static_cast<uint8_t>((value << 4) & 0xF0), rs);
}

bool command(uint8_t value) {
    return send(value, false);
}

bool writeChar(char value) {
    return send(static_cast<uint8_t>(value), true);
}

bool probeAddress(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

bool initLcd(uint8_t address) {
    g_address = address;
    delay(50);
    if (!write4(0x30, false)) return false;
    delay(5);
    if (!write4(0x30, false)) return false;
    delayMicroseconds(150);
    if (!write4(0x30, false)) return false;
    if (!write4(0x20, false)) return false;
    if (!command(0x28)) return false; // 4-bit, 2 lines.
    if (!command(0x08)) return false;
    if (!command(0x01)) return false;
    delay(2);
    if (!command(0x06)) return false;
    if (!command(0x0C)) return false;
    return true;
}

bool probeDisplay() {
    for (uint8_t i = 0; i < sizeof(kCandidateAddresses); ++i) {
        const uint8_t address = kCandidateAddresses[i];
        if (probeAddress(address) && initLcd(address)) {
            ESP32BASE_LOG_I("display", "lcd1602 ready addr=0x%02x", address);
            return true;
        }
    }
    g_address = 0;
    return false;
}

void fitLine(const char* source, char* out) {
    size_t len = 0;
    if (source) {
        while (len < 16 && source[len] != '\0') {
            out[len] = source[len];
            ++len;
        }
    }
    while (len < 16) {
        out[len++] = ' ';
    }
    out[16] = '\0';
}

void writeLine(uint8_t row, const char* text) {
    char line[17];
    fitLine(text, line);
    char* last = row == 0 ? g_lastLine1 : g_lastLine2;
    if (strcmp(last, line) == 0) {
        return;
    }
    if (!command(row == 0 ? 0x80 : 0xC0)) {
        g_ready = false;
        return;
    }
    for (uint8_t i = 0; i < 16; ++i) {
        if (!writeChar(line[i])) {
            g_ready = false;
            return;
        }
    }
    strlcpy(last, line, 17);
}

const char* compactState(Irrigation::ZoneState state) {
    switch (state) {
        case Irrigation::ZoneState::DISABLED: return "DIS";
        case Irrigation::ZoneState::IDLE: return "IDLE";
        case Irrigation::ZoneState::STARTING: return "STRT";
        case Irrigation::ZoneState::RUNNING: return "RUN";
        case Irrigation::ZoneState::ERROR: return "ERR";
        default: return "UNK";
    }
}

void renderLines(char* line1, size_t line1Len, char* line2, size_t line2Len) {
    const uint8_t zoneId = LocalControl::selectedZoneId();
    const char* pending = LocalControl::pendingActionName();
    if (zoneId == 0) {
        snprintf(line1, line1Len, "No enabled zone");
        snprintf(line2, line2Len, "%s", pending);
        return;
    }
    const Irrigation::ZoneConfig& config = ZoneManager::config(zoneId);
    const Irrigation::ZoneStatus status = ZoneManager::status(zoneId);
    if (pending && strcmp(pending, "none") != 0) {
        snprintf(line1, line1Len, "Z%u F%u %s", zoneId, config.flowId, pending);
    } else {
        snprintf(line1, line1Len, "Z%u F%u %s", zoneId, config.flowId, compactState(status.state));
    }
    if (status.busy) {
        snprintf(line2, line2Len, "%luml %luL/m",
                 static_cast<unsigned long>(status.estimatedMilliliters),
                 static_cast<unsigned long>(status.flowMlPerMin / 1000UL));
        return;
    }
    const char* reason = ZoneManager::blockedReason(zoneId);
    if (reason && strcmp(reason, "none") != 0) {
        snprintf(line2, line2Len, "%s", reason);
    } else {
        snprintf(line2, line2Len, "ready %lus def",
                 static_cast<unsigned long>(SystemConfigStore::current().manualDefaultDurationSec));
    }
}

}

namespace DisplayService {

void begin() {
    Wire.begin(IrrigationPins::I2cSda, IrrigationPins::I2cScl);
    g_ready = probeDisplay();
    g_lastProbeMs = millis();
    g_lastRefreshMs = 0;
    memset(g_lastLine1, 0, sizeof(g_lastLine1));
    memset(g_lastLine2, 0, sizeof(g_lastLine2));
}

void handle() {
    const uint32_t nowMs = millis();
    if (!g_ready) {
        if (nowMs - g_lastProbeMs < kRetryMs) {
            return;
        }
        g_lastProbeMs = nowMs;
        g_ready = probeDisplay();
        return;
    }
    if (nowMs - g_lastRefreshMs < kRefreshMs) {
        return;
    }
    g_lastRefreshMs = nowMs;
    char line1[48];
    char line2[48];
    renderLines(line1, sizeof(line1), line2, sizeof(line2));
    writeLine(0, line1);
    writeLine(1, line2);
}

bool available() {
    return g_ready;
}

}
