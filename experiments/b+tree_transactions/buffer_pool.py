from collections import OrderedDict

class BufferFrame:
    def __init__(self, page):
        self.page = page
        self.dirty = False
        self.pin = 0

class BufferPool:
    def __init__(self, disk_manager, capacity=2):
        self.disk = disk_manager
        self.capacity = capacity
        self.frames = OrderedDict()

    def fetch(self, page_id, txn):
        if page_id in self.frames:
            frame = self.frames.pop(page_id)
            self.frames[page_id] = frame
            frame.pin += 1
            return frame.page

        if len(self.frames) >= self.capacity:
            self.evict()

        # Read page directly from disk
        all_pages = self.disk.read_all()
        if page_id not in all_pages:
            raise KeyError(f"Page {page_id} not found on disk")
        
        page = all_pages[page_id]
        frame = BufferFrame(page)
        frame.pin = 1
        self.frames[page_id] = frame
        return page

    def mark_dirty(self, page_id):
        self.frames[page_id].dirty = True

    def unpin(self, page_id):
        self.frames[page_id].pin -= 1

    def evict(self):
        for pid, frame in self.frames.items():
            if frame.pin == 0:
                if frame.dirty:
                    # Write this specific page to disk
                    all_pages = self.disk.read_all()
                    all_pages[pid] = frame.page
                    self.disk.write_all(all_pages)
                    # Non-dirty pages are just removed (no write needed)
                self.frames.pop(pid)
                return
        raise RuntimeError("All pages are pinned")
    
    def evict_with_log(self):
        """Evict a page and return info about what was evicted."""
        for pid, frame in self.frames.items():
            if frame.pin == 0:
                was_dirty = frame.dirty
                if frame.dirty:
                    # Write this specific page to disk
                    all_pages = self.disk.read_all()
                    all_pages[pid] = frame.page
                    self.disk.write_all(all_pages)
                self.frames.pop(pid)
                return pid, was_dirty
        raise RuntimeError("All pages are pinned")

    def flush_all(self):
        # Write all dirty pages to disk
        all_pages = self.disk.read_all()
        for pid, frame in self.frames.items():
            if frame.dirty:
                all_pages[pid] = frame.page
        self.disk.write_all(all_pages)

    def get_state(self):
        """Return a string representation of the current buffer pool state."""
        state_lines = []
        state_lines.append(f"Buffer Pool State (capacity: {self.capacity}, frames: {len(self.frames)})")
        state_lines.append("-" * 60)
        
        if not self.frames:
            state_lines.append("  No pages in buffer pool")
        else:
            for pid, frame in self.frames.items():
                page = frame.page
                leaf_info = "leaf" if page.is_leaf else "internal"
                keys_info = f"keys={page.keys[:5]}"  # Show first 5 keys
                if len(page.keys) > 5:
                    keys_info += "..."
                pin_info = f"pin={frame.pin}"
                dirty_info = "DIRTY" if frame.dirty else "clean"
                state_lines.append(f"  Page {pid} ({leaf_info}): {keys_info}, {pin_info}, {dirty_info}")
        
        return "\n".join(state_lines)