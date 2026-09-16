#pragma once

#include <ModelPublisher.h>

// Application-owned immutable binary-fact -> platform-record codec table for
// the irrigation model. Type codes match IrrigationPlatform::FactType and are
// never reused. Each decode turns the stored business bytes into the JSON data
// document required by the corresponding controlled record schema.
namespace IrrigationPlatformRecords {

const iot_device::RecordCodec* codecs();
size_t codecCount();

}  // namespace IrrigationPlatformRecords
