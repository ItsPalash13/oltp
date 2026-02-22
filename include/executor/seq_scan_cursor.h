#ifndef EXECUTOR_SEQ_SCAN_CURSOR_H
#define EXECUTOR_SEQ_SCAN_CURSOR_H

#include "executor/scan_position.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

class BufferPoolManager;
class StorageManager;

/**
 * Stateless: return next row and updated position from (page_id, slot_index).
 * Used by SeqScanExecutor which holds the position. nullopt when no more rows.
 */
std::optional<std::pair<std::vector<uint8_t>, ScanPosition>> next_row_from(
    BufferPoolManager& bp, StorageManager& storage, uint32_t table_id,
    uint32_t page_id, uint16_t slot_index);

#endif // EXECUTOR_SEQ_SCAN_CURSOR_H
