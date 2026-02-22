#ifndef CATALOG_STMT_H
#define CATALOG_STMT_H

#include <string>

/*
 * CATALOG LIST;       - list cached tables
 * CATALOG READ table; - force load table into cache
 * CATALOG VIEW;       - view cache state (slot, table, db_path, last_access, dirty)
 * CATALOG EVICT table; - evict table from cache
 */
enum class CatalogOp { List, Read, View, Evict };

struct CatalogStmt {
    CatalogOp op;
    std::string table_name;  // used for Read, Evict; empty for List, View
};

class Parser;

CatalogStmt parse_catalog(Parser& parser);

#endif // CATALOG_STMT_H
