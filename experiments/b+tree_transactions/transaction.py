class Transaction:
    def __init__(self, txn_id: str):
        self.txn_id = txn_id

    def __repr__(self):
        return f"Txn({self.txn_id})"
