#include "WateringFlowOverviewStore.h"

#include <Esp32Base.h>

#include <cstring>
#include <memory>
#include <new>

namespace {

constexpr const char* kDirectory = "/app/irrigation";
constexpr const char* kFilePath = "/app/irrigation/last_flow.bin";
constexpr const char* kTempPath = "/app/irrigation/last_flow.tmp";
constexpr const char* kBackupPath = "/app/irrigation/last_flow.bak";
constexpr uint32_t kMagic = 0x4F4C5749U;  // "IWLO"
// v4: CRC covers the header (excluding its trailing crc field) plus every
// point. v3 (header-only CRC, experiment-only, never released) is rejected.
constexpr uint16_t kVersion = 4;
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
    uint32_t crc;  // CRC of every preceding byte (header minus crc) + points
};
#pragma pack(pop)

// Fold bytes into a running CRC32 (0xedb88320, init/final XOR 0xFFFFFFFF),
// so the header prefix and the points can both be covered while skipping the
// crc field that sits between them.
uint32_t crc32Append(uint32_t crc, const uint8_t* data, std::size_t length) {
    crc = ~crc;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (kCrcPolynomial & (0U - (crc & 1U)));
    }
    return ~crc;
}

// CRC of the on-file record excluding its 4-byte crc field (the last packed
// header member, immediately before the points): the header prefix up to the
// crc slot, then every point byte. One contiguous range would wrongly include
// the crc slot that lies between the two regions.
uint32_t recordCrc(const uint8_t* buffer, std::size_t pointBytes) {
    constexpr std::size_t kPrefixBytes = sizeof(Header) - sizeof(uint32_t);
    // crc32Append() inverts on entry, so a 0 seed yields the standard init.
    uint32_t crc = crc32Append(0U, buffer, kPrefixBytes);
    return crc32Append(crc, buffer + sizeof(Header), pointBytes);
}

bool ensureDirectory() {
    if (!Esp32BaseFs::exists("/app") && !Esp32BaseFs::mkdir("/app")) return false;
    return Esp32BaseFs::exists(kDirectory) || Esp32BaseFs::mkdir(kDirectory);
}

bool removeIfExists(const char* path) {
    if (!Esp32BaseFs::exists(path)) return true;
    return Esp32BaseFs::removeFileWithRecovery(path) ==
           Esp32BaseFs::REMOVE_FILE_DELETED;
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
    const std::size_t pointBytes =
        static_cast<std::size_t>(header.pointCount) * 4U;
    if (header.magic != kMagic || header.version != kVersion ||
        header.headerBytes != sizeof(Header) ||
        header.zoneCount == 0 ||
        header.zoneCount > BoardPins::kZoneCount ||
        header.pointCount > kFlowOverviewSampleCount ||
        readLength != sizeof(Header) + pointBytes ||
        header.crc != recordCrc(buffer.get(), pointBytes)) {
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
                    pointBytes);
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

    // One combined heap buffer (at most ~17 KB at six hours); CRC covers the
    // header (minus its trailing crc field) and every point.
    const std::size_t pointBytes = static_cast<std::size_t>(pointCount) * 4U;
    const std::size_t totalSize = sizeof(Header) + pointBytes;
    std::unique_ptr<uint8_t[]> encoded(new (std::nothrow) uint8_t[totalSize]);
    if (!encoded) return false;
    std::memcpy(encoded.get(), &header, sizeof(Header));
    if (pointCount > 0) {
        std::memcpy(encoded.get() + sizeof(Header), points, pointBytes);
    }
    header.crc = recordCrc(encoded.get(), pointBytes);
    // crc is the last packed header field; write the whole header back so the
    // crc slot is actually persisted.
    std::memcpy(encoded.get(), &header, sizeof(Header));

    if (!Esp32BaseFs::writeBytes(kTempPath, encoded.get(), totalSize)) {
        removeIfExists(kTempPath);
        return false;
    }

    // Verify the temp file (length + CRC) before swapping it into place.
    const int64_t verifySize = Esp32BaseFs::fileSize(kTempPath);
    if (verifySize != static_cast<int64_t>(totalSize)) {
        removeIfExists(kTempPath);
        return false;
    }
    std::unique_ptr<uint8_t[]> verify(new (std::nothrow) uint8_t[totalSize]);
    if (!verify) {
        removeIfExists(kTempPath);
        return false;
    }
    std::size_t verifyLength = 0;
    if (!Esp32BaseFs::readBytes(kTempPath, verify.get(), totalSize,
                                &verifyLength) ||
        verifyLength != totalSize) {
        removeIfExists(kTempPath);
        return false;
    }
    Header verifyHeader{};
    std::memcpy(&verifyHeader, verify.get(), sizeof(Header));
    if (recordCrc(verify.get(), pointBytes) != verifyHeader.crc) {
        removeIfExists(kTempPath);
        return false;
    }

    // Backup -> promote temp -> rollback on failure, mirroring ConfigStore so
    // a failed replace never leaves the previous overview silently deleted.
    if (!removeIfExists(kBackupPath)) {
        removeIfExists(kTempPath);
        return false;
    }
    const bool hadCurrent = Esp32BaseFs::exists(kFilePath);
    if (hadCurrent && !Esp32BaseFs::rename(kFilePath, kBackupPath)) {
        removeIfExists(kTempPath);
        return false;
    }
    if (!Esp32BaseFs::rename(kTempPath, kFilePath)) {
        if (hadCurrent) Esp32BaseFs::rename(kBackupPath, kFilePath);
        removeIfExists(kTempPath);
        return false;
    }
    removeIfExists(kBackupPath);

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
        removeIfExists(kFilePath);
        removeIfExists(kTempPath);
        removeIfExists(kBackupPath);
    }
}
