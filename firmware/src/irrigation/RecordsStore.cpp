#include "RecordsStore.h"

namespace irrigation {

bool RecordsStore::begin() {
    _ready = true;
    return _ready;
}

void RecordsStore::handle() {
}

bool RecordsStore::ready() const {
    return _ready;
}

}  // namespace irrigation
