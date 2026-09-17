#pragma once

#include <cstddef>
#include <cstdint>

// Local audit event payload plus its fixed 20-byte codec. Kept independent of
// Esp32Base so native tests and the platform record projection can use it
// without the hardware store.
struct IrrigationAuditPayload {
    enum class Kind : uint8_t {
        AutomaticStateChanged = 2,
        PlansChanged = 3,
        ZoneBaselineSaved = 5,
        ClosedFlowChanged = 6,
        ZoneChanged = 7,
        SystemFieldChanged = 8,
    };

    Kind kind = Kind::AutomaticStateChanged;
    uint8_t reason = 0;
    uint8_t flags = 0;
    uint8_t objectId = 0;
    uint32_t value1 = 0;
    uint32_t value2 = 0;
};

struct StoredIrrigationAuditRecord;

class IrrigationAuditCodec {
public:
    static constexpr std::size_t kPayloadSize = 20;
    static bool encode(const IrrigationAuditPayload& payload,
                       uint8_t* output,
                       std::size_t outputSize);
    static bool decode(const uint8_t* data,
                       std::size_t dataSize,
                       IrrigationAuditPayload& payload);
};
