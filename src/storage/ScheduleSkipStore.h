#pragma once

#include "domain/ZoneTypes.h"

namespace ScheduleSkipStore {

static constexpr uint8_t Capacity = 128;

struct SkipEntry {
    uint32_t planId;
    uint32_t ymd;
    Irrigation::SkipReason reason;
    uint8_t reserved[3];
};

void begin();
bool clear();
bool isSkipped(uint32_t planId, uint32_t ymd);
bool skip(uint32_t planId, uint32_t ymd, Irrigation::SkipReason reason);
bool unskip(uint32_t planId, uint32_t ymd);
bool read(uint8_t offset, uint8_t limit, SkipEntry* out, uint8_t* outCount);
const char* reasonName(Irrigation::SkipReason reason);

}
