# Query Planner Documentation

## Overview

The query planner transforms parsed SQL statements into execution plan trees. Each plan node represents a physical operator that can be executed by the query execution engine. The planner follows an **iterator model** (also known as the Volcano model), where each operator consumes input from its child operators and produces output for its parent.

## Architecture

### Plan IR (Intermediate Representation)

The Plan IR is defined in `plan.h` and consists of:

1. **PlanType enum**: Identifies the type of plan node
2. **Base Plan struct**: Virtual base class for all plan nodes
3. **Concrete Plan Node structs**: Specific implementations for each operator type

### Plan Node Hierarchy

```
Plan (base class)
├── SeqScanPlan      - Sequential table scan
├── IndexScanPlan    - Index-based scan (reserved for future)
├── FilterPlan      - WHERE clause filtering
├── ProjectPlan     - Column projection (SELECT list)
├── SortPlan        - ORDER BY sorting
├── InsertPlan      - INSERT statement execution
├── UpdatePlan      - UPDATE statement execution
├── DeletePlan      - DELETE statement execution
└── ValuesPlan      - VALUES clause (for INSERT)
```

## Plan Node Types

### 1. SeqScanPlan

**Purpose**: Performs a sequential scan of a table, reading all rows.

**Structure**:
```cpp
struct SeqScanPlan : Plan {
    std::string table;  // Table name to scan
};
```

**Usage**: Base operator for all queries that read from a table.

**Example**: `SELECT * FROM users;` → `SeqScanPlan("users")`

---

### 2. FilterPlan

**Purpose**: Filters rows based on a WHERE clause predicate.

**Structure**:
```cpp
struct FilterPlan : Plan {
    Expr* predicate;              // WHERE clause expression
    std::unique_ptr<Plan> source; // Child plan (usually SeqScan)
};
```

**Usage**: Wraps a scan or other plan to filter rows that don't match the predicate.

**Example**: `SELECT * FROM users WHERE id > 10;`
```
FilterPlan
 └─ SeqScanPlan("users")
```

---

### 3. ProjectPlan

**Purpose**: Projects specific columns from the input (implements SELECT column list).

**Structure**:
```cpp
struct ProjectPlan : Plan {
    std::vector<Expr*> projections; // SELECT column expressions
    std::unique_ptr<Plan> source;   // Child plan
};
```

**Usage**: Transforms rows to include only the specified columns/expressions.

**Example**: `SELECT name, email FROM users;`
```
ProjectPlan (columns: [name, email])
 └─ SeqScanPlan("users")
```

---

### 4. SortPlan

**Purpose**: Sorts rows based on ORDER BY expressions.

**Structure**:
```cpp
struct SortPlan : Plan {
    std::vector<Expr*> order_by;  // ORDER BY expressions
    std::unique_ptr<Plan> source; // Child plan
};
```

**Usage**: Sorts the input rows according to the ORDER BY clause.

**Example**: `SELECT * FROM users ORDER BY name;`
```
SortPlan (order_by: [name])
 └─ SeqScanPlan("users")
```

---

### 5. InsertPlan

**Purpose**: Inserts rows into a table.

**Structure**:
```cpp
struct InsertPlan : Plan {
    std::string table;                    // Target table
    std::vector<std::string> columns;     // Column list (optional)
    std::unique_ptr<Plan> source;          // ValuesPlan or SelectPlan
};
```

**Usage**: Inserts data from the source plan into the specified table.

**Example**: `INSERT INTO users (id, name) VALUES (1, 'John');`
```
InsertPlan (table: "users", columns: ["id", "name"])
 └─ ValuesPlan (values: [1, 'John'])
```

---

### 6. UpdatePlan

**Purpose**: Updates rows in a table based on assignments and optional WHERE clause.

**Structure**:
```cpp
struct UpdatePlan : Plan {
    std::string table;                    // Target table
    std::vector<Assignment> assignments; // Column = value pairs
    std::unique_ptr<Plan> source;         // FilterPlan + SeqScanPlan
};
```

**Usage**: Updates matching rows with new values.

**Example**: `UPDATE users SET name = 'John' WHERE id = 1;`
```
UpdatePlan (table: "users", assignments: [name='John'])
 └─ FilterPlan (WHERE: id = 1)
     └─ SeqScanPlan("users")
```

---

### 7. DeletePlan

**Purpose**: Deletes rows from a table based on optional WHERE clause.

**Structure**:
```cpp
struct DeletePlan : Plan {
    std::string table;            // Target table
    std::unique_ptr<Plan> source; // FilterPlan + SeqScanPlan (or just SeqScanPlan)
};
```

