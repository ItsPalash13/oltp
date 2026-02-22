# OLTP

A row-oriented OLTP database engine with B+ tree indexing, buffer pool, and catalog cache. Built in C++ with TCP client-server architecture.

---

## What Is This Project?

This is an educational/research OLTP engine that demonstrates:

- **Row storage** — Data stored in table files (`.ibd`), 8KB pages, slotted page layout
- **B+ tree index** — Primary key index for point lookups and range scans
- **Buffer pool** — Page cache with pin/unpin and eviction
- **Catalog manager** — In-memory cache (3-slot LRU) for table meta pages (page 0)
- **SQL subset** — CREATE DATABASE, CREATE TABLE, USE, INSERT, SELECT, UPDATE, DELETE, DESCRIBE, CATALOG
- **Transactions** — One transaction per statement (serialized execution via worker thread)

The server listens on TCP (default 5432). The client is an interactive REPL that sends SQL and displays results.

---

## Project Structure

```
OLTP-ORI/
├── server/                  # Server build and entry
│   ├── build.bat            # Build server.exe (run from server/)
│   ├── main.cpp             # select() I/O loop, db worker, connections
│   └── server.exe           # (after build)
├── client/                  # Client build and entry
│   ├── build.bat            # Build client.exe (run from client/)
│   ├── main.cpp             # REPL, send SQL, parse OK/ERR/END
│   └── client.exe           # (after build)
├── @data/                   # Created on first use — all persistent data (run server from project root)
│   └── <db_name>/           # Database directory
│       ├── <table>.ibd      # Table file (pages: meta page 0, data pages)
│       └── ...
├── include/
│   ├── analyser/
│   ├── executor/
│   ├── network/
│   ├── orchestrator/
│   ├── parser/              # statements.h + statements/*.h
│   ├── planner/
│   ├── storage/
│   └── transaction/
├── src/
│   ├── analyser/
│   ├── executor/
│   ├── network/
│   ├── orchestrator/
│   ├── parser/
│   ├── planner/
│   ├── storage/
│   └── transaction/
├── experiments/             # Prototypes (e.g. B+ tree, parser variants)
└── README.md
```

**Storage layout:** Each table is one `.ibd` file. Page 0 holds table metadata (schema); subsequent pages hold row data in slotted layout. Catalog manager caches up to 3 table meta pages with LRU eviction.

---

## How to Run

1. **Build server**
   ```batch
   cd server
   build.bat
   ```
   Produces `server.exe` in `server/`.

2. **Build client**
   ```batch
   cd client
   build.bat
   ```
   Produces `client.exe` in `client/`.

3. **Start the server** (run first, keep it open)
   ```batch
   cd server
   server.exe
   ```
   Or from project root: `server\server.exe`. Server listens on `127.0.0.1:5432`. The `@data/` folder is created in the current working directory on first use (create database/table).

4. **Run the client** (in a separate terminal)
   ```batch
   cd client
   client.exe
   ```
   Or with host/port: `client.exe 127.0.0.1 5432`

5. **Use the REPL** — type SQL, end with `;`. Example:
   ```sql
   CREATE DATABASE mydb;
   CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100));
   USE mydb;
   INSERT INTO users VALUES (1, 'alice');
   INSERT INTO users (id, name) VALUES (2, 'bob');
   SELECT * FROM users;
   SELECT name FROM users WHERE id = 1;
   UPDATE users SET name = 'Alice' WHERE id = 1;
   DELETE FROM users WHERE id = 2;
   DESCRIBE users;
   exit;
   ```

**Note:** Run the server from project root (or `server/`) so `@data/` is created in a predictable location.

---

## Architecture — Components & Interactions

### High-Level Diagram

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT (client.exe)                                     │
│  REPL prompt  →  SQL line(s)  →  TCP send  →  TCP recv  →  parse OK/ERR  →  display  │
└─────────────────────────────────────────────────────────────────────────────────────┘
                                            │
                                            │ TCP (host:port)
                                            ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              SERVER (server.exe)                                     │
