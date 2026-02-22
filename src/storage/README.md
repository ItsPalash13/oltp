# Storage Module: Design Choices and Future Scopes

This document explains the design choices and future scopes for the storage module, including the database manager and catalog manager.

## Architecture Overview

The storage module consists of:

- **DatabaseManager** (`db_manager.h/cpp`): Database-level operations (create_db, drop_db, use_db)
- **CatalogManager** (`catalog_manager.h/cpp`): In-memory cache for table meta pages (page 0)
- **StorageManager** (`storage_manager.h/cpp`): Low-level table file operations (.ibd files)

### File Layout

```
@data/                    # Root data folder (project root)
├── mydb/                 # Database directory
│   ├── users.ibd        # Table file
│   └── products.ibd     # Table file
└── otherdb/              # Another database
    └── items.ibd
```

## Design Choices

### 1. Page Size (8KB)

**Choice:** Global `PAGE_SIZE = 8192` (8KB) for all pages (meta and data).

**Rationale:**
- Consistent page size simplifies memory management and disk I/O
- 8KB is a common page size in database systems (balance between memory usage and I/O efficiency)
- All pages (meta, data, index) use the same size for alignment and compatibility

**Location:** `include/storage/page/page.h`

### 2. Root Data Folder (@data/)

**Choice:** All databases are stored under a single root directory `@data/` at the project root.

**Rationale:**
- Centralized data location makes backup and management easier
- Clear separation between code and data
- Each database is a subdirectory: `@data/<db_name>/`
- Table files are: `@data/<db_name>/<table>.ibd`

**Implementation:**
- `DatabaseManager` manages the root path (default: `"@data/"`)
- `DatabaseManager::get_current_db_path()` returns `"@data/<current_db>/"`
- `StorageManager` accepts database paths from `DatabaseManager`

### 3. Catalog Pool: Preallocated (Option 1)

**Choice:** Preallocate the entire catalog pool (3 pages × 8KB = 24KB) at construction time.

**Implementation:**
```cpp
std::array<std::array<uint8_t, PAGE_SIZE>, 3> catalog_pool;
```

**Rationale:**
- **24KB is small:** Preallocating avoids allocator overhead and fragmentation
- **No allocation overhead:** No per-load/evict allocate/free operations
- **Simpler code:** No need to check for null pointers or handle allocation failures
- **Cache-friendly:** Fixed memory layout improves CPU cache behavior
- **Empty slot handling:** When a slot is "empty", the hash index marks it invalid; the buffer is reused on the next load (no free/alloc)

**Alternative considered:** On-demand allocation (allocate when loading, free on eviction)
- **Rejected because:** 24KB is too small to justify the complexity and overhead of dynamic allocation

### 4. Hash Index (3 Slots)

**Choice:** Fixed 3-slot hash table mapping table names to catalog pool pages.

**Implementation:**
```cpp
std::array<CatalogSlot, 3> hash_index;
// Hash function: hash(table_name) % 3
```

**CatalogSlot structure:**
- `table_name`: Table identifier
- `db_path`: Database path (e.g., `"@data/mydb/"`)
- `pool_slot_index`: Which pool page (0-2) stores this table's meta page
- `is_valid`: Whether slot is in use
- `last_access_time`: For LRU eviction

**Collision handling:** Replace (evict existing table if slot is full)
- When a new table hashes to an occupied slot, the existing table is evicted (via LRU if pool is full)

**Rationale:**
- Simple and fast: O(1) lookup (with possible eviction)
- Fixed size: No dynamic allocation
- 3 slots match 3 pool pages: One-to-one mapping simplifies management

### 5. LRU Eviction

**Choice:** When the catalog pool is full and a new table needs to be loaded, evict the least recently used (LRU) slot.

**Implementation:**
- Each `CatalogSlot` tracks `last_access_time` (monotonically increasing counter)
- `evict_lru_slot()` finds the slot with the oldest `last_access_time`
- Before eviction, if the page is dirty, it is flushed to disk

**Rationale:**
- Keeps frequently accessed tables in memory
- Simple to implement with a timestamp counter
- Works well for typical database workloads (some tables accessed more than others)

### 6. Dirty Tracking and Flush

**Choice:** Track which catalog pool pages are modified (dirty) and flush them to disk on commit.

**Implementation:**
```cpp
std::array<bool, 3> dirty_flags;  // One flag per pool page
```

**Behavior:**
- Pages are marked dirty on `write_schema()` or `create_table_meta()`
- `flush()` writes all dirty pages to disk and clears dirty flags
- `clear()` flushes dirty pages before clearing the catalog pool

