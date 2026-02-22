#include "storage/bufferpool_manager.h"
#include "executor/storage.h"
#include <algorithm>
#include <stdexcept>
#include <iostream>

BufferPoolManager::BufferPoolManager(Storage* storage, size_t num_frames)
    : storage_(storage), access_counter_(0) {
    if (!storage_) {
        throw std::runtime_error("BufferPoolManager: Storage is null");
    }
    frames_.resize(num_frames);
    for (auto& f : frames_) {
        f.table_id = INVALID_TABLE_ID;
        f.page_id = INVALID_PAGE_ID;
        f.pin_count = 0;
        f.dirty = false;
        f.last_used = 0;
    }
}

BufferPoolManager::~BufferPoolManager() {
    flush_all();
}

uint8_t* BufferPoolManager::fetch(uint32_t table_id, uint32_t page_id) {
    if (page_id < 2) {
        throw std::runtime_error("BufferPoolManager: must not cache page_id 0 or 1");
    }
    access_counter_++;

    Frame* frame = find_frame(table_id, page_id);
    if (frame) {
        std::cout << "bufferpool hit  tid=" << table_id << " page=" << page_id << "\n";
        frame->pin_count++;
        frame->last_used = access_counter_;
        return frame->data;
    }

    std::cout << "bufferpool miss tid=" << table_id << " page=" << page_id << "\n";
    frame = evict_and_use();
    StorageManager& sm = storage_->get_storage_manager_by_id(table_id);
    sm.read_page(page_id, frame->data);
    frame->table_id = table_id;
    frame->page_id = page_id;
    frame->pin_count = 1;
    frame->dirty = false;
    frame->last_used = access_counter_;
    return frame->data;
}

void BufferPoolManager::unpin(uint32_t table_id, uint32_t page_id) {
    Frame* frame = find_frame(table_id, page_id);
    if (!frame) {
        throw std::runtime_error("BufferPoolManager::unpin: page " + std::to_string(page_id) + " not in pool");
    }
    if (frame->pin_count <= 0) {
        throw std::runtime_error("BufferPoolManager::unpin: pin count already 0");
    }
    frame->pin_count--;
}

void BufferPoolManager::mark_dirty(uint32_t table_id, uint32_t page_id) {
    Frame* frame = find_frame(table_id, page_id);
    if (!frame) {
        throw std::runtime_error("BufferPoolManager::mark_dirty: page " + std::to_string(page_id) + " not in pool");
    }
    frame->dirty = true;
}

void BufferPoolManager::flush_all() {
    for (auto& f : frames_) {
        if (f.table_id != INVALID_TABLE_ID && f.page_id != INVALID_PAGE_ID && f.dirty) {
            flush_frame(&f);
        }
    }
}

BufferPoolManager::Frame* BufferPoolManager::find_frame(uint32_t table_id, uint32_t page_id) {
    for (auto& f : frames_) {
        if (f.table_id == table_id && f.page_id == page_id) {
            return &f;
        }
    }
    return nullptr;
}

BufferPoolManager::Frame* BufferPoolManager::evict_and_use() {
    // Look for free frame first
    for (auto& f : frames_) {
        if (f.table_id == INVALID_TABLE_ID) {
            return &f;
        }
    }
    // LRU eviction: pick unpinned frame with smallest last_used
    Frame* victim = nullptr;
    uint64_t min_used = UINT64_MAX;
    for (auto& f : frames_) {
        if (f.pin_count == 0 && f.last_used < min_used) {
            min_used = f.last_used;
            victim = &f;
        }
    }
    if (!victim) {
        throw std::runtime_error("BufferPoolManager: all frames pinned, cannot evict");
    }
    if (victim->dirty) {
        flush_frame(victim);
    }
    victim->table_id = INVALID_TABLE_ID;
    victim->page_id = INVALID_PAGE_ID;
    victim->pin_count = 0;
    victim->dirty = false;
    return victim;
}

void BufferPoolManager::flush_frame(Frame* f) {
    if (f->table_id == INVALID_TABLE_ID || f->page_id == INVALID_PAGE_ID || !f->dirty) return;
    StorageManager& sm = storage_->get_storage_manager_by_id(f->table_id);
    sm.write_page(f->page_id, f->data);
    f->dirty = false;
}
