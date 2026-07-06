#include "storage/HistoryStore.h"

#include <Arduino.h>
#include <Esp32Base.h>
#include <string.h>

namespace Irrigation {
namespace {
constexpr const char* HISTORY_DIR_APP = "/app";
constexpr const char* HISTORY_DIR = "/app/irrigation";
constexpr const char* HISTORY_PATH = "/app/irrigation/history.bin";
constexpr uint32_t HISTORY_MAGIC = 0x49524831UL;
constexpr uint16_t HISTORY_VERSION = 1;

struct HistoryFileHeader {
    uint32_t magic = HISTORY_MAGIC;
    uint16_t version = HISTORY_VERSION;
    uint16_t recordSize = sizeof(HistoryRecord);
    uint8_t capacity = MAX_HISTORY_RECORDS;
    uint8_t count = 0;
    uint8_t next = 0;
    uint8_t reserved = 0;
    uint32_t seq = 0;
};

constexpr uint32_t HISTORY_FILE_SIZE =
    sizeof(HistoryFileHeader) + sizeof(HistoryRecord) * MAX_HISTORY_RECORDS;

uint32_t recordOffset(uint8_t physical) {
    return sizeof(HistoryFileHeader) + static_cast<uint32_t>(physical) * sizeof(HistoryRecord);
}

bool validHeader(const HistoryFileHeader& header) {
    return header.magic == HISTORY_MAGIC &&
           header.version == HISTORY_VERSION &&
           header.recordSize == sizeof(HistoryRecord) &&
           header.capacity == MAX_HISTORY_RECORDS &&
           header.count <= MAX_HISTORY_RECORDS &&
           header.next < MAX_HISTORY_RECORDS;
}
}

void HistoryStore::begin() {
    resetMemory();
    load();
}

void HistoryStore::resetMemory() {
    _count = 0;
    _next = 0;
    _seq = 0;
    _persistent = false;
    memset(_records, 0, sizeof(_records));
}

void HistoryStore::load() {
    if (!Esp32BaseFs::isReady()) {
        return;
    }
    Esp32BaseFs::mkdir(HISTORY_DIR_APP);
    Esp32BaseFs::mkdir(HISTORY_DIR);
    if (!Esp32BaseFs::createFixedFile(HISTORY_PATH, HISTORY_FILE_SIZE, 0)) {
        return;
    }

    HistoryFileHeader header;
    size_t readLen = 0;
    if (!Esp32BaseFs::readBytesAt(HISTORY_PATH, 0, reinterpret_cast<uint8_t*>(&header), sizeof(header), &readLen) ||
        readLen != sizeof(header) ||
        !validHeader(header)) {
        _persistent = true;
        persistHeader();
        for (uint8_t i = 0; i < MAX_HISTORY_RECORDS; ++i) {
            persistRecord(i);
        }
        return;
    }

    _count = header.count;
    _next = header.next;
    _seq = header.seq;
    for (uint8_t i = 0; i < MAX_HISTORY_RECORDS; ++i) {
        readLen = 0;
        if (!Esp32BaseFs::readBytesAt(HISTORY_PATH,
                                      recordOffset(i),
                                      reinterpret_cast<uint8_t*>(&_records[i]),
                                      sizeof(HistoryRecord),
                                      &readLen) ||
            readLen != sizeof(HistoryRecord)) {
            resetMemory();
            return;
        }
    }
    _persistent = true;
}

void HistoryStore::add(uint8_t type,
                       uint8_t sourceId,
                       uint8_t zoneId,
                       uint8_t resultCode,
                       uint16_t taskId,
                       uint32_t plannedSec,
                       uint32_t actualSec,
                       const char* message) {
    HistoryRecord& record = _records[_next];
    record.seq = ++_seq;
    record.uptimeMs = millis();
    record.type = type;
    record.sourceId = sourceId;
    record.zoneId = zoneId;
    record.resultCode = resultCode;
    record.taskId = taskId;
    record.plannedSec = plannedSec;
    record.actualSec = actualSec;
    strlcpy(record.message, message ? message : "", sizeof(record.message));

    const uint8_t written = _next;
    _next = static_cast<uint8_t>((_next + 1) % MAX_HISTORY_RECORDS);
    if (_count < MAX_HISTORY_RECORDS) {
        ++_count;
    }
    if (_persistent) {
        persistRecord(written);
        persistHeader();
    }
}

uint8_t HistoryStore::count() const {
    return _count;
}

const HistoryRecord& HistoryStore::record(uint8_t index) const {
    static const HistoryRecord empty;
    if (index >= _count) {
        return empty;
    }
    const uint8_t physical = static_cast<uint8_t>((_next + MAX_HISTORY_RECORDS - 1 - index) % MAX_HISTORY_RECORDS);
    return _records[physical];
}

bool HistoryStore::persistHeader() {
    if (!Esp32BaseFs::isReady()) {
        _persistent = false;
        return false;
    }
    HistoryFileHeader header;
    header.count = _count;
    header.next = _next;
    header.seq = _seq;
    const bool ok = Esp32BaseFs::writeBytesAt(HISTORY_PATH,
                                             0,
                                             reinterpret_cast<const uint8_t*>(&header),
                                             sizeof(header));
    if (!ok) {
        _persistent = false;
    }
    return ok;
}

bool HistoryStore::persistRecord(uint8_t physical) {
    if (physical >= MAX_HISTORY_RECORDS || !Esp32BaseFs::isReady()) {
        _persistent = false;
        return false;
    }
    const bool ok = Esp32BaseFs::writeBytesAt(HISTORY_PATH,
                                             recordOffset(physical),
                                             reinterpret_cast<const uint8_t*>(&_records[physical]),
                                             sizeof(HistoryRecord));
    if (!ok) {
        _persistent = false;
    }
    return ok;
}

}  // namespace Irrigation
