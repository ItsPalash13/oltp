class Page:
    def __init__(self, page_id, is_leaf):
        self.page_id = page_id
        self.is_leaf = is_leaf
        self.keys = []
        self.children = []          # internal nodes
        self.values = []            # leaf nodes (list per key)
        self.next = None
        self.prev = None
