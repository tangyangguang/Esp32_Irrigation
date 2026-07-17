#include "IrrigationConfigStore.h"

#include <Esp32Base.h>

#include <array>
#include <memory>
#include <new>
#include <string>

#include "IrrigationConfig.h"
#include "IrrigationConfigJson.h"

namespace {

constexpr const char* kConfigDirectory = "/app/irrigation";
constexpr const char* kConfigPath = "/app/irrigation/config.json";
constexpr const char* kTempPath = "/app/irrigation/config.tmp";
constexpr const char* kBackupPath = "/app/irrigation/config.bak";
constexpr std::size_t kMaxConfigBytes = 16U * 1024U;

bool sameCanonicalConfig(const IrrigationConfig& config, const std::string& expected) {
    std::string actual;
    return IrrigationConfigJson::encode(config, actual) && actual == expected;
}

}  // namespace

bool IrrigationConfigStore::begin() {
    ready_ = false;
    loadResult_ = LoadResult::NotLoaded;
    lastError_ = "not_loaded";
    if (!Esp32BaseFs::isReady()) {
        loadResult_ = LoadResult::StorageUnavailable;
        lastError_ = "filesystem_unavailable";
        return false;
    }

    bool anyFileExists = false;
    std::size_t bestPath = 0;
    const bool foundValid = loadBestConfig(anyFileExists, bestPath);

    if (!foundValid) {
        if (anyFileExists) {
            loadResult_ = LoadResult::InvalidConfig;
            lastError_ = "no_valid_config_copy";
            return false;
        }
        config_ = IrrigationConfigRules::createDefault();
        if (!writeConfig(config_)) {
            loadResult_ = LoadResult::WriteFailed;
            lastError_ = "default_config_write_failed";
            return false;
        }
        ready_ = true;
        loadResult_ = LoadResult::CreatedDefault;
        lastError_ = "none";
        return true;
    }

    if (bestPath != 0 && !writeConfig(config_)) {
        loadResult_ = LoadResult::WriteFailed;
        lastError_ = "config_recovery_write_failed";
        return false;
    }
    ready_ = true;
    loadResult_ = LoadResult::Loaded;
    lastError_ = "none";
    return true;
}

bool IrrigationConfigStore::loadBestConfig(bool& anyFileExists,
                                           std::size_t& bestPath) {
    constexpr std::array<const char*, 3> paths = {kConfigPath, kTempPath, kBackupPath};
    anyFileExists = false;
    bestPath = 0;
    bool foundValid = false;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (!Esp32BaseFs::exists(paths[index])) continue;
        anyFileExists = true;
        verificationScratch_ = {};
        if (readConfig(paths[index], verificationScratch_) &&
            (!foundValid ||
             verificationScratch_.revision > config_.revision)) {
            foundValid = true;
            bestPath = index;
            config_ = verificationScratch_;
        }
    }
    return foundValid;
}

bool IrrigationConfigStore::save(const IrrigationConfig& proposed, uint32_t expectedRevision) {
    if (!ready_) {
        lastError_ = "config_not_ready";
        return false;
    }
    if (expectedRevision != config_.revision) {
        lastError_ = "config_revision_mismatch";
        return false;
    }
    if (config_.revision == UINT32_MAX) {
        lastError_ = "config_revision_exhausted";
        return false;
    }
    saveScratch_ = proposed;
    saveScratch_.schemaVersion = kIrrigationConfigSchemaVersion;
    saveScratch_.revision = config_.revision + 1U;
    if (!IrrigationConfigRules::validate(saveScratch_)) {
        lastError_ = "config_validation_failed";
        return false;
    }
    if (!writeConfig(saveScratch_)) {
        lastError_ = "config_write_failed";
        return false;
    }
    config_ = saveScratch_;
    lastError_ = "none";
    return true;
}

bool IrrigationConfigStore::applyRuntimeParameters(const IrrigationConfig& source) {
    if (!ready_) return false;
    saveScratch_ = config_;
    saveScratch_.valveDrive = source.valveDrive;
    saveScratch_.pump = source.pump;
    saveScratch_.flowMeter = source.flowMeter;
    saveScratch_.flowProtection = source.flowProtection;
    saveScratch_.timeSafety = source.timeSafety;
    if (!IrrigationConfigRules::validate(saveScratch_)) return false;
    config_ = saveScratch_;
    return true;
}

bool IrrigationConfigStore::ready() const {
    return ready_;
}

const IrrigationConfig* IrrigationConfigStore::current() const {
    return ready_ ? &config_ : nullptr;
}

IrrigationConfigStore::LoadResult IrrigationConfigStore::loadResult() const {
    return loadResult_;
}

const char* IrrigationConfigStore::lastError() const {
    return lastError_;
}

bool IrrigationConfigStore::readConfig(const char* path, IrrigationConfig& config) const {
    const int64_t fileSize = Esp32BaseFs::fileSize(path);
    if (fileSize <= 0 || fileSize > static_cast<int64_t>(kMaxConfigBytes)) {
        return false;
    }
    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[static_cast<std::size_t>(fileSize)]);
    if (!data) {
        return false;
    }
    std::size_t readLength = 0;
    if (!Esp32BaseFs::readBytes(path,
                                data.get(),
                                static_cast<std::size_t>(fileSize),
                                &readLength) ||
        readLength != static_cast<std::size_t>(fileSize)) {
        return false;
    }
    return IrrigationConfigJson::decode(reinterpret_cast<const char*>(data.get()), readLength, config);
}

bool IrrigationConfigStore::writeConfig(const IrrigationConfig& config) {
    std::string json;
    if (!IrrigationConfigJson::encode(config, json) ||
        json.empty() || json.size() > kMaxConfigBytes ||
        !ensureDirectory(kConfigDirectory) ||
        !Esp32BaseFs::writeBytes(kTempPath,
                                 reinterpret_cast<const uint8_t*>(json.data()),
                                 json.size())) {
        return false;
    }

    verificationScratch_ = {};
    if (!readConfig(kTempPath, verificationScratch_) ||
        !sameCanonicalConfig(verificationScratch_, json)) {
        return false;
    }
    if (!removeExisting(kBackupPath)) {
        return false;
    }

    const bool hadCurrent = Esp32BaseFs::exists(kConfigPath);
    if (hadCurrent && !Esp32BaseFs::rename(kConfigPath, kBackupPath)) {
        return false;
    }
    if (!Esp32BaseFs::rename(kTempPath, kConfigPath)) {
        if (hadCurrent) {
            Esp32BaseFs::rename(kBackupPath, kConfigPath);
        }
        return false;
    }

    verificationScratch_ = {};
    if (readConfig(kConfigPath, verificationScratch_) &&
        sameCanonicalConfig(verificationScratch_, json)) {
        return true;
    }

    removeExisting(kConfigPath);
    if (hadCurrent) {
        Esp32BaseFs::rename(kBackupPath, kConfigPath);
    }
    return false;
}

bool IrrigationConfigStore::ensureDirectory(const char* path) {
    if (!Esp32BaseFs::exists("/app") && !Esp32BaseFs::mkdir("/app")) {
        return false;
    }
    return Esp32BaseFs::exists(path) || Esp32BaseFs::mkdir(path);
}

bool IrrigationConfigStore::removeExisting(const char* path) {
    if (!Esp32BaseFs::exists(path)) {
        return true;
    }
    return Esp32BaseFs::removeFileWithRecovery(path) ==
           Esp32BaseFs::REMOVE_FILE_DELETED;
}
