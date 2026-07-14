#include "DeviceAliveCheckpoint.h"

#include <cstring>

namespace {

constexpr const char* kNamespace = "irrigation";
constexpr const char* kKey = "alive";
constexpr uint32_t kMagic = 0x31435641UL;  // AVC1, little-endian.
constexpr uint8_t kVersion = 1;
constexpr std::size_t kEncodedSize = 16;

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

}  // namespace

bool DeviceAliveCheckpoint::begin() {
    checkpointEpoch_ = 0;
    lastActivityEpoch_ = 0;
    observedActivitySequence_ = 0;
    timeInitialized_ = false;
    storageFault_ = false;
    if (!Esp32BaseConfig::isReady()) {
        storageFault_ = true;
        return false;
    }
    uint8_t encoded[kEncodedSize]{};
    if (!Esp32BaseConfig::getBlob(kNamespace, kKey, encoded, sizeof(encoded))) {
        return true;
    }
    if (get32(encoded) != kMagic || encoded[4] != kVersion || encoded[5] != 0 ||
        encoded[6] != 0 || encoded[7] != 0 ||
        get32(encoded + 12) != crc32(encoded, 12)) {
        storageFault_ = true;
        return false;
    }
    checkpointEpoch_ = get32(encoded + 8);
    if (checkpointEpoch_ == 0) {
        storageFault_ = true;
        return false;
    }
    return true;
}

void DeviceAliveCheckpoint::handle(const Esp32BaseTime::Snapshot& now,
                                   uint8_t intervalHours,
                                   bool wateringActive,
                                   uint64_t activitySequence) {
    if (storageFault_ || !now.synced || now.epochSec == 0) {
        return;
    }
    if (!timeInitialized_) {
        lastActivityEpoch_ = now.epochSec;
        observedActivitySequence_ = activitySequence;
        timeInitialized_ = true;
        return;
    }
    if (activitySequence != observedActivitySequence_) {
        observedActivitySequence_ = activitySequence;
        lastActivityEpoch_ = now.epochSec;
        return;
    }
    if (now.epochSec < lastActivityEpoch_) {
        lastActivityEpoch_ = now.epochSec;
        return;
    }
    if (intervalHours == 0 || wateringActive) {
        return;
    }
    const uint32_t intervalSec = static_cast<uint32_t>(intervalHours) * 3600U;
    if (now.epochSec - lastActivityEpoch_ >= intervalSec && save(now.epochSec)) {
        lastActivityEpoch_ = now.epochSec;
    }
}

uint32_t DeviceAliveCheckpoint::lastKnownAliveEpoch() const {
    return checkpointEpoch_;
}

bool DeviceAliveCheckpoint::storageFault() const {
    return storageFault_;
}

bool DeviceAliveCheckpoint::save(uint32_t epochSec) {
    uint8_t encoded[kEncodedSize]{};
    put32(encoded, kMagic);
    encoded[4] = kVersion;
    put32(encoded + 8, epochSec);
    put32(encoded + 12, crc32(encoded, 12));
    if (!Esp32BaseConfig::setBlob(kNamespace, kKey, encoded, sizeof(encoded))) {
        storageFault_ = true;
        return false;
    }
    checkpointEpoch_ = epochSec;
    return true;
}
