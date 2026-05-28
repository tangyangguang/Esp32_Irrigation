#pragma once

#include <stdint.h>

namespace PlanResultStore {

static constexpr uint8_t Capacity = 96;

enum Result : uint8_t {
    RESULT_NONE = 0,
    RESULT_STARTED = 1,
    RESULT_SKIPPED_MANUAL = 2,
    RESULT_SKIPPED_ROAD_DISABLED = 3,
    RESULT_SKIPPED_ROAD_BUSY = 4,
    RESULT_REJECTED = 5,
    RESULT_CONFIG_INVALID = 6,
    RESULT_FACTORY_RESET_PENDING = 7,
    RESULT_LEAK_ALERT = 8,
};

void begin();
bool getResult(uint8_t planIndex, uint32_t ymd, Result* result);
bool setResult(uint8_t planIndex, uint32_t ymd, Result result);
bool clearResult(uint8_t planIndex, uint32_t ymd);
bool clear();

const char* resultName(Result result);
const char* resultLabel(Result result);

}