**Usage**: Deletes matching rows from the table.

**Example**: `DELETE FROM users WHERE id = 1;`
```
DeletePlan (table: "users")
 └─ FilterPlan (WHERE: id = 1)
     └─ SeqScanPlan("users")
```

---

### 8. ValuesPlan

**Purpose**: Produces a single row of literal values (used in INSERT statements).

**Structure**:
```cpp
struct ValuesPlan : Plan {
    std::vector<Expr*> values; // Literal value expressions
};
```

**Usage**: Source operator for INSERT statements with VALUES clause.

**Example**: `INSERT INTO users VALUES (1, 'John');`
```
ValuesPlan (values: [1, 'John'])
```

---

## Planner Implementation

The planner is implemented in `planner.cpp` with the main entry point:

```cpp
std::unique_ptr<Plan> build_plan(const Statement& stmt);
```

### Planning Process

The planner converts parsed SQL statements into execution plans through statement-specific functions:

#### 1. SELECT Statement Planning

**Function**: `build_select_plan(const SelectStmt& select_stmt)`

**Steps**:
1. Create `SeqScanPlan` for the table
2. If WHERE clause exists: wrap in `FilterPlan`
3. If SELECT columns exist: wrap in `ProjectPlan`
4. If ORDER BY exists: wrap in `SortPlan`

**Example**: `SELECT price, discount FROM products WHERE price >= 100 ORDER BY price;`

```
SortPlan
 └─ ProjectPlan
     └─ FilterPlan
         └─ SeqScanPlan("products")
```

**Code Flow**:
```cpp
// Step 1: Base scan
std::unique_ptr<Plan> plan = std::make_unique<SeqScanPlan>(select_stmt.table);

// Step 2: Add WHERE filter
if (select_stmt.where != nullptr) {
    plan = std::make_unique<FilterPlan>(select_stmt.where, std::move(plan));
}

// Step 3: Add projection
if (!select_stmt.columns.empty()) {
    plan = std::make_unique<ProjectPlan>(select_stmt.columns, std::move(plan));
}

// Step 4: Add ORDER BY
if (!select_stmt.order_by.empty()) {
    plan = std::make_unique<SortPlan>(select_stmt.order_by, std::move(plan));
}
```

---

#### 2. INSERT Statement Planning

**Function**: `build_insert_plan(const InsertStmt& insert_stmt)`

**Steps**:
1. Create `ValuesPlan` with the VALUES expressions
2. Create `InsertPlan` with table, columns, and ValuesPlan as source

**Example**: `INSERT INTO users (id, name) VALUES (1, 'John');`

```
InsertPlan
 └─ ValuesPlan
```

**Code Flow**:
```cpp
// Step 1: Create Values plan
auto values_plan = std::make_unique<ValuesPlan>(insert_stmt.values);

// Step 2: Create Insert plan
auto insert_plan = std::make_unique<InsertPlan>(
    insert_stmt.table,
    insert_stmt.columns,
    std::move(values_plan)
);
```

---

#### 3. UPDATE Statement Planning

**Function**: `build_update_plan(const UpdateStmt& update_stmt)`

**Steps**:
1. Create `SeqScanPlan` for the table
2. If WHERE clause exists: wrap in `FilterPlan`
3. Create `UpdatePlan` with assignments and filtered scan as source

**Example**: `UPDATE users SET name = 'John' WHERE id = 1;`

```
UpdatePlan
 └─ FilterPlan
     └─ SeqScanPlan("users")
```

**Code Flow**:
```cpp
// Step 1: Base scan
std::unique_ptr<Plan> plan = std::make_unique<SeqScanPlan>(update_stmt.table);

// Step 2: Add WHERE filter
if (update_stmt.where != nullptr) {
    plan = std::make_unique<FilterPlan>(update_stmt.where, std::move(plan));
}

// Step 3: Create Update plan
plan = std::make_unique<UpdatePlan>(
    update_stmt.table,
    update_stmt.assignments,
    std::move(plan)
);
```

---

#### 4. DELETE Statement Planning

**Function**: `build_delete_plan(const DeleteStmt& delete_stmt)`

**Steps**:
1. Create `SeqScanPlan` for the table
2. If WHERE clause exists: wrap in `FilterPlan`
3. Create `DeletePlan` with scan/filter as source

**Example**: `DELETE FROM users WHERE id = 1;`

```
DeletePlan
 └─ FilterPlan
     └─ SeqScanPlan("users")
```

**Without WHERE**: `DELETE FROM users;`

```
DeletePlan
 └─ SeqScanPlan("users")
```

