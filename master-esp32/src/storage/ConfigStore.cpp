#include "ConfigStore.h"

#include <ArduinoJson.h>
#include <Esp32Base.h>
#include <stdio.h>
#include <string.h>

#include "IrrigationConfig.h"

namespace Irrigation {

namespace {

constexpr const char* kConfigDir = "/irrigation";
constexpr const char* kConfigPath = "/irrigation/config.json";
constexpr const char* kConfigTempPath = "/irrigation/config.json.tmp";
constexpr const char* kConfigBackupPath = "/irrigation/config.json.bak";
constexpr size_t kConfigJsonBufferSize = 8192;
constexpr size_t kConfigJsonDocSize = 12288;

IrrigationConfig g_config;
bool g_loadedFromDefaults = true;
bool g_configValid = false;
char g_lastError[48] = "not_started";
char g_jsonBuffer[kConfigJsonBufferSize];

void setLastError(const char* error) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", error != nullptr ? error : "unknown");
}

void copyString(char* target, size_t targetSize, const char* value) {
    if (target == nullptr || targetSize == 0) {
        return;
    }
    snprintf(target, targetSize, "%s", value != nullptr ? value : "");
    target[targetSize - 1] = '\0';
}

bool normalizeLegacyEnglishNames(IrrigationConfig& config) {
    bool changed = false;
    char legacy[16];
    char localized[16];

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        ZoneConfig& zone = config.zones[i];
        snprintf(legacy, sizeof(legacy), "Zone %u", static_cast<unsigned>(zone.id));
        if (strcmp(zone.name, legacy) == 0) {
            snprintf(localized, sizeof(localized), "水路 %u", static_cast<unsigned>(zone.id));
            copyString(zone.name, sizeof(zone.name), localized);
            changed = true;
        }
    }

    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        WateringPlan& plan = config.plans[i];
        snprintf(legacy, sizeof(legacy), "Plan %u", static_cast<unsigned>(plan.id));
        if (strcmp(plan.name, legacy) == 0) {
            snprintf(localized, sizeof(localized), "计划 %u", static_cast<unsigned>(plan.id));
            copyString(plan.name, sizeof(plan.name), localized);
            changed = true;
        }
    }

    return changed;
}

bool migrateOldDefaultEnabledZones(IrrigationConfig& config) {
    bool oldDefaultPattern = true;
    char expectedName[16];

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        snprintf(expectedName, sizeof(expectedName), "水路 %u", static_cast<unsigned>(zone.id));
        if (zone.enabled != (i == 0) ||
            strcmp(zone.name, expectedName) != 0 ||
            zone.standardFlowMlPerMin != 0) {
            oldDefaultPattern = false;
            break;
        }
    }

    if (!oldDefaultPattern) {
        return false;
    }

    for (uint8_t i = 0; i < kMaxZones; ++i) {
        config.zones[i].enabled = i < kDefaultEnabledZones;
    }
    return true;
}

const char* contactTypeToString(ContactType type) {
    return type == ContactType::NormallyClosed ? "normally_closed" : "normally_open";
}

ContactType contactTypeFromString(const char* value) {
    if (value != nullptr && strcmp(value, "normally_closed") == 0) {
        return ContactType::NormallyClosed;
    }
    return ContactType::NormallyOpen;
}

uint32_t readU32(JsonVariantConst value, uint32_t fallback) {
    return value.is<uint32_t>() ? value.as<uint32_t>() : fallback;
}

uint16_t readU16(JsonVariantConst value, uint16_t fallback) {
    return value.is<uint16_t>() ? value.as<uint16_t>() : fallback;
}

uint8_t readU8(JsonVariantConst value, uint8_t fallback) {
    return value.is<uint8_t>() ? value.as<uint8_t>() : fallback;
}

bool readBool(JsonVariantConst value, bool fallback) {
    return value.is<bool>() ? value.as<bool>() : fallback;
}

void writeSupply(JsonObject data, const SupplyConfig& supply) {
    JsonObject obj = data.createNestedObject("supply");
    obj["pumpEnabled"] = supply.pumpEnabled;
    obj["pumpStartDelayMs"] = supply.pumpStartDelayMs;
    obj["pumpStopDelayMs"] = supply.pumpStopDelayMs;
    obj["lowLevelEnabled"] = supply.lowLevelEnabled;
    obj["lowLevelContactType"] = contactTypeToString(supply.lowLevelContactType);
    obj["lowLevelDebounceMs"] = supply.lowLevelDebounceMs;
}

