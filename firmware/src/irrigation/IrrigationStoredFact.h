#pragma once
#include <RecordStream.h>
#include <runtime/Esp32BaseRecordStore.h>
namespace irrigation_fact {
inline void putDuration(uint8_t* bytes, uint32_t duration) {
    for (unsigned n=0;n<4;++n) bytes[n]=uint8_t(duration>>(n*8));
}
inline bool decode(const uint8_t* bytes, size_t length, size_t expected,
                   Esp32BaseRecordStore::RecordTiming& timing, iot_device::RecordFactView& fact) {
    if (!iot_device::linearRecordEncoding().decode(bytes,length,fact) || fact.dataBytes!=expected ||
        fact.dataBytes<4 || !fact.sequence || fact.sequence>iot_device::RecordStream::MaxSequence) return false;
    timing={};
    if (fact.observedAtMs!=iot_device::RecordStream::UnknownTime) {
        if (fact.observedAtMs%1000 || fact.observedAtMs/1000>UINT32_MAX) return false;
        timing.completedEpochSec=uint32_t(fact.observedAtMs/1000);
    }
    for (unsigned n=0;n<4;++n) timing.durationSec|=uint32_t(fact.data[n])<<(n*8);
    return true;
}
} // namespace irrigation_fact
