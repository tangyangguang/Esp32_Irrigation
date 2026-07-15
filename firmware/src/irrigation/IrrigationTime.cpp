#include "IrrigationTime.h"

#include <climits>
#include <cstring>

namespace IrrigationTime {

bool parseLocalDateTimeUtc8(const char* text, uint32_t& epochSec) {
    if (!text || std::strlen(text) != 16 || text[4] != '-' || text[7] != '-' ||
        text[10] != 'T' || text[13] != ':') {
        return false;
    }
    const uint8_t digitIndexes[] = {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15};
    for (const uint8_t index : digitIndexes) {
        if (text[index] < '0' || text[index] > '9') return false;
    }
    const int year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                     (text[2] - '0') * 10 + text[3] - '0';
    const unsigned month = static_cast<unsigned>((text[5] - '0') * 10 + text[6] - '0');
    const unsigned day = static_cast<unsigned>((text[8] - '0') * 10 + text[9] - '0');
    const unsigned hour = static_cast<unsigned>((text[11] - '0') * 10 + text[12] - '0');
    const unsigned minute = static_cast<unsigned>((text[14] - '0') * 10 + text[15] - '0');
    if (year < 2026 || year > 2099 || month < 1 || month > 12 || hour > 23 || minute > 59) {
        return false;
    }
    static constexpr uint8_t daysPerMonth[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const unsigned maximumDay = month == 2 && leap ? 29U : daysPerMonth[month - 1U];
    if (day < 1 || day > maximumDay) return false;

    const int adjustedYear = year - (month <= 2 ? 1 : 0);
    const int era = adjustedYear / 400;
    const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3U : month + 9U;
    const unsigned dayOfYear = (153U * adjustedMonth + 2U) / 5U + day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U -
                              yearOfEra / 100U + dayOfYear;
    const int64_t daysSinceEpoch = static_cast<int64_t>(era) * 146097LL + dayOfEra - 719468LL;
    const int64_t localEpoch = daysSinceEpoch * 86400LL + hour * 3600LL + minute * 60LL;
    const int64_t utcEpoch = localEpoch - 8LL * 3600LL;
    if (utcEpoch <= 0 || utcEpoch > UINT32_MAX) return false;
    epochSec = static_cast<uint32_t>(utcEpoch);
    return true;
}

}  // namespace IrrigationTime
