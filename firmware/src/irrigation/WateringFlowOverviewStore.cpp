#include "WateringFlowOverviewStore.h"

#include <Esp32Base.h>

#include <cstring>
#include <memory>
#include <new>

namespace {

constexpr const char* kDirectory = "/app/irrigation";
constexpr const char* kFilePath = "/app/irrigation/last_flow.bin";
constexpr const char* kTempPath = "/app/irrigation/last_flow.tmp";
constexpr uint32_t kMagic = 0x4F4C5749U;  // "IWLO"
constexpr uint16_t kVersion = 3;
constexpr uint32_t kCrcPolynomial = 0xedb88320U;

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t headerBytes;
    uint32_t recordId;
    uint32_t startedEpoch;
    uint8_t zoneCount;
    uint16_t pointCount;
    uint16_t zonePointCount[BoardPins::kZoneCount];
    uint32_t zoneBaselineFlow[BoardPins::kZoneCount];
    uint32_t crc;  // CRC of all preceding header bytes
};
#pragma pack(pop)

uint32_t crc32(const uint8_t* data, std::size_t length) {
    uint32_t crc = UINT32_MAX;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (kCrcPolynomial & (0U - (crc & 1U)));
    }
    return ~crc;
}

bool ensureDirectory() {
    if (!Esp32BaseFs::exists("/app") && !Esp32BaseFs::mkdir("/app")) return false;
    return Esp32BaseFs::exists(kDirectory) || Esp32BaseFs::mkdir(kDirectory);
}

}  // namespace

void WateringFlowOverviewStore::begin() {
    loaded_ = Snapshot{};
    if (!Esp32BaseFs::isReady()) return;

    const int64_t fileSize = Esp32BaseFs::fileSize(kFilePath);
    const int64_t minSize = static_cast<int64_t>(sizeof(Header));
    const int64_t maxSize =
        minSize + static_cast<int64_t>(kFlowOverviewSampleCount) * 4;
    if (fileSize < minSize || fileSize > maxSize) return;

    std::unique_ptr<uint8_t[]> buffer(
        new (std::nothrow) uint8_t[static_cast<std::size_t>(fileSize)]);
    if (!buffer) return;
    std::size_t readLength = 0;
    if (!Esp32BaseFs::readBytes(kFilePath, buffer.get(),
                                static_cast<std::size_t>(fileSize),
                                &readLength) ||
        readLength != static_cast<std::size_t>(fileSize)) {
        return;
    }

    Header header{};
    std::memcpy(&header, buffer.get(), sizeof(header));
    if (header.magic != kMagic || header.version != kVersion ||
        header.headerBytes != sizeof(Header) ||
        header.zoneCount == 0 ||
        header.zoneCount > BoardPins::kZoneCount ||
        header.pointCount > kFlowOverviewSampleCount ||
        readLength != sizeof(Header) +
                           static_cast<std::size_t>(header.pointCount) * 4U ||
        header.crc != crc32(buffer.get(), sizeof(Header) - sizeof(uint32_t))) {
        return;
    }

    // The sum of per-zone points must equal the total point count.
    uint32_t zoneSum = 0;
    for (uint8_t z = 0; z < header.zoneCount; ++z) zoneSum += header.zonePointCount[z];
    if (zoneSum != header.pointCount) return;

    Snapshot snapshot{};
    snapshot.valid = true;
    snapshot.recordId = header.recordId;
    snapshot.startedEpoch = header.startedEpoch;
    snapshot.zoneCount = header.zoneCount;
    snapshot.pointCount = header.pointCount;
    for (uint8_t z = 0; z < header.zoneCount; ++z) {
        snapshot.zonePointCount[z] = header.zonePointCount[z];
        snapshot.zoneBaselineFlow[z] = header.zoneBaselineFlow[z];
    }
    // Heap sized to the real point count; the app hands it to the controller
    // and then calls releaseLoadedPoints() so only one copy stays resident.
    if (header.pointCount > 0) {
        snapshot.points.reset(new (std::nothrow) uint32_t[header.pointCount]);
        if (!snapshot.points) return;
        std::memcpy(snapshot.points.get(), buffer.get() + sizeof(Header),
                    static_cast<std::size_t>(header.pointCount) * 4U);
    }
    loaded_ = std::move(snapshot);
}

void WateringFlowOverviewStore::releaseLoadedPoints() {
    loaded_.points.reset();
}

