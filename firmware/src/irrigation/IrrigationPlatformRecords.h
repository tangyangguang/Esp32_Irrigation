#pragma once

#include <ModelPublisher.h>

// Application-owned immutable binary-fact -> platform-record codec table for
// the irrigation model. Type codes match IrrigationPlatform::FactType and are
// never reused. Each decode turns the stored business bytes into the JSON data
// document required by the corresponding controlled record schema.
namespace IrrigationPlatformRecords {

// JSON document capacity for decoding one stored fact into its platform
// record projection. The worst case is a 6-zone watering fact with baseline
// values: about 2.35 KB of JSON, whose ArduinoJson 6.21.6 slot+string pool
// needs 4096 bytes (measured on 64-bit host; 32-bit device slots are smaller).
// Any smaller pool overflows on multi-zone facts and ModelPublisher silently
// drops them on every 5 s retry while RecordStream's strict ordering blocks
// every later record behind the undeliverable one.
constexpr size_t kRecordDocBytes = 4096;

const iot_device::RecordCodec* codecs();
size_t codecCount();

}  // namespace IrrigationPlatformRecords
