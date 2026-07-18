#include "WateringSchedulerStore.h"

#include <Esp32Base.h>

#include <cstring>

namespace {

constexpr const char* kNamespace = "irrigation";
constexpr const char* kStateKey = "sched_state";
constexpr const char* kInitializedKey = "sched_init";
constexpr uint32_t kMagic = 0x31435357UL;  // WSC1, little-endian.
constexpr uint8_t kVersion = 1;
constexpr std::size_t kCrcOffset = 28;

void put32(uint8_t* target, uint32_t value) {
    target[0] = static_cast<uint8_t>(value);
    target[1] = static_cast<uint8_t>(value >> 8U);
    target[2] = static_cast<uint8_t>(value >> 16U);
    target[3] = static_cast<uint8_t>(value >> 24U);
}

uint32_t get32(const uint8_t* source) {
    return static_cast<uint32_t>(source[0]) |
           static_cast<uint32_t>(source[1]) << 8U |
           static_cast<uint32_t>(source[2]) << 16U |
           static_cast<uint32_t>(source[3]) << 24U;
}

uint32_t crc32(const uint8_t* data, std::size_t size) {
    uint32_t crc = UINT32_MAX;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

bool validState(const WateringSchedulerPersistentState& state) {
    if (state.mode != AutomaticWateringMode::Enabled &&
        state.mode != AutomaticWateringMode::PausedIndefinitely &&
        state.mode != AutomaticWateringMode::PausedUntil) {
        return false;
    }
    if (state.mode == AutomaticWateringMode::PausedUntil) {
        return state.resumeAtEpoch != 0;
    }
    return state.resumeAtEpoch == 0;
}

}  // namespace

bool WateringSchedulerStateCodec::encode(const WateringSchedulerPersistentState& state,
                                         uint8_t* output,
                                         std::size_t outputSize) {
    if (!output || outputSize != kEncodedSize || !validState(state)) {
        return false;
    }
    std::memset(output, 0, outputSize);
    put32(output, kMagic);
    output[4] = kVersion;
    output[5] = static_cast<uint8_t>(state.mode);
    put32(output + 8, state.resumeAtEpoch);
    put32(output + 12, state.currentLocalDay);
    put32(output + 16, state.currentProcessedMask);
    put32(output + 20, state.previousLocalDay);
    put32(output + 24, state.previousProcessedMask);
    put32(output + kCrcOffset, crc32(output, kCrcOffset));
    return true;
}

bool WateringSchedulerStateCodec::decode(const uint8_t* data,
                                         std::size_t dataSize,
                                         WateringSchedulerPersistentState& state) {
    state = {};
    if (!data || dataSize != kEncodedSize || get32(data) != kMagic ||
        data[4] != kVersion || data[6] != 0 || data[7] != 0 ||
        get32(data + kCrcOffset) != crc32(data, kCrcOffset)) {
        return false;
    }
    WateringSchedulerPersistentState decoded{};
    decoded.mode = static_cast<AutomaticWateringMode>(data[5]);
    decoded.resumeAtEpoch = get32(data + 8);
    decoded.currentLocalDay = get32(data + 12);
    decoded.currentProcessedMask = get32(data + 16);
    decoded.previousLocalDay = get32(data + 20);
    decoded.previousProcessedMask = get32(data + 24);
    if (!validState(decoded)) {
        return false;
    }
    state = decoded;
    return true;
}

SchedulerStorageLoadResult WateringSchedulerStore::load(
    WateringSchedulerPersistentState& state) {
    state = {};
    if (!Esp32BaseConfig::isReady()) {
        return SchedulerStorageLoadResult::Error;
    }
    uint8_t encoded[WateringSchedulerStateCodec::kEncodedSize]{};
    if (!Esp32BaseConfig::getBlob(kNamespace, kStateKey, encoded, sizeof(encoded))) {
        return Esp32BaseConfig::getBool(kNamespace, kInitializedKey, false)
                   ? SchedulerStorageLoadResult::Invalid
                   : SchedulerStorageLoadResult::Missing;
    }
    return WateringSchedulerStateCodec::decode(encoded, sizeof(encoded), state)
               ? SchedulerStorageLoadResult::Loaded
               : SchedulerStorageLoadResult::Invalid;
}

bool WateringSchedulerStore::save(const WateringSchedulerPersistentState& state) {
    uint8_t encoded[WateringSchedulerStateCodec::kEncodedSize]{};
    if (!Esp32BaseConfig::isReady() ||
        !WateringSchedulerStateCodec::encode(state, encoded, sizeof(encoded))) {
        return false;
    }
    if (!Esp32BaseConfig::setBlob(kNamespace, kStateKey, encoded, sizeof(encoded))) {
        return false;
    }
    return Esp32BaseConfig::getBool(kNamespace, kInitializedKey, false) ||
           Esp32BaseConfig::setBool(kNamespace, kInitializedKey, true);
}

bool WateringSchedulerStore::clear() {
    return Esp32BaseConfig::isReady() && Esp32BaseConfig::clearNamespace(kNamespace);
}
