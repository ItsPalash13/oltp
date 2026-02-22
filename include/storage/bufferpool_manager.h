#ifndef BUFFERPOOL_MANAGER_H
#define BUFFERPOOL_MANAGER_H

#include "storage/page/page.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <stdexcept>

class Storage;

/**
 * BufferPoolManager - shared page cache for pages with page_id >= 2 only.
 * Catalog pool owns pages 0 and 1; buffer pool must not cache them.
 * Frames are keyed by (table_id, page_id). Fixed number of frames, LRU eviction, pin/unpin.
 */
class BufferPoolManager {
public:
    static constexpr uint32_t INVALID_PAGE_ID = 0xFFFFFFFFu;
    static constexpr uint32_t INVALID_TABLE_ID = 0u;
    static constexpr size_t DEFAULT_NUM_FRAMES = 10;

    explicit BufferPoolManager(Storage* storage, size_t num_frames = DEFAULT_NUM_FRAMES);
    ~BufferPoolManager();

    // Fetch page into buffer pool; pin it. Returns pointer to page data (valid until unpin).
    // Only call for page_id >= 2.
    uint8_t* fetch(uint32_t table_id, uint32_t page_id);

    // Unpin page (decrement pin count).
    void unpin(uint32_t table_id, uint32_t page_id);

    // Mark page as dirty so it is written back on eviction.
    void mark_dirty(uint32_t table_id, uint32_t page_id);

    // Flush all dirty pages to disk (e.g. on close).
    void flush_all();

private:
    struct Frame {
        uint32_t table_id;
        uint32_t page_id;
        int pin_count;
        bool dirty;
        uint64_t last_used;
        uint8_t data[PAGE_SIZE];
    };

    Storage* storage_;
    std::vector<Frame> frames_;
    uint64_t access_counter_;

    Frame* find_frame(uint32_t table_id, uint32_t page_id);
    Frame* evict_and_use();
    void flush_frame(Frame* f);
};

#endif // BUFFERPOOL_MANAGER_H
