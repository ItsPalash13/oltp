#ifndef SCAN_POSITION_H
#define SCAN_POSITION_H

#include <cstdint>

/**
 * Position for stateless "next from position" scan API.
 * Executor holds this and passes it to storage; storage returns updated position with each row.
 */
struct ScanPosition {
    uint32_t page_id = 0;
    uint16_t slot_index = 0;
};

#endif // SCAN_POSITION_H
