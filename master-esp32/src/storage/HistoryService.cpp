#include "HistoryService.h"

#include <ArduinoJson.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>

#include "IrrigationConfig.h"

namespace Irrigation {

namespace {
constexpr const char* kHistoryDir = "/irrigation/history";
constexpr const char* kHistoryPath = "/irrigation/history/runs.log";
constexpr size_t kHistoryJsonDocSize = 2048;
constexpr size_t kHistoryLineSize = 1536;
constexpr size_t kHistoryReadChunkSize = 256;

char g_lastError[40] = "ok";
char g_line[kHistoryLineSize];
uint8_t g_readChunk[kHistoryReadChunkSize];

void reverseLine(char* text, size_t len) {
    for (size_t i = 0; i < len / 2; ++i) {
        const char tmp = text[i];
        text[i] = text[len - 1 - i];
        text[len - 1 - i] = tmp;
    }
}

const char* runSourceName(RunSource source) {
    switch (source) {
        case RunSource::Manual: return "manual";
        case RunSource::Plan: return "plan";
        case RunSource::RunPlanNow: return "run_plan_now";
        case RunSource::Calibration: return "calibration";
    }
    return "unknown";
}

const char* runResultName(RunResult result) {
    switch (result) {
        case RunResult::None: return "none";
        case RunResult::Completed: return "completed";
        case RunResult::UserStopped: return "user_stopped";
        case RunResult::FaultStopped: return "fault_stopped";
        case RunResult::Skipped: return "skipped";
    }
    return "unknown";
}

bool ensureHistoryDir() {
    return (Esp32BaseFs::mkdir("/irrigation") || Esp32BaseFs::exists("/irrigation")) &&
           (Esp32BaseFs::mkdir(kHistoryDir) || Esp32BaseFs::exists(kHistoryDir));
}

bool isWateringHistoryLine(const char* line) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) {
        return false;
    }
    const char* source = doc["source"] | "";
    return strcmp(source, "manual") == 0 ||
           strcmp(source, "plan") == 0 ||
           strcmp(source, "run_plan_now") == 0;
}

} // namespace

bool HistoryService::appendRun(const WateringRun& run) {
    if (run.source == RunSource::Calibration) {
        setLastError("ok");
        return true;
    }
    if (!Esp32BaseFs::isReady()) {
        setLastError("fs_not_ready");
        return false;
    }
    if (!ensureHistoryDir()) {
        setLastError("history_dir_failed");
        return false;
    }

    DynamicJsonDocument doc(kHistoryJsonDocSize);
    doc["id"] = run.id;
    doc["source"] = runSourceName(run.source);
    doc["planId"] = run.planId;
    doc["result"] = runResultName(run.result);
    doc["reason"] = runReasonToString(run.reason);
    doc["startedAt"] = run.startedAtEpoch;
    doc["finishedAt"] = run.finishedAtEpoch;
    doc["stepCount"] = run.stepCount;

    JsonArray steps = doc.createNestedArray("steps");
    for (uint8_t i = 0; i < run.stepCount; ++i) {
        JsonObject step = steps.createNestedObject();
        step["zoneId"] = run.steps[i].zoneId;
        step["targetDurationSec"] = run.steps[i].targetDurationSec;
    }

    const size_t written = serializeJson(doc, g_line, sizeof(g_line) - 2);
    if (written == 0 || written >= sizeof(g_line) - 2) {
        setLastError("history_serialize_failed");
        return false;
    }
    g_line[written] = '\n';
    g_line[written + 1] = '\0';

    if (!Esp32BaseFs::appendFile(kHistoryPath, g_line)) {
        setLastError("history_append_failed");
        return false;
    }

    setLastError("ok");
    return true;
}

bool HistoryService::readRecent(char* out, size_t len) {
    if (out == nullptr || len == 0) {
        setLastError("history_buffer_invalid");
        return false;
    }
    out[0] = '\0';
    uint32_t total = 0;
    return readPage(1, 20, out, len, total);
}

bool HistoryService::readPage(uint32_t page, uint32_t perPage, char* out, size_t len, uint32_t& totalOut) {
    totalOut = 0;
    if (out == nullptr || len == 0) {
        setLastError("history_buffer_invalid");
        return false;
    }
    out[0] = '\0';

    if (page == 0) {
        page = 1;
    }
    if (perPage == 0) {
        perPage = 10;
    }
    if (perPage > 50) {
        perPage = 50;
    }

    if (!Esp32BaseFs::isReady()) {
        setLastError("fs_not_ready");
        return false;
    }
    if (!Esp32BaseFs::exists(kHistoryPath)) {
        setLastError("history_missing");
        return true;
    }

    const int64_t size = Esp32BaseFs::fileSize(kHistoryPath);
    if (size <= 0) {
        setLastError("history_empty");
        return true;
    }

    const uint32_t firstWanted = (page - 1U) * perPage;
    const uint32_t lastWanted = firstWanted + perPage;
    uint32_t includedIndexFromNewest = 0;
    size_t outLen = 0;
    size_t lineLen = 0;
    bool lineOverflow = false;
    uint32_t pos = static_cast<uint32_t>(size);

    auto finishLine = [&]() {
        if (lineLen == 0 && !lineOverflow) {
            return;
        }

        if (!lineOverflow) {
            reverseLine(g_line, lineLen);
            g_line[lineLen] = '\0';
            if (!isWateringHistoryLine(g_line)) {
                lineLen = 0;
                lineOverflow = false;
                return;
            }
            if (includedIndexFromNewest >= firstWanted && includedIndexFromNewest < lastWanted) {
                const size_t needed = lineLen + 1;
                if (outLen + needed < len) {
                    memcpy(out + outLen, g_line, lineLen);
                    outLen += lineLen;
                    out[outLen++] = '\n';
                    out[outLen] = '\0';
                }
            }
        }

        ++totalOut;
        ++includedIndexFromNewest;
        lineLen = 0;
        lineOverflow = false;
    };

    while (pos > 0) {
        const size_t chunkSize = pos > kHistoryReadChunkSize ? kHistoryReadChunkSize : pos;
        pos -= chunkSize;
        size_t readLen = 0;
        if (!Esp32BaseFs::readBytesAt(kHistoryPath, pos, g_readChunk, chunkSize, &readLen)) {
            setLastError("history_read_failed");
            return false;
        }
        for (size_t n = readLen; n > 0; --n) {
            const char c = static_cast<char>(g_readChunk[n - 1]);
            if (c == '\n') {
                finishLine();
                continue;
            }
            if (c == '\r') {
                continue;
            }
            if (lineLen < sizeof(g_line) - 1) {
                g_line[lineLen++] = c;
            } else {
                lineOverflow = true;
            }
        }
    }
    finishLine();

    setLastError("ok");
    return true;
}

const char* HistoryService::lastError() {
    return g_lastError;
}

const char* HistoryService::path() {
    return kHistoryPath;
}

void HistoryService::setLastError(const char* error) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", error != nullptr ? error : "unknown");
    g_lastError[sizeof(g_lastError) - 1] = '\0';
}

} // namespace Irrigation
