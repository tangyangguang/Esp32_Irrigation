#include "IrrigationAuditPayload.h"

#include <cstdint>

namespace {
constexpr uint32_t kMagic = 0x31545541UL;  // AUT1
constexpr uint8_t kVersion = 2;

void put32(uint8_t*& cursor, uint32_t value) {
    for (uint8_t shift = 0; shift < 32; shift += 8)
        *cursor++ = static_cast<uint8_t>(value >> shift);
}
uint32_t get32(const uint8_t*& cursor) {
    const uint32_t value = static_cast<uint32_t>(cursor[0]) |
                           static_cast<uint32_t>(cursor[1]) << 8U |
                           static_cast<uint32_t>(cursor[2]) << 16U |
                           static_cast<uint32_t>(cursor[3]) << 24U;
    cursor += 4;
    return value;
}
bool validKind(IrrigationAuditPayload::Kind kind) {
    return kind == IrrigationAuditPayload::Kind::PlanSkipped ||
           kind == IrrigationAuditPayload::Kind::AutomaticStateChanged ||
           kind == IrrigationAuditPayload::Kind::PlansChanged ||
           kind == IrrigationAuditPayload::Kind::ZoneBaselineSaved ||
           kind == IrrigationAuditPayload::Kind::ClosedFlowChanged ||
           kind == IrrigationAuditPayload::Kind::ZoneChanged ||
           kind == IrrigationAuditPayload::Kind::SystemFieldChanged;
}
}  // namespace

bool IrrigationAuditCodec::encode(const IrrigationAuditPayload& payload,
                                  uint8_t* output,
                                  std::size_t outputSize) {
    if (!output || outputSize != kPayloadSize || !validKind(payload.kind))
        return false;
    uint8_t* cursor = output;
    put32(cursor, kMagic);
    *cursor++ = kVersion;
    *cursor++ = static_cast<uint8_t>(payload.kind);
    *cursor++ = payload.reason;
    *cursor++ = payload.flags;
    *cursor++ = payload.objectId;
    *cursor++ = 0;
    *cursor++ = 0;
    *cursor++ = 0;
    put32(cursor, payload.value1);
    put32(cursor, payload.value2);
    return cursor == output + outputSize;
}

bool IrrigationAuditCodec::decode(const uint8_t* data,
                                  std::size_t dataSize,
                                  IrrigationAuditPayload& payload) {
    payload = {};
    if (!data || dataSize != kPayloadSize) return false;
    const uint8_t* cursor = data;
    if (get32(cursor) != kMagic || *cursor++ != kVersion) return false;
    payload.kind = static_cast<IrrigationAuditPayload::Kind>(*cursor++);
    payload.reason = *cursor++;
    payload.flags = *cursor++;
    payload.objectId = *cursor++;
    if (*cursor++ != 0U || *cursor++ != 0U || *cursor++ != 0U) return false;
    payload.value1 = get32(cursor);
    payload.value2 = get32(cursor);
    return cursor == data + dataSize && validKind(payload.kind);
}