**Rationale:**
- Ensures data consistency: Modified pages are eventually written to disk
- Batch writes: Multiple modifications can be flushed together
- Explicit control: Caller decides when to commit changes

## API Summary

### DatabaseManager

**Location:** `include/storage/db_manager.h`

**Key methods:**
- `create_db(db_name)`: Create database directory at `@data/<db_name>/`
- `drop_db(db_name)`: Delete database directory and all contents
- `use_db(db_name)`: Set current database context, returns `@data/<db_name>/`
- `get_current_db_path()`: Get current database path
- `list_databases()`: List all databases in `@data/`

### CatalogManager

**Location:** `include/storage/catalog_manager.h`

**Key methods:**
- `load_table_meta(db_path, table_name)`: Load page 0 from disk into catalog pool (on-demand)
- `get_table_meta(db_path, table_name)`: Get meta page from pool (loads if not present)
- `create_table_meta(db_path, table_name, schema)`: Create new table, write to pool and disk
- `read_schema(db_path, table_name)`: Read schema from cached meta page
- `write_schema(db_path, table_name, schema)`: Update schema in pool (mark dirty)
- `flush()`: Write all dirty pages from pool to disk
- `clear()`: Clear catalog pool (evict all cached pages)

## Future Scopes

### 1. DML-Driven Catalog Load

**Scope:** When SELECT, INSERT, UPDATE, or DELETE operations need a table's schema, they will call `CatalogManager::get_table_meta()` or `load_table_meta()` to ensure the meta page is in the catalog pool.

**Current state:** Catalog manager provides the APIs (`get_table_meta`, `load_table_meta`)

**Future work:**
- Wire executors/query path to call catalog manager when resolving tables
- On-demand loading: If a table's meta page is not in the pool, load it automatically
- This integration is **future scope**; the current implementation provides the foundation

**Example future flow:**
```
SELECT * FROM users;
  → Executor calls catalog_manager.get_table_meta("@data/mydb/", "users")
  → If not in pool: load from disk into pool
  → Read schema from pool
  → Execute query
```

### 2. Executor Wiring

**Scope:** Executors (SeqScanExecutor, FilterExecutor, etc.) will use the catalog manager to resolve table schemas.

**Current state:** Executors may use `StorageManager` directly or in-memory schemas

**Future work:**
- Update executors to call `CatalogManager::read_schema()` instead of direct file access
- Ensure catalog manager is integrated into the query execution path
- This is **future scope**; current executors work independently

### 3. Larger or Variable Pool Size

**Scope:** If needed later, the catalog pool size could be made configurable or increased.

**Current state:** Fixed 3-page pool (24KB)

**Future considerations:**
- Make pool size configurable (e.g., via constructor parameter)
- Consider larger pools for systems with many tables
- Revisit preallocation vs on-demand allocation for very large pools (e.g., 100+ pages)
- This is **future scope**; current 3-page pool is sufficient for initial implementation

### 4. Multi-Database Catalog Pool

**Scope:** Currently, the catalog pool is shared across all databases. When switching databases with `use_db()`, the pool is cleared.

**Future consideration:**
- Could maintain separate pools per database
- Or use a larger pool and track which database each cached table belongs to
- This is **future scope**; current design (clear on switch) is simple and sufficient

## Usage Examples

### Creating a Database and Table

```cpp
DatabaseManager db_mgr("@data/");
db_mgr.create_db("mydb");
std::string db_path = db_mgr.use_db("mydb");  // Returns "@data/mydb/"

CatalogManager catalog;
CreateTableStmt schema;
schema.table_name = "users";
// ... set columns ...
catalog.create_table_meta(db_path, "users", schema);
```

### Reading Schema

```cpp
CreateTableStmt schema = catalog.read_schema(db_path, "users");
// Schema is read from catalog pool (loaded from disk if not cached)
```

### Updating Schema and Flushing

```cpp
CreateTableStmt new_schema = /* ... */;
catalog.write_schema(db_path, "users", new_schema);  // Marks page dirty
catalog.flush();  // Writes dirty pages to disk
```

### Switching Databases

```cpp
db_mgr.use_db("otherdb");  // Switch to another database
catalog.clear();  // Clear catalog pool (flushes dirty pages first)
```

## Notes

- The catalog pool is **in-memory only**; it does not persist across program restarts
- On startup, tables must be loaded into the pool on first access (or explicitly via `load_table_meta()`)
- The catalog manager is **not thread-safe**; concurrent access requires external synchronization
- All paths use forward slashes (`/`) for cross-platform compatibility (C++17 `std::filesystem` handles this)
