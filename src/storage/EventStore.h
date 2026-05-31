#pragma once

#include <stdint.h>

#include "domain/ZoneTypes.h"

namespace EventStore {

static constexpr uint16_t Capacity = 256;

struct Event {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t id;
    uint32_t uptimeMs;
    uint32_t epoch;
    uint8_t type;
    uint8_t source;
    uint8_t zoneId;
    uint8_t code;
    int32_t value1;
    int32_t value2;
    char text[32];
};

static_assert(sizeof(Event) == 64, "EventStore::Event binary layout changed");

using ReadCallback = void (*)(const Event& event, void* user);

void begin();
bool append(Irrigation::EventType type, Irrigation::EventSource source, uint8_t zoneId, uint8_t code, int32_t value1, int32_t value2, const char* text);
bool clear();
uint16_t count();
uint16_t capacity();
bool readLatest(uint16_t offset, uint16_t limit, ReadCallback callback, void* user);

const char* typeName(Irrigation::EventType type);
const char* sourceName(Irrigation::EventSource source);

}