void writeFlow(JsonObject data, const FlowConfig& flow) {
    JsonObject obj = data.createNestedObject("flow");
    obj["pulsesPerLiter"] = flow.pulsesPerLiter;
    obj["startupGraceSec"] = flow.startupGraceSec;
    obj["noFlowConfirmSec"] = flow.noFlowConfirmSec;
    obj["leakWindowSec"] = flow.leakWindowSec;
    obj["leakPulseThreshold"] = flow.leakPulseThreshold;
    obj["lowFlowPercent"] = flow.lowFlowPercent;
    obj["highFlowPercent"] = flow.highFlowPercent;
    obj["lowHighFlowConfirmSec"] = flow.lowHighFlowConfirmSec;
}

void writeValve(JsonObject data, const ValveConfig& valve) {
    JsonObject obj = data.createNestedObject("valve");
    obj["pullInMs"] = valve.pullInMs;
    obj["holdPercent"] = valve.holdPercent;
    obj["maxZoneDurationSec"] = valve.maxZoneDurationSec;
}

void writeZones(JsonObject data, const IrrigationConfig& config) {
    JsonArray zones = data.createNestedArray("zones");
    for (uint8_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig& zone = config.zones[i];
        JsonObject obj = zones.createNestedObject();
        obj["id"] = zone.id;
        obj["enabled"] = zone.enabled;
        obj["name"] = zone.name;
        obj["standardFlowMlPerMin"] = zone.standardFlowMlPerMin;
        obj["valveIndex"] = zone.valveIndex;
    }
}

void writePlans(JsonObject data, const IrrigationConfig& config) {
    JsonArray plans = data.createNestedArray("plans");
    for (uint8_t i = 0; i < kMaxPlans; ++i) {
        const WateringPlan& plan = config.plans[i];
        JsonObject obj = plans.createNestedObject();
        obj["id"] = plan.id;
        obj["used"] = plan.used;
        obj["enabled"] = plan.enabled;
        obj["name"] = plan.name;

        JsonArray starts = obj.createNestedArray("startTimes");
        for (uint8_t j = 0; j < kMaxPlanStartTimes; ++j) {
            JsonObject start = starts.createNestedObject();
            start["enabled"] = plan.startTimes[j].enabled;
            start["minuteOfDay"] = plan.startTimes[j].minuteOfDay;
        }

        JsonArray durations = obj.createNestedArray("zoneDurationSec");
        for (uint8_t j = 0; j < kMaxZones; ++j) {
            durations.add(plan.zoneDurationSec[j]);
        }
    }
}

bool serializeConfig(const IrrigationConfig& config, char* out, size_t outLen, size_t& writtenLen) {
    DynamicJsonDocument doc(kConfigJsonDocSize);
    doc["version"] = config.version;
    const Esp32BaseTime::Snapshot time = Esp32BaseTime::snapshot();
    doc["updatedAt"] = time.synced ? time.epochSec : 0;

    JsonObject data = doc.createNestedObject("data");
    writeSupply(data, config.supply);
    writeFlow(data, config.flow);
    writeValve(data, config.valve);
    writeZones(data, config);
    writePlans(data, config);

    writtenLen = serializeJsonPretty(doc, out, outLen);
    return writtenLen > 0 && writtenLen < outLen;
}

void readSupply(JsonObjectConst data, SupplyConfig& supply) {
    JsonObjectConst obj = data["supply"];
    if (obj.isNull()) {
        return;
    }
    supply.pumpEnabled = readBool(obj["pumpEnabled"], supply.pumpEnabled);
    supply.pumpStartDelayMs = readU32(obj["pumpStartDelayMs"], supply.pumpStartDelayMs);
    supply.pumpStopDelayMs = readU32(obj["pumpStopDelayMs"], supply.pumpStopDelayMs);
    supply.lowLevelEnabled = readBool(obj["lowLevelEnabled"], supply.lowLevelEnabled);
    supply.lowLevelContactType = contactTypeFromString(obj["lowLevelContactType"] | contactTypeToString(supply.lowLevelContactType));
    supply.lowLevelDebounceMs = readU32(obj["lowLevelDebounceMs"], supply.lowLevelDebounceMs);
}

void readFlow(JsonObjectConst data, FlowConfig& flow) {
    JsonObjectConst obj = data["flow"];
    if (obj.isNull()) {
        return;
    }
    flow.pulsesPerLiter = readU32(obj["pulsesPerLiter"], flow.pulsesPerLiter);
    flow.startupGraceSec = readU32(obj["startupGraceSec"], flow.startupGraceSec);
    flow.noFlowConfirmSec = readU32(obj["noFlowConfirmSec"], flow.noFlowConfirmSec);
    flow.leakWindowSec = readU32(obj["leakWindowSec"], flow.leakWindowSec);
    flow.leakPulseThreshold = readU32(obj["leakPulseThreshold"], flow.leakPulseThreshold);
    flow.lowFlowPercent = readU8(obj["lowFlowPercent"], flow.lowFlowPercent);
    flow.highFlowPercent = readU16(obj["highFlowPercent"], flow.highFlowPercent);
    flow.lowHighFlowConfirmSec = readU32(obj["lowHighFlowConfirmSec"], flow.lowHighFlowConfirmSec);
}