│  ┌──────────────────────────────────────────────────────────────────────────────┐   │
│  │  Network Layer (select() I/O loop)                                            │   │
│  │  • Accept connections, read SQL lines, enqueue to db_worker                    │   │
│  │  • Drain response, send back OK/ERR + CURRENT_DB + body + END                 │   │
│  └──────────────────────────────────┬───────────────────────────────────────────┘   │
│                                     │ task_queue (conn_id, sql)                      │
│                                     ▼                                                │
│  ┌──────────────────────────────────────────────────────────────────────────────┐   │
│  │  DB worker thread (single)                                                    │   │
│  │  One query at a time: run_query(sql, db_mgr, txn_mgr)                         │   │
│  └──────────────────────────────────┬───────────────────────────────────────────┘   │
└─────────────────────────────────────┼────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│  ORCHESTRATOR (orchestrator.cpp)                                                     │
│  • parse_statement() → Statement                                                     │
│  • analyse(stmt, db_mgr) → AnalysisResult                                            │
│  • Switch on statement type: CREATE/USE/DESCRIBE/CATALOG or plan + execute           │
│  • DML: build_plan() → build_executor() / execute_plan() → print results              │
└─────────────────────────────────────────────────────────────────────────────────────┘
          │                    │                    │                    │
          │ Create/Use/        │ INSERT/            │ SELECT             │ UPDATE/DELETE
          │ Describe/Catalog   │                    │                    │
          ▼                    ▼                    ▼                    ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│  DatabaseManager │  │  Planner          │  │  Planner          │  │  Planner          │
│  • create_db     │  │  • InsertPlan    │  │  • SeqScan/        │  │  • UpdatePlan/    │
│  • use_db        │  │  • ValuesPlan    │  │    Filter/Project/ │  │    DeletePlan     │
│  • get_storage   │  │  Executor        │  │    Sort/Collect    │  │  • Filter + Scan  │
│  Catalog (meta)  │  │  • InsertExecutor│  │  SelectExecutor   │  │  Executor         │
└────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘  └────────┬─────────┘
         │                     │                     │                     │
         │                     │                     │                     │
         ▼                     ▼                     ▼                     ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│  STORAGE LAYER                                                                       │
│  ┌─────────────┐  ┌─────────────────┐  ┌─────────────────────────────────────────┐  │
│  │ Catalog     │  │ Storage         │  │ B+ tree (per table, PK)                  │  │
│  │ • 3-slot    │  │ • read/write    │  │ • IndexScan when WHERE on PK prefix     │  │
│  │   LRU meta  │  │   pages         │  │ • SeqScan otherwise                      │  │
│  │ • flush     │  │ • schema from   │  │ • Used for INSERT/UPDATE/DELETE by key  │  │
│  └──────┬──────┘  │   catalog       │  └─────────────────────────────────────────┘  │
│         │         └────────┬────────┘                                               │
│         │                  │  BufferPool, PageLayout, SchemaSerializer    │
│         ▼                  ▼                                                          │
│  ┌──────────────────────────────────────────────────────────────────────────────┐   │
│  │  DISK: @data/<db>/<table>.ibd                                                │   │
│  │  Page 0 = meta (schema); rest = slotted row pages                             │   │
│  └──────────────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

### Component Interactions

| Component | Role | Interacts With |
|-----------|------|----------------|
| **Client** | TCP REPL: sends SQL, receives OK/ERR + CURRENT_DB + body, shows prompt (`none>` or `<db>`) | Server (TCP) |
| **Network** | select() I/O loop, accept/recv/send, enqueue (conn_id, sql) to task_queue, worker sends response | DB worker, connections |
| **DB worker** | Single thread: dequeue task, run_query(sql, db_mgr, txn_mgr), write response to connection | Orchestrator, DatabaseManager, TransactionManager |
| **Orchestrator** | Parse → Analyse → route by type; DML: build_plan → execute (executor or execute_plan) | Parser, Analyser, Planner, Executor, Storage, Catalog |
| **Parser** | Recursive-descent: tokens → Statement (Select, Create, Insert, Update, Delete, Use, Describe, Catalog) | Orchestrator |
| **Analyser** | Validate DB/table exist, columns valid, schema matches; fill AnalysisResult | Orchestrator, DatabaseManager |
| **Planner** | Build plan tree: SeqScan/IndexScan, Filter, Project, Sort, Collect, Insert, Update, Delete, Values | Orchestrator, Executor |
| **Executor** | build_executor(plan) → Executor; next() returns optional Tuple; Insert/Update/Delete return row count | Storage, BPlusTree, Transaction |
| **DatabaseManager** | create_db, drop_db, use_db, get_storage_engine(); root_path = @data/ | Storage, disk |
| **Storage** | Table-level read/write, get_table_schema, create_bplustree; uses CatalogManager, BufferPoolManager, StorageManager | Catalog, BufferPool, B+ tree |
| **CatalogManager** | get_table_meta, read_schema, write_schema, create_table_meta, flush; 3-slot LRU cache | Storage, disk (meta pages) |
| **TransactionManager** | execute(lambda): runs lambda with one Transaction (serialized) | Orchestrator |
| **BPlusTree** | Primary key index; point lookup and range scan for IndexScan plans | Storage, Executor |

