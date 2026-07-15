#include "IrrigationConfig.h"

#include <cstdio>

namespace {

constexpr std::size_t kMaxNameCharacters = 20;
constexpr std::size_t kMaxNameBytes = 63;

bool inRange(uint32_t value, uint32_t minimum, uint32_t maximum) {
    return value >= minimum && value <= maximum;
}

template <std::size_t N>
void setText(std::array<char, N>& target, const char* value) {
    std::snprintf(target.data(), target.size(), "%s", value);
}

template <std::size_t N>
bool isTerminated(const std::array<char, N>& value) {
    for (const char ch : value) {
        if (ch == '\0') {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
bool isValidName(const std::array<char, N>& value, bool allowEmpty) {
    if (!isTerminated(value)) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(value.data());
    std::size_t byteCount = 0;
    std::size_t characterCount = 0;
    while (byteCount < N && bytes[byteCount] != 0) {
        const uint8_t first = bytes[byteCount];
        std::size_t sequenceLength = 0;
        uint32_t codePoint = 0;
        if (first <= 0x7F) {
            sequenceLength = 1;
            codePoint = first;
        } else if (first >= 0xC2 && first <= 0xDF) {
            sequenceLength = 2;
            codePoint = first & 0x1F;
        } else if (first >= 0xE0 && first <= 0xEF) {
            sequenceLength = 3;
            codePoint = first & 0x0F;
        } else if (first >= 0xF0 && first <= 0xF4) {
            sequenceLength = 4;
            codePoint = first & 0x07;
        } else {
            return false;
        }

        if (byteCount + sequenceLength > N - 1) {
            return false;
        }
        for (std::size_t offset = 1; offset < sequenceLength; ++offset) {
            const uint8_t next = bytes[byteCount + offset];
            if ((next & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (next & 0x3F);
        }
        if ((sequenceLength == 3 && codePoint < 0x800) ||
            (sequenceLength == 4 && codePoint < 0x10000) ||
            (codePoint >= 0xD800 && codePoint <= 0xDFFF) ||
            codePoint > 0x10FFFF ||
            codePoint < 0x20 ||
            codePoint == 0x7F) {
            return false;
        }

        byteCount += sequenceLength;
        ++characterCount;
    }

    if ((!allowEmpty && characterCount == 0) ||
        characterCount > kMaxNameCharacters ||
        byteCount > kMaxNameBytes) {
        return false;
    }
    return value[0] != ' ' && (byteCount == 0 || value[byteCount - 1] != ' ');
}

bool isValidFlowAction(FlowAlertAction action) {
    return action == FlowAlertAction::AlertOnly || action == FlowAlertAction::StopWatering;
}

}  // namespace

IrrigationConfig IrrigationConfigRules::createDefault() {
    IrrigationConfig config{};
    config.schemaVersion = kIrrigationConfigSchemaVersion;
    config.revision = 1;

    config.valveDrive = {3000, 20000, 75};
    config.pump = {false, 0, 1000};
    config.flowMeter = {25000, 0, 0, 0};
    config.calibrationStability = {3, 3, 10};
    config.flowProtection = {
        20,
        10,
        30,
        30,
        3,
        20,
        30,
        200,
        FlowAlertAction::AlertOnly,
        FlowAlertAction::AlertOnly,
    };
    config.timeSafety = {5, 12};

    for (std::size_t index = 0; index < config.zones.size(); ++index) {
        ZoneConfig& zone = config.zones[index];
        zone.id = static_cast<uint8_t>(index + 1);
        zone.enabled = index < 2;
        char name[kObjectNameCapacity];
        std::snprintf(name, sizeof(name), "区域 %u", static_cast<unsigned>(zone.id));
        setText(zone.name, name);
        zone.learnedFlowMlPerMinute = 0;
    }

    for (std::size_t index = 0; index < config.plans.size(); ++index) {
        WateringPlan& plan = config.plans[index];
        plan.id = static_cast<uint8_t>(index + 1);
        plan.configured = false;
        plan.scheduleEnabled = false;
        plan.startMinutes.fill(kUnusedStartMinute);
        plan.zoneDurationMinutes.fill(0);
    }

    return config;
}

bool IrrigationConfigRules::validate(const IrrigationConfig& config) {
    if (config.schemaVersion != kIrrigationConfigSchemaVersion || config.revision == 0) {
        return false;
    }
    if (!inRange(config.valveDrive.pullInTimeMs, 100, 10000) ||
        !inRange(config.valveDrive.pwmFrequencyHz, 1000, 25000) ||
        !inRange(config.valveDrive.holdDutyPercent, 1, 100)) {
        return false;
    }
    if (!inRange(config.pump.startDelayMs, 0, 60000) ||
        !inRange(config.pump.stopToValveCloseDelayMs, 0, 10000) ||
        !inRange(config.flowMeter.pulsesPerLiterX100, 1, 10000000) ||
        config.flowMeter.calibrationStartupPulseCount > 10000000U ||
        config.flowMeter.calibrationStartupWaterMl > 1000000U ||
        config.flowMeter.calibrationSteadyFlowMlPerMinute > 100000U ||
        !inRange(config.calibrationStability.windowSec, 1, 10) ||
        !inRange(config.calibrationStability.requiredWindows, 2, 10) ||
        !inRange(config.calibrationStability.allowedVariationPercent, 1, 30) ||
        !inRange(config.flowProtection.flowStartTimeoutSec, 1, 120) ||
        !inRange(config.flowProtection.noFlowTimeoutSec, 1, 60) ||
        !inRange(config.flowProtection.unexpectedFlowDelaySec, 0, 300) ||
        !inRange(config.flowProtection.unexpectedFlowWindowSec, 1, 300) ||
        config.flowProtection.unexpectedFlowPulseCount == 0 ||
        !inRange(config.flowProtection.flowDeviationConfirmSec, 1, 300) ||
        !inRange(config.flowProtection.lowFlowPercent, 1, 99) ||
        !inRange(config.flowProtection.highFlowPercent, 101, 1000) ||
        !isValidFlowAction(config.flowProtection.lowFlowAction) ||
        !isValidFlowAction(config.flowProtection.highFlowAction)) {
        return false;
    }
    if (config.timeSafety.rtcRollbackThresholdMinutes < 1 ||
        config.timeSafety.rtcRollbackThresholdMinutes > 60 ||
        config.timeSafety.aliveCheckpointHours > 168) {
        return false;
    }

    for (std::size_t index = 0; index < config.zones.size(); ++index) {
        const ZoneConfig& zone = config.zones[index];
        if (zone.id != index + 1 || !isValidName(zone.name, false) ||
            zone.learnedFlowMlPerMinute > 100000U) {
            return false;
        }
    }
    std::array<uint16_t, kWateringPlanCount * kPlanStartTimeCount> scheduledMinutes{};
    std::size_t scheduledMinuteCount = 0;
    for (std::size_t index = 0; index < config.plans.size(); ++index) {
        const WateringPlan& plan = config.plans[index];
        if (plan.id != index + 1 ||
            !isValidName(plan.name, !plan.configured) ||
            (!plan.configured && plan.scheduleEnabled)) {
            return false;
        }
        bool hasStartTime = false;
        for (const uint16_t minute : plan.startMinutes) {
            if (minute != kUnusedStartMinute && minute >= 24U * 60U) {
                return false;
            }
            if (minute != kUnusedStartMinute) {
                hasStartTime = true;
                if (plan.scheduleEnabled) {
                    for (std::size_t used = 0; used < scheduledMinuteCount; ++used) {
                        if (scheduledMinutes[used] == minute) {
                            return false;
                        }
                    }
                    scheduledMinutes[scheduledMinuteCount++] = minute;
                }
            }
        }
        bool hasAnyDuration = false;
        bool hasEnabledZoneDuration = false;
        for (std::size_t zoneIndex = 0; zoneIndex < plan.zoneDurationMinutes.size(); ++zoneIndex) {
            const uint16_t duration = plan.zoneDurationMinutes[zoneIndex];
            if (duration > 120) {
                return false;
            }
            if (duration > 0) {
                hasAnyDuration = true;
                hasEnabledZoneDuration = hasEnabledZoneDuration || config.zones[zoneIndex].enabled;
            }
        }
        if (plan.configured && !hasAnyDuration) {
            return false;
        }
        if (plan.scheduleEnabled && (!hasStartTime || !hasEnabledZoneDuration)) {
            return false;
        }
    }
    return true;
}

bool IrrigationConfigRules::parsePulsesPerLiter(const char* text, uint32_t& valueX100) {
    if (!text || *text == '\0') {
        return false;
    }

    uint64_t whole = 0;
    std::size_t index = 0;
    std::size_t wholeDigits = 0;
    while (text[index] >= '0' && text[index] <= '9') {
        whole = whole * 10U + static_cast<uint8_t>(text[index] - '0');
        if (whole > 100000U) {
            return false;
        }
        ++index;
        ++wholeDigits;
    }
    if (wholeDigits == 0) {
        return false;
    }

    uint32_t fraction = 0;
    std::size_t fractionDigits = 0;
    if (text[index] == '.') {
        ++index;
        while (text[index] >= '0' && text[index] <= '9') {
            if (fractionDigits >= 2) {
                return false;
            }
            fraction = fraction * 10U + static_cast<uint8_t>(text[index] - '0');
            ++index;
            ++fractionDigits;
        }
        if (fractionDigits == 0) {
            return false;
        }
    }
    if (text[index] != '\0') {
        return false;
    }
    if (fractionDigits == 1) {
        fraction *= 10U;
    }

    const uint64_t scaled = whole * 100U + fraction;
    if (scaled < 1U || scaled > 10000000U) {
        return false;
    }
    valueX100 = static_cast<uint32_t>(scaled);
    return true;
}

bool IrrigationConfigRules::formatPulsesPerLiter(uint32_t valueX100, char* out, std::size_t outSize) {
    if (!out || outSize == 0 || valueX100 < 1U || valueX100 > 10000000U) {
        return false;
    }
    const int written = std::snprintf(out,
                                      outSize,
                                      "%lu.%02lu",
                                      static_cast<unsigned long>(valueX100 / 100U),
                                      static_cast<unsigned long>(valueX100 % 100U));
    return written > 0 && static_cast<std::size_t>(written) < outSize;
}

bool IrrigationConfigRules::parseLitersPerMinute(const char* text,
                                                 uint32_t& valueMlPerMinute) {
    if (!text || *text == '\0') return false;
    uint32_t whole = 0;
    std::size_t index = 0;
    std::size_t wholeDigits = 0;
    while (text[index] >= '0' && text[index] <= '9') {
        whole = whole * 10U + static_cast<uint8_t>(text[index] - '0');
        if (whole > 100U) return false;
        ++index;
        ++wholeDigits;
    }
    if (wholeDigits == 0) return false;

    uint32_t fraction = 0;
    uint32_t scale = 100;
    if (text[index] == '.') {
        ++index;
        std::size_t digits = 0;
        while (text[index] >= '0' && text[index] <= '9' && digits < 3) {
            fraction += static_cast<uint32_t>(text[index] - '0') * scale;
            scale /= 10U;
            ++index;
            ++digits;
        }
        if (digits == 0 || (text[index] >= '0' && text[index] <= '9')) return false;
    }
    if (text[index] != '\0') return false;
    const uint32_t result = whole * 1000U + fraction;
    if (result > 100000U) return false;
    valueMlPerMinute = result;
    return true;
}

bool IrrigationConfigRules::formatLitersPerMinute(uint32_t valueMlPerMinute,
                                                  char* out,
                                                  std::size_t outSize) {
    if (!out || outSize == 0 || valueMlPerMinute > 100000U) return false;
    const int written = std::snprintf(
        out,
        outSize,
        "%lu.%03lu",
        static_cast<unsigned long>(valueMlPerMinute / 1000U),
        static_cast<unsigned long>(valueMlPerMinute % 1000U));
    return written > 0 && static_cast<std::size_t>(written) < outSize;
}
