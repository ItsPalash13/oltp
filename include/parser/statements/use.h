#ifndef USE_H
#define USE_H

#include <string>

/*
 * USE database_name;
 *   - Switches current database context
 */

struct UseStmt {
    std::string database_name;
};

class Parser;

UseStmt parse_use(Parser& parser);

#endif // USE_H
