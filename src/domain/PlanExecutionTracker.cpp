#include "domain/PlanExecutionTracker.h"

#include <Esp32Base.h>
#include <stdio.h>

namespace {

static constexpr const char* kNamespace = "irr_plan_exec";
static constexpr uint32_t kMagic = 0x49504558UL;
static constexpr uint16_t kVersion = 1;

struct StoredTracker {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t zoneId;
    uint8_t count;
    uint16_t reserved;
    uint32_t currentYmd;
    PlanExecutionTracker::Entry entries[Irrigation::MaxPlansPerZone];
};

void keyForZone(uint8_t zoneId, char* out, size_t len) {
    snprintf(out, len, "z%u", static_cast<unsigned>(zoneId));
}

bool validStored(const StoredTracker& stored, uint8_t zoneId) {
    if (stored.magic != kMagic ||
        stored.version != kVersion ||
        stored.size != sizeof(stored) ||
        stored.zoneId != zoneId ||
        stored.count > Irrigation::MaxPlansPerZone) {
        return false;
    }
    for (uint8_t i = 0; i < stored.count; ++i) {
        if (stored.entries[i].planId == 0 || stored.entries[i].ymd == 0 || stored.entries[i].minuteOfDay >= 1440) {
            return false;
        }
    }
    return true;
}

}

void PlanExecutionTracker::begin(uint8_t zoneId) {
    m_zoneId = zoneId;
    reset();
    if (Irrigation::validZoneId(m_zoneId)) {
        (void)load();
    }
}

void PlanExecutionTracker::reset() {
    for (uint8_t i = 0; i < Irrigation::MaxPlansPerZone; ++i) {
        m_entries[i] = {};
    }
    m_count = 0;
    m_currentYmd = 0;
}

bool PlanExecutionTracker::resetNewDay(uint32_t ymd) {
    if (m_currentYmd == ymd) {
        return true;
    }
    reset();
    m_currentYmd = ymd;
    return save();
}

bool PlanExecutionTracker::isHandled(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay) const {
    for (uint8_t i = 0; i < m_count; ++i) {
        if (m_entries[i].planId == planId && m_entries[i].ymd == ymd && m_entries[i].minuteOfDay == minuteOfDay) {
            return true;
        }
    }
    return false;
}

bool PlanExecutionTracker::mark(uint32_t planId, uint32_t ymd, uint16_t minuteOfDay, Irrigation::PlanObservationStatus status) {
    for (uint8_t i = 0; i < m_count; ++i) {
        if (m_entries[i].planId == planId && m_entries[i].ymd == ymd && m_entries[i].minuteOfDay == minuteOfDay) {
            m_entries[i].status = status;
            return save();
        }
    }
    if (m_count >= Irrigation::MaxPlansPerZone) {
        return false;
    }
    m_entries[m_count++] = {planId, ymd, minuteOfDay, status};
    return save();
}

uint8_t PlanExecutionTracker::count() const {
    return m_count;
}

const PlanExecutionTracker::Entry& PlanExecutionTracker::get(uint8_t index) const {
    static Entry invalid = {};
    return index < m_count ? m_entries[index] : invalid;
}

bool PlanExecutionTracker::clearPersistent() {
    return Esp32BaseConfig::clearNamespace(kNamespace);
}

bool PlanExecutionTracker::load() {
    char key[8];
    keyForZone(m_zoneId, key, sizeof(key));
    StoredTracker stored = {};
    if (!Esp32BaseConfig::getPod(kNamespace, key, stored) || !validStored(stored, m_zoneId)) {
        return false;
    }
    m_currentYmd = stored.currentYmd;
    m_count = stored.count;
    for (uint8_t i = 0; i < Irrigation::MaxPlansPerZone; ++i) {
        m_entries[i] = i < m_count ? stored.entries[i] : Entry{};
    }
    return true;
}

bool PlanExecutionTracker::save() const {
    if (!Irrigation::validZoneId(m_zoneId)) {
        return false;
    }
    char key[8];
    keyForZone(m_zoneId, key, sizeof(key));
    StoredTracker stored = {};
    stored.magic = kMagic;
    stored.version = kVersion;
    stored.size = sizeof(stored);
    stored.zoneId = m_zoneId;
    stored.count = m_count;
    stored.currentYmd = m_currentYmd;
    for (uint8_t i = 0; i < m_count && i < Irrigation::MaxPlansPerZone; ++i) {
        stored.entries[i] = m_entries[i];
    }
    return Esp32BaseConfig::setPod(kNamespace, key, stored);
}
