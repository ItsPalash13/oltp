from bplustree import BPlusTree
from transaction import Transaction
import os

# Check if database exists
db_exists = os.path.exists("tree.db")

if not db_exists:
    print("Creating new database and inserting keys...")
    print("=" * 60)
    tree = BPlusTree("tree.db")
    txn = Transaction("T1")
    
    print("\nInitial buffer pool state:")
    print(tree.bp.get_state())
    print("\n----------------insert----------------")
    
    keys_to_insert = [10, 20, 5, 6, 12, 30, 7, 17]
    prev_frames = set()
    for i, key in enumerate(keys_to_insert, 1):
        print(f"\nInsert {i}: Inserting key {key}")
        prev_frames = set(tree.bp.frames.keys())
        tree.insert(key, txn)
        curr_frames = set(tree.bp.frames.keys())
        evicted = prev_frames - curr_frames
        loaded = curr_frames - prev_frames
        
        if evicted:
            for pid in evicted:
                print(f"  >> Evicted page {pid} (written to disk)")
        if loaded:
            for pid in loaded:
                print(f"  >> Loaded page {pid} from disk")
        
        print(f"After insert {key}:")
        print(tree.bp.get_state())
    
    # Flush changes to disk for persistence
    print("\n----------------flush----------------")
    print("Flushing all changes to disk...")
    tree.close()
    print("Keys inserted and flushed to disk.\n")
else:
    print("Database already exists - skipping insert.\n")

# Test persistence - create a new tree instance and read data
print("Loading database and searching for keys >= 6:")
tree2 = BPlusTree("tree.db")
txn2 = Transaction("T2")

print("\nInitial buffer pool state:")
print(tree2.bp.get_state())
print("----------------search----------------")

cursor = tree2.search(6, txn2)
if cursor:
    print("\nInitial buffer pool state:")
    print(tree2.bp.get_state())
    
    iteration = 1
    while True:
        r = cursor.next()
        if not r:
            break
        print(f"Iteration {iteration}: Found {r}")
        print(tree2.bp.get_state())
        print()
        iteration += 1
else:
    print("No cursor returned (key not found)")

tree2.close()
