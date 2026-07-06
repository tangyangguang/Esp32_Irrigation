#include "HistoryService.h"

#include <ArduinoJson.h>
#include <Esp32Base.h>
#include <stdio.h>

#include "IrrigationConfig.h"

namespace Irrigation {

namespace {
constexpr const char* kHistoryDir = "/irrigation/history";
constexpr const char* kHistoryPath = "/irrigation/history/runs.log";
constexpr size_t kHistoryJsonDocSize = 2048;
constexpr size_t kHistoryLineSize = 1536;

char g_lastError[40] = "ok";
char g_line[kHistoryLineSize];

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

} // namespace

bool HistoryService::appendRun(const WateringRun& run) {
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

    const size_t maxRead = len - 1;
    const uint32_t offset = size > static_cast<int64_t>(maxRead) ? static_cast<uint32_t>(size - maxRead) : 0;
    size_t readLen = 0;
    if (!Esp32BaseFs::readBytesAt(kHistoryPath, offset, reinterpret_cast<uint8_t*>(out), maxRead, &readLen)) {
        setLastError("history_read_failed");
        return false;
    }
    out[readLen] = '\0';
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
