class IndexCursor:
    def __init__(self, tree, page_id, index, txn):
        self.tree = tree
        self.page_id = page_id
        self.index = index
        self.txn = txn

    def next(self):
        page = self.tree.bp.fetch(self.page_id, self.txn)

        if self.index >= len(page.keys):
            if page.next is None:
                return None
            self.tree.bp.unpin(self.page_id)
            self.page_id = page.next
            self.index = 0
            page = self.tree.bp.fetch(self.page_id, self.txn)

        key = page.keys[self.index]
        values = page.values[self.index]
        self.index += 1

        self.tree.bp.unpin(self.page_id)
        return key, values