### Query Flow Summary

- **CREATE DATABASE/TABLE:** SQL → Parser → Analyser → DatabaseManager / Catalog create_db / create_table_meta
- **USE:** SQL → Parser → Analyser → DatabaseManager.use_db
- **INSERT:** SQL → Parser → Analyser → Planner (InsertPlan + ValuesPlan) → InsertExecutor → Storage.insert (B+ tree + page writes)
- **SELECT:** SQL → Parser → Analyser → Planner (SeqScan/IndexScan + Filter + Project + Sort/Collect) → execute_plan → results
- **UPDATE/DELETE:** SQL → Parser → Analyser → Planner (UpdatePlan/DeletePlan + Filter + Scan) → Executor → Storage updates, catalog flush

---

## Available Commands

### Build & Run

| Command | Description |
|---------|-------------|
| `server\build.bat` | Compiles `server.exe` in `server/` |
| `client\build.bat` | Compiles `client.exe` in `client/` |
| `server\server.exe` | Start the OLTP server (run first; from project root or server/) |
| `client\client.exe [host] [port]` | Start SQL REPL (default: 127.0.0.1 5432) |

See [How to Run](#how-to-run) for step-by-step instructions.

### SQL Commands — Reference

| Command | Example |
|---------|---------|
| **CREATE DATABASE** | `CREATE DATABASE mydb;` |
| **CREATE TABLE** | `CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100));` |
| **USE** | `USE mydb;` |
| **INSERT** | `INSERT INTO users VALUES (1, 'alice');` or `INSERT INTO users (id, name) VALUES (2, 'bob');` |
| **SELECT** | `SELECT * FROM users;` or `SELECT name FROM users WHERE id = 1;` |
| **UPDATE** | `UPDATE users SET name = 'Alice' WHERE id = 1;` |
| **DELETE** | `DELETE FROM users WHERE id = 2;` |
| **DESCRIBE** | `DESCRIBE users;` |
| **CATALOG LIST** | `CATALOG LIST;` — list cached tables |
| **CATALOG READ** | `CATALOG READ users;` — load table meta into cache |
| **CATALOG VIEW** | `CATALOG VIEW;` — show cache slots (table, db_path, last_access, dirty) |
| **CATALOG EVICT** | `CATALOG EVICT users;` — evict table from cache |
| **exit / quit** | `exit;` or `quit;` (client only) |

---

### SQL Commands — Examples

Use `USE <db>;` before DML so the server has a current database. Table names in DML are unqualified (current database).

#### CREATE DATABASE / TABLE

```sql
CREATE DATABASE mydb;
USE mydb;
CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(100));
CREATE TABLE products (id INT PRIMARY KEY, name VARCHAR(200), price INT);
```

#### INSERT

```sql
INSERT INTO users VALUES (1, 'alice');
INSERT INTO users (id, name) VALUES (2, 'bob');
```

#### SELECT

```sql
SELECT * FROM users;
SELECT name FROM users WHERE id = 1;
SELECT * FROM users WHERE id > 0;
```

#### UPDATE / DELETE

```sql
UPDATE users SET name = 'Alice' WHERE id = 1;
DELETE FROM users WHERE id = 2;
```

#### DESCRIBE / CATALOG

```sql
DESCRIBE users;
CATALOG LIST;
CATALOG VIEW;
CATALOG READ users;
CATALOG EVICT users;
```

---

## Data Types & Constraints

- **Types:** `INT`, `VARCHAR(n)` (stored as `int` and `std::string` in executor types).
- **Primary key:** At most one `PRIMARY KEY` column per table; used for B+ tree index. DELETE/UPDATE require a table with PRIMARY KEY (row_id exposure for tables without PK is planned).
- **Other constraints:** NOT NULL, UNIQUE mentioned in schema but UNIQUE is not yet supported.

---

## Limitations

- **No NULL support** — All columns are effectively NOT NULL.
- **No DATE/TIME types** — Use INT or string for timestamps.
- **No subqueries or JOINs** — Single-table DML only.
- **One transaction per statement** — No BEGIN/COMMIT/ROLLBACK; no multi-statement transactions.
- **No WAL** — Writes go to page files; no crash recovery.
- **Single DB worker** — Queries executed one at a time.
- **ORDER BY** — Planner builds SortPlan but SortExecutor is not implemented (future).
- **Catalog** — 3-slot cache; cleared on USE another database.
- **Primary key** — Single-column only; composite PK not supported.
