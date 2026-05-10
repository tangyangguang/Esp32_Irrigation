#pragma once

#include <stdint.h>

namespace EventStore {

static constexpr uint16_t Capacity = 256;

enum Type : uint8_t {
    TYPE_BOOT = 1,
    TYPE_CONFIG_CHANGED = 2,
    TYPE_PLAN_CHANGED = 3,
    TYPE_WATER_START = 4,
    TYPE_WATER_STOP = 5,
    TYPE_WATER_ERROR = 6,
    TYPE_LEAK_ALERT = 7,
    TYPE_ALERT_CLEAR = 8,
    TYPE_FACTORY_RESET_REQUESTED = 9,
    TYPE_FACTORY_RESET_EXECUTED = 10,
};

enum Source : uint8_t {
    SOURCE_SYSTEM = 0,
    SOURCE_BUTTON = 1,
    SOURCE_WEB = 2,
    SOURCE_PLAN = 3,
};

struct Event {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t id;
    uint32_t uptimeMs;
    uint32_t epoch;
    uint8_t type;
    uint8_t source;
    uint8_t road;
    uint8_t code;
    int32_t value1;
    int32_t value2;
    char text[32];
};

static_assert(sizeof(Event) == 64, "EventStore::Event binary layout changed");

using ReadCallback = void (*)(const Event& event, void* user);

void begin();
bool append(Type type, Source source, uint8_t road, uint8_t code, int32_t value1, int32_t value2, const char* text);
bool clear();
uint16_t count();
uint16_t capacity();
bool readLatest(uint16_t offset, uint16_t limit, ReadCallback callback, void* user);

const char* typeName(Type type);
const char* sourceName(Source source);

}