void readValve(JsonObjectConst data, ValveConfig& valve) {
    JsonObjectConst obj = data["valve"];
    if (obj.isNull()) {
        return;
    }
    valve.pullInMs = readU32(obj["pullInMs"], valve.pullInMs);
    valve.holdPercent = readU8(obj["holdPercent"], valve.holdPercent);
    valve.maxZoneDurationSec = readU32(obj["maxZoneDurationSec"], valve.maxZoneDurationSec);
}

void readZones(JsonObjectConst data, IrrigationConfig& config) {
    JsonArrayConst zones = data["zones"];
    if (zones.isNull()) {
        return;
    }

    uint8_t index = 0;
    for (JsonObjectConst obj : zones) {
        if (index >= kMaxZones) {
            break;
        }
        ZoneConfig& zone = config.zones[index];
        zone.id = readU8(obj["id"], zone.id);
        zone.enabled = readBool(obj["enabled"], zone.enabled);
        copyString(zone.name, sizeof(zone.name), obj["name"] | zone.name);
        zone.standardFlowMlPerMin = readU32(obj["standardFlowMlPerMin"], zone.standardFlowMlPerMin);
        zone.valveIndex = readU8(obj["valveIndex"], zone.valveIndex);
        ++index;
    }
}

void readPlans(JsonObjectConst data, IrrigationConfig& config) {
    JsonArrayConst plans = data["plans"];
    if (plans.isNull()) {
        return;
    }

    uint8_t planIndex = 0;
    for (JsonObjectConst obj : plans) {
        if (planIndex >= kMaxPlans) {
            break;
        }
        WateringPlan& plan = config.plans[planIndex];
        plan.id = readU8(obj["id"], plan.id);
        plan.used = readBool(obj["used"], plan.used);
        plan.enabled = readBool(obj["enabled"], plan.enabled);
        copyString(plan.name, sizeof(plan.name), obj["name"] | plan.name);

        JsonArrayConst starts = obj["startTimes"];
        uint8_t startIndex = 0;
        for (JsonObjectConst start : starts) {
            if (startIndex >= kMaxPlanStartTimes) {
                break;
            }
            plan.startTimes[startIndex].enabled = readBool(start["enabled"], plan.startTimes[startIndex].enabled);
            plan.startTimes[startIndex].minuteOfDay = readU16(start["minuteOfDay"], plan.startTimes[startIndex].minuteOfDay);
            ++startIndex;
        }

        JsonArrayConst durations = obj["zoneDurationSec"];
        uint8_t zoneIndex = 0;
        for (JsonVariantConst duration : durations) {
            if (zoneIndex >= kMaxZones) {
                break;
            }
            plan.zoneDurationSec[zoneIndex] = readU32(duration, plan.zoneDurationSec[zoneIndex]);
            ++zoneIndex;
        }
        ++planIndex;
    }
}

bool parseConfigJson(const char* json, IrrigationConfig& out) {
    DynamicJsonDocument doc(kConfigJsonDocSize);
    DeserializationError jsonError = deserializeJson(doc, json);
    if (jsonError) {
        setLastError("config_json_parse_failed");
        return false;
    }

    applyDefaultConfig(out);
    out.version = doc["version"] | out.version;

    JsonObjectConst data = doc["data"];
    if (data.isNull()) {
        setLastError("config_data_missing");
        return false;
    }

    readSupply(data, out.supply);
    readFlow(data, out.flow);
    readValve(data, out.valve);
    readZones(data, out);
    readPlans(data, out);

    const char* validationError = nullptr;
    if (!validateConfig(out, &validationError)) {
        setLastError(validationError);
        return false;
    }

    return true;
}

bool readConfigFile(const char* path, IrrigationConfig& out) {
    const int64_t size = Esp32BaseFs::fileSize(path);
    if (size <= 0 || size >= static_cast<int64_t>(kConfigJsonBufferSize)) {
        setLastError(size <= 0 ? "config_file_empty" : "config_file_too_large");
        return false;
    }

    if (!Esp32BaseFs::readFile(path, g_jsonBuffer, sizeof(g_jsonBuffer))) {
        setLastError("config_read_failed");
        return false;
    }

    return parseConfigJson(g_jsonBuffer, out);
}

bool ensureConfigDir() {
    return Esp32BaseFs::mkdir(kConfigDir) || Esp32BaseFs::exists(kConfigDir);
}

