import pickle
import os

class DiskManager:
    def __init__(self, filename):
        self.filename = filename
        if not os.path.exists(filename):
            with open(filename, "wb") as f:
                pickle.dump({}, f)

    def read_all(self):
        with open(self.filename, "rb") as f:
            return pickle.load(f)

    def write_all(self, data):
        with open(self.filename, "wb") as f:
            pickle.dump(data, f)
