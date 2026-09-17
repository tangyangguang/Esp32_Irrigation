#pragma once

#include <cstddef>
#include <cstdint>

#include <ArduinoJson.h>
#include <CommandInbox.h>
#include <ConnectionSession.h>
#include <ModelPublisher.h>
#include <RecordStream.h>

class WateringRecordStore;
class IrrigationAuditStore;

// Durable fact type codes shared by the local stores and the platform
// adapter. They are the application-owned immutable mapping to the controlled
// recordKey directory and are never reused.
namespace IrrigationPlatform {
enum FactType : uint16_t {
    // watering stream business records (business payload: 246B codec payload)
    FactWateringCompleted = 1,
    FactWateringStopped = 2,
    FactWateringFailed = 3,
    // audit stream records (business payload: 20B audit codec payload)
    // Type code 10 is permanently reserved: the removed automatic plan-run
    // operation record is now expressed as a watering.failed start rejection.
    FactAutomaticPaused = 11,
    FactAutomaticResumed = 12,
    FactPlansChanged = 13,
    FactZoneBaselineSaved = 14,
    FactZoneChanged = 15,
    FactSystemFieldChanged = 16,
};
}

// Peripheral MQTT platform adapter. It only connects, projects the existing
// local facts/states to the platform and routes platform intents to the single
// IrrigationApp business entry. It never drives outputs or keeps a second
// business state machine. Missing MQTT provisioning leaves local operation
// fully available without MQTT.
namespace IrrigationPlatform {

bool configured();

// Call before Esp32Base::begin(). Returns false and leaves the adapter idle
// when MQTT provisioning is absent or rejected. Local operation is unaffected.
bool configure();

// Bind the two local durable stores. Each owns its SDK RecordStream; the
// platform layer drives their publish/ACK through the connected session.
void bindStores(WateringRecordStore& watering, IrrigationAuditStore& audit);

// Per-loop drive: session, command inbox, evidence, state projection and the
// two durable record streams (the stores own their SDK RecordStream).
void begin();
void poll();

}  // namespace IrrigationPlatform
