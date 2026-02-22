#include "executor/seq_scan_cursor.h"
#include "storage/bufferpool_manager.h"
#include "storage/storage_manager.h"
#include "storage/page/record_layout.h"
#include "storage/page/page.h"
#include <stdexcept>
#include <cstring>
#include <utility>

std::optional<std::pair<std::vector<uint8_t>, ScanPosition>> next_row_from(
    BufferPoolManager& bp, StorageManager& storage, uint32_t table_id,
    uint32_t page_id, uint16_t slot_index) {
    uint32_t page_count = storage.get_page_count();
    while (page_id < page_count) {
        uint8_t* page = bp.fetch(table_id, page_id);
        const PageHeader* h = reinterpret_cast<const PageHeader*>(page);
        if (h->kind != static_cast<uint16_t>(PageKind::PAGE_INDEX) ||
            h->level != static_cast<uint16_t>(PageLevel::PAGE_LEAF)) {
            bp.unpin(table_id, page_id);
            page_id++;
            slot_index = 0;
            continue;
        }
        if (slot_index >= h->cell_count) {
            bp.unpin(table_id, page_id);
            page_id++;
            slot_index = 0;
            continue;
        }
        while (slot_index < h->cell_count && is_record_deleted(page, slot_index))
            slot_index++;
        if (slot_index >= h->cell_count) {
            bp.unpin(table_id, page_id);
            page_id++;
            slot_index = 0;
            continue;
        }
        uint16_t row_len;
        const uint8_t* row_ptr = get_row_at_slot(page, slot_index, &row_len);
        // Strip row_id from row_bytes before returning (row_id is first 8 bytes)
        if (row_len < 8) {
            bp.unpin(table_id, page_id);
            throw std::runtime_error("Row data too small to contain row_id");
        }
        std::vector<uint8_t> row(row_ptr + 8, row_ptr + row_len);
        ScanPosition next_pos;
        next_pos.page_id = page_id;
        next_pos.slot_index = slot_index + 1;
        bp.unpin(table_id, page_id);
        return std::make_pair(std::move(row), next_pos);
    }
    return std::nullopt;
}