**Code Flow**:
```cpp
// Step 1: Base scan
std::unique_ptr<Plan> plan = std::make_unique<SeqScanPlan>(delete_stmt.table);

// Step 2: Add WHERE filter
if (delete_stmt.where != nullptr) {
    plan = std::make_unique<FilterPlan>(delete_stmt.where, std::move(plan));
}

// Step 3: Create Delete plan
plan = std::make_unique<DeletePlan>(delete_stmt.table, std::move(plan));
```

---

## Main Dispatcher

The `build_plan()` function dispatches to the appropriate planner based on statement type:

```cpp
std::unique_ptr<Plan> build_plan(const Statement& stmt) {
    switch (stmt.get_type()) {
        case StatementType::Select:
            return build_select_plan(stmt.as_select());
        case StatementType::Insert:
            return build_insert_plan(stmt.as_insert());
        case StatementType::Update:
            return build_update_plan(stmt.as_update());
        case StatementType::Delete:
            return build_delete_plan(stmt.as_delete());
        case StatementType::Create:
            throw std::runtime_error("CREATE statements do not require execution plans");
        default:
            throw std::runtime_error("Unsupported statement type for planning");
    }
}
```

## Design Principles

### 1. Iterator Model

Each plan node follows the iterator model:
- **Pull-based**: Parent operators request data from children
- **Lazy evaluation**: Data is processed on-demand
- **Pipeline execution**: Operators can process data as it flows through

### 2. Tree Structure

Plans form a tree where:
- **Leaf nodes**: Data sources (SeqScanPlan, ValuesPlan)
- **Internal nodes**: Transformations (FilterPlan, ProjectPlan, SortPlan)
- **Root node**: Final operation (InsertPlan, UpdatePlan, DeletePlan, or output)

### 3. Memory Management

- Uses `std::unique_ptr<Plan>` for automatic memory management
- Plan trees are owned by the root node
- Expression pointers (`Expr*`) are raw pointers (owned by the parser)

### 4. Polymorphism

- All plan nodes inherit from base `Plan` struct
- Virtual destructor ensures proper cleanup
- `PlanType` enum allows type identification without RTTI

## Example Plan Trees

### Complex SELECT Query

**SQL**:
```sql
SELECT price * discount / 100
FROM products
WHERE price >= 100 AND discount < 20
ORDER BY price;
```

**Plan Tree**:
```
SortPlan (order_by: [price])
 └─ ProjectPlan (projections: [price * discount / 100])
     └─ FilterPlan (predicate: price >= 100 AND discount < 20)
         └─ SeqScanPlan (table: "products")
```

### INSERT with Multiple Values

**SQL**:
```sql
INSERT INTO users (id, name, email)
VALUES (1, 'John', 'john@example.com');
```

**Plan Tree**:
```
InsertPlan (table: "users", columns: ["id", "name", "email"])
 └─ ValuesPlan (values: [1, 'John', 'john@example.com'])
```

### UPDATE with Expression

**SQL**:
```sql
UPDATE products
SET price = price * 0.9
WHERE price > 100;
```

**Plan Tree**:
```
UpdatePlan (table: "products", assignments: [price = price * 0.9])
 └─ FilterPlan (predicate: price > 100)
     └─ SeqScanPlan (table: "products")
```

## Future Enhancements

### IndexScanPlan

Currently defined but not used. Future implementation will:
- Use indexes for faster lookups
- Replace SeqScanPlan when index conditions match WHERE clause
- Support index-only scans

### GROUP BY Support

Future implementation will add:
- `AggregatePlan`: Groups rows and computes aggregates
- `HashAggregatePlan`: Uses hash tables for grouping
- `SortAggregatePlan`: Uses sorting for grouping

### JOIN Support

Future implementation will add:
- `NestedLoopJoinPlan`: Nested loop join algorithm
- `HashJoinPlan`: Hash-based join algorithm
- `MergeJoinPlan`: Sort-merge join algorithm

## Usage Example

```cpp
#include "planner.h"
#include "../parser/statements.cpp"

// Parse SQL statement
std::string sql = "SELECT name, email FROM users WHERE id > 10;";
Parser parser(sql);
Statement stmt = parse_statement(parser);

// Build execution plan
std::unique_ptr<Plan> plan = build_plan(stmt);

// Plan is now ready for execution by the query executor
// The executor would traverse the plan tree and execute each operator
```

## See Also

- `src/parser/` - SQL parser that produces Statement objects
- `src/planner/main.cpp` - Example usage and test cases
- `experiments/planner_in_rust/planner.rs` - Reference Rust implementation
