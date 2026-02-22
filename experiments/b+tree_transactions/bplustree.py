from config import MAX_KEYS, MIN_KEYS
from page import Page
from cursor import IndexCursor

class BPlusTree:
    def __init__(self, filename):
        from disk_manager import DiskManager
        from buffer_pool import BufferPool

        self.disk = DiskManager(filename)
        self.bp = BufferPool(self.disk)
        
        # Check if database exists by reading from disk
        all_pages = self.disk.read_all()
        if not all_pages:
            root = Page(0, True)
            self.root = 0
            # Write root to disk immediately
            self.disk.write_all({0: root})
        else:
            self.root = 0

    def _new_page(self, is_leaf):
        # Read from disk to find max page ID
        all_pages = self.disk.read_all()
        pid = (max(all_pages.keys()) + 1) if all_pages else 0
        new_page = Page(pid, is_leaf)
        # Add to disk (so it exists for future fetches)
        all_pages[pid] = new_page
        self.disk.write_all(all_pages)
        return pid

    def search(self, key, txn):
        pid = self.root
        while True:
            page = self.bp.fetch(pid, txn)
            if page.is_leaf:
                for i, k in enumerate(page.keys):
                    if k >= key:
                        self.bp.unpin(pid)
                        return IndexCursor(self, pid, i, txn)
                self.bp.unpin(pid)
                return None

            for i, k in enumerate(page.keys):
                if key < k:
                    next_pid = page.children[i]
                    break
            else:
                next_pid = page.children[-1]

            self.bp.unpin(pid)
            pid = next_pid

    def insert(self, key, txn):
        self._insert(self.root, key, txn)

    def _insert(self, pid, key, txn):
        page = self.bp.fetch(pid, txn)

        if page.is_leaf:
            if key in page.keys:
                idx = page.keys.index(key)
                page.values[idx].append(key)
            else:
                idx = 0
                while idx < len(page.keys) and page.keys[idx] < key:
                    idx += 1
                page.keys.insert(idx, key)
                page.values.insert(idx, [key])

            self.bp.mark_dirty(pid)

            if len(page.keys) > MAX_KEYS:
                self._split_leaf(pid, txn)

            self.bp.unpin(pid)
            return

        for i, k in enumerate(page.keys):
            if key < k:
                child = page.children[i]
                break
        else:
            child = page.children[-1]

        self.bp.unpin(pid)
        self._insert(child, key, txn)

    def _split_leaf(self, pid, txn):
        page = self.bp.fetch(pid, txn)
        mid = len(page.keys) // 2

        new_pid = self._new_page(True)
        # Fetch the new page through buffer pool to get it in memory
        new_page = self.bp.fetch(new_pid, txn)

        new_page.keys = page.keys[mid:]
        new_page.values = page.values[mid:]
        page.keys = page.keys[:mid]
        page.values = page.values[:mid]

        new_page.next = page.next
        page.next = new_pid
        new_page.prev = pid

        self.bp.mark_dirty(pid)
        self.bp.mark_dirty(new_pid)
        self.bp.unpin(pid)
        self.bp.unpin(new_pid)

        self._insert_into_parent(pid, new_page.keys[0], new_pid, txn)

    def _insert_into_parent(self, left, key, right, txn):
        if left == self.root:
            new_root = self._new_page(False)
            # Fetch the new root through buffer pool
            root = self.bp.fetch(new_root, txn)
            root.keys = [key]
            root.children = [left, right]
            self.bp.mark_dirty(new_root)
            self.bp.unpin(new_root)
            self.root = new_root
            return

        parent = self._find_parent(self.root, left, txn)
        p = self.bp.fetch(parent, txn)

        idx = p.children.index(left)
        p.keys.insert(idx, key)
        p.children.insert(idx + 1, right)

        self.bp.mark_dirty(parent)

        if len(p.keys) > MAX_KEYS:
            self._split_internal(parent, txn)

        self.bp.unpin(parent)

    def _split_internal(self, pid, txn):
        page = self.bp.fetch(pid, txn)
        mid = len(page.keys) // 2

        promote = page.keys[mid]

        new_pid = self._new_page(False)
        # Fetch the new page through buffer pool
        new_page = self.bp.fetch(new_pid, txn)

        new_page.keys = page.keys[mid+1:]
        new_page.children = page.children[mid+1:]

        page.keys = page.keys[:mid]
        page.children = page.children[:mid+1]

        self.bp.mark_dirty(pid)
        self.bp.mark_dirty(new_pid)
        self.bp.unpin(pid)
        self.bp.unpin(new_pid)

        self._insert_into_parent(pid, promote, new_pid, txn)

    def _find_parent(self, current, target, txn):
        page = self.bp.fetch(current, txn)
        if page.is_leaf:
            self.bp.unpin(current)
            return None

        if target in page.children:
            self.bp.unpin(current)
            return current

        for child in page.children:
            res = self._find_parent(child, target, txn)
            if res is not None:
                self.bp.unpin(current)
                return res

        self.bp.unpin(current)
        return None

    def close(self):
        """Flush all changes to disk."""
        # Flush buffer pool to disk (this writes all dirty pages)
        self.bp.flush_all()
