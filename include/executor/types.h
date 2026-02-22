#ifndef TYPES_H
#define TYPES_H

#include <variant>
#include <vector>
#include <string>
#include <optional>
#include <map>

// Value type: can be an integer or a string
using Value = std::variant<int, std::string>;

// Tuple: a vector of values representing a row
using Tuple = std::vector<Value>;

// Table: a collection of tuples (rows)
using Table = std::vector<Tuple>;

#endif // TYPES_H