bool WateringFlowOverviewStore::save(uint32_t recordId,
                                     uint32_t startedEpoch,
                                     uint8_t zoneCount,
                                     const uint16_t* zonePointCount,
                                     const uint32_t* zoneBaselineFlow,
                                     const uint32_t* points,
                                     uint16_t pointCount) {
    if (!Esp32BaseFs::isReady() || zonePointCount == nullptr || points == nullptr ||
        zoneCount == 0 || zoneCount > BoardPins::kZoneCount ||
        pointCount > kFlowOverviewSampleCount || !ensureDirectory()) {
        return false;
    }
    uint32_t zoneSum = 0;
    for (uint8_t z = 0; z < zoneCount; ++z) zoneSum += zonePointCount[z];
    if (zoneSum != pointCount) return false;

    Header header{};
    header.magic = kMagic;
    header.version = kVersion;
    header.headerBytes = sizeof(Header);
    header.recordId = recordId;
    header.startedEpoch = startedEpoch;
    header.zoneCount = zoneCount;
    header.pointCount = pointCount;
    for (uint8_t z = 0; z < zoneCount; ++z) {
        header.zonePointCount[z] = zonePointCount[z];
        header.zoneBaselineFlow[z] =
            zoneBaselineFlow != nullptr ? zoneBaselineFlow[z] : 0U;
    }
    header.crc = crc32(reinterpret_cast<const uint8_t*>(&header),
                       sizeof(Header) - sizeof(uint32_t));

    // One combined heap buffer (at most ~17 KB at six hours) written atomically
    // to the temp file; allocated on the service task, freed on exit.
    const std::size_t totalSize =
        sizeof(Header) + static_cast<std::size_t>(pointCount) * 4U;
    std::unique_ptr<uint8_t[]> encoded(new (std::nothrow) uint8_t[totalSize]);
    if (!encoded) return false;
    std::memcpy(encoded.get(), &header, sizeof(Header));
    if (pointCount > 0) {
        std::memcpy(encoded.get() + sizeof(Header), points,
                    static_cast<std::size_t>(pointCount) * 4U);
    }
    if (!Esp32BaseFs::writeBytes(kTempPath, encoded.get(), totalSize)) {
        Esp32BaseFs::removeFileWithRecovery(kTempPath);
        return false;
    }

    Snapshot snapshot{};
    // Verify the temp file before swapping it into place.
    const int64_t verifySize = Esp32BaseFs::fileSize(kTempPath);
    const std::size_t expectedSize =
        sizeof(Header) + static_cast<std::size_t>(pointCount) * 4U;
    if (verifySize != static_cast<int64_t>(expectedSize)) {
        Esp32BaseFs::removeFileWithRecovery(kTempPath);
        return false;
    }
    std::unique_ptr<uint8_t[]> verify(new (std::nothrow) uint8_t[expectedSize]);
    if (!verify) {
        Esp32BaseFs::removeFileWithRecovery(kTempPath);
        return false;
    }
    std::size_t verifyLength = 0;
    if (!Esp32BaseFs::readBytes(kTempPath, verify.get(), expectedSize,
                                &verifyLength) ||
        verifyLength != expectedSize ||
        std::memcmp(verify.get(), encoded.get(), expectedSize) != 0) {
        Esp32BaseFs::removeFileWithRecovery(kTempPath);
        return false;
    }

    Esp32BaseFs::removeFileWithRecovery(kFilePath);
    if (!Esp32BaseFs::rename(kTempPath, kFilePath)) {
        Esp32BaseFs::removeFileWithRecovery(kTempPath);
        return false;
    }

    // The controller already owns these points in RAM, so keep only the small
    // metadata here (used for boot id matching); do not hold a second full copy.
    loaded_ = Snapshot{};
    loaded_.valid = true;
    loaded_.recordId = recordId;
    loaded_.startedEpoch = startedEpoch;
    loaded_.zoneCount = zoneCount;
    loaded_.pointCount = pointCount;
    for (uint8_t z = 0; z < zoneCount; ++z) {
        loaded_.zonePointCount[z] = zonePointCount[z];
        loaded_.zoneBaselineFlow[z] =
            zoneBaselineFlow != nullptr ? zoneBaselineFlow[z] : 0U;
    }
    return true;
}

void WateringFlowOverviewStore::discard() {
    loaded_ = Snapshot{};
    if (Esp32BaseFs::isReady()) {
        Esp32BaseFs::removeFileWithRecovery(kFilePath);
        Esp32BaseFs::removeFileWithRecovery(kTempPath);
    }
}
