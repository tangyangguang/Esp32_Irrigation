#pragma once

#include <cstdint>

#include "IrrigationTypes.h"

class IrrigationConfigStore {
public:
    enum class LoadResult : uint8_t {
        NotLoaded,
        Loaded,
        CreatedDefault,
        StorageUnavailable,
        InvalidConfig,
        WriteFailed,
    };

    bool begin();
    bool save(const IrrigationConfig& proposed, uint32_t expectedRevision);
    bool applyRuntimeParameters(const IrrigationConfig& source);

    bool ready() const;
    const IrrigationConfig* current() const;
    LoadResult loadResult() const;
    const char* lastError() const;

private:
    bool readConfig(const char* path, IrrigationConfig& config) const;
    bool writeConfig(const IrrigationConfig& config);
    static bool ensureDirectory(const char* path);
    static bool removeExisting(const char* path);

    IrrigationConfig config_{};
    LoadResult loadResult_ = LoadResult::NotLoaded;
    const char* lastError_ = "not_loaded";
    bool ready_ = false;
};
