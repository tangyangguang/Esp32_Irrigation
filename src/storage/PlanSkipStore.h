#pragma once

#include <stdint.h>

namespace PlanSkipStore {

static constexpr uint8_t Capacity = 32;

void begin();
bool isSkipped(uint8_t planIndex, uint32_t ymd);
bool setSkipped(uint8_t planIndex, uint32_t ymd);
bool clearSkipped(uint8_t planIndex, uint32_t ymd);
bool clear();

}