bool verifySerializedConfig(const char* path) {
    IrrigationConfig verify;
    return readConfigFile(path, verify);
}

bool replaceConfigFile() {
    if (Esp32BaseFs::exists(kConfigBackupPath)) {
        Esp32BaseFs::removeFile(kConfigBackupPath);
    }

    const bool hadOldConfig = Esp32BaseFs::exists(kConfigPath);
    if (hadOldConfig && !Esp32BaseFs::rename(kConfigPath, kConfigBackupPath)) {
        setLastError("config_backup_failed");
        return false;
    }

    if (Esp32BaseFs::rename(kConfigTempPath, kConfigPath)) {
        if (hadOldConfig) {
            Esp32BaseFs::removeFile(kConfigBackupPath);
        }
        return true;
    }

    if (hadOldConfig) {
        Esp32BaseFs::rename(kConfigBackupPath, kConfigPath);
    }
    setLastError("config_replace_failed");
    return false;
}

} // namespace

bool ConfigStore::begin() {
    applyDefaultConfig(g_config);
    g_loadedFromDefaults = true;
    g_configValid = validateConfig(g_config, nullptr);

    if (!Esp32BaseFs::isReady()) {
        setLastError("fs_not_ready");
        return false;
    }

    if (!load()) {
        ESP32BASE_LOG_W("config_store", "using_default_config error=%s", g_lastError);
        if (!save(g_config)) {
            ESP32BASE_LOG_W("config_store", "default_config_save_failed error=%s", g_lastError);
        }
        g_loadedFromDefaults = true;
    } else {
        const bool versionChanged = g_config.version != kConfigVersion;
        if (versionChanged) {
            g_config.version = kConfigVersion;
        }
        const bool namesChanged = normalizeLegacyEnglishNames(g_config);
        const bool zonesChanged = migrateOldDefaultEnabledZones(g_config);
        if (!versionChanged && !namesChanged && !zonesChanged) {
            return g_configValid;
        }
        ESP32BASE_LOG_I("config_store", "config_normalized");
        if (!save(g_config)) {
            ESP32BASE_LOG_W("config_store", "normalized_config_save_failed error=%s", g_lastError);
        }
    }

    return g_configValid;
}

bool ConfigStore::load() {
    IrrigationConfig loaded;
    if (!loadFromFile(loaded)) {
        applyDefaultConfig(g_config);
        g_loadedFromDefaults = true;
        g_configValid = validateConfig(g_config, nullptr);
        return false;
    }

    g_config = loaded;
    g_loadedFromDefaults = false;
    g_configValid = true;
    setLastError("ok");
    return true;
}

bool ConfigStore::loadFromFile(IrrigationConfig& out) {
    if (!Esp32BaseFs::exists(kConfigPath)) {
        setLastError("config_missing");
        return false;
    }
    return readConfigFile(kConfigPath, out);
}

bool ConfigStore::save(const IrrigationConfig& config) {
    const char* validationError = nullptr;
    if (!validateConfig(config, &validationError)) {
        setLastError(validationError);
        return false;
    }

    if (!Esp32BaseFs::isReady()) {
        setLastError("fs_not_ready");
        return false;
    }

    if (!ensureConfigDir()) {
        setLastError("config_dir_failed");
        return false;
    }

    size_t writtenLen = 0;
    if (!serializeConfig(config, g_jsonBuffer, sizeof(g_jsonBuffer), writtenLen)) {
        setLastError("config_serialize_failed");
        return false;
    }

    if (!Esp32BaseFs::writeFile(kConfigTempPath, g_jsonBuffer)) {
        setLastError("config_temp_write_failed");
        return false;
    }

    if (!verifySerializedConfig(kConfigTempPath)) {
        Esp32BaseFs::removeFile(kConfigTempPath);
        setLastError("config_temp_verify_failed");
        return false;
    }

    if (!replaceConfigFile()) {
        Esp32BaseFs::removeFile(kConfigTempPath);
        return false;
    }

    g_config = config;
    g_loadedFromDefaults = false;
    g_configValid = true;
    setLastError("ok");
    ESP32BASE_LOG_I("config_store", "config_saved bytes=%u", static_cast<unsigned>(writtenLen));
    return true;
}

const IrrigationConfig& ConfigStore::config() {
    return g_config;
}

IrrigationConfig& ConfigStore::mutableConfig() {
    return g_config;
}

bool ConfigStore::loadedFromDefaults() {
    return g_loadedFromDefaults;
}

bool ConfigStore::configValid() {
    return g_configValid;
}

const char* ConfigStore::lastError() {
    return g_lastError;
}

const char* ConfigStore::path() {
    return kConfigPath;
}

} // namespace Irrigation
