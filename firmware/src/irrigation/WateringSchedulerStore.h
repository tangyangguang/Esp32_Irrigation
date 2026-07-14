#pragma once

#include <cstddef>
#include <cstdint>

#include "WateringScheduler.h"

class WateringSchedulerStateCodec {
public:
    static constexpr std::size_t kEncodedSize = 32;

    static bool encode(const WateringSchedulerPersistentState& state,
                       uint8_t* output,
                       std::size_t outputSize);
    static bool decode(const uint8_t* data,
                       std::size_t dataSize,
                       WateringSchedulerPersistentState& state);
};

class WateringSchedulerStore : public WateringSchedulerStorage {
public:
    SchedulerStorageLoadResult load(WateringSchedulerPersistentState& state) override;
    bool save(const WateringSchedulerPersistentState& state) override;
    bool clear() override;
};
