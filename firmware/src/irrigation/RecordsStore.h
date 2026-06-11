#pragma once

namespace irrigation {

class RecordsStore {
public:
    // Boundary for future fixed-size watering records. Runtime decides when a
    // record exists; this store will only append/read/export those records.
    bool begin();
    void handle();

    bool ready() const;

private:
    bool _ready = false;
};

}  // namespace irrigation
