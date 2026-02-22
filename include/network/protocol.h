#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>

namespace protocol {

const std::string OK = "OK";
const std::string ERR = "ERR";
const std::string END = "END";
const std::string CURRENT_DB_PREFIX = "CURRENT_DB: ";
const char DELIMITER = '\n';

}  // namespace protocol

#endif  // PROTOCOL_H
