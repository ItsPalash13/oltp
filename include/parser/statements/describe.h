#ifndef DESCRIBE_H
#define DESCRIBE_H

#include <string>

/*
 * DESCRIBE table_name;
 *   - Shows column names and types for the table
 */

struct DescribeStmt {
    std::string table_name;
};

class Parser;

DescribeStmt parse_describe(Parser& parser);

#endif // DESCRIBE_H
