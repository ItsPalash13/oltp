#ifndef CONNECTION_H
#define CONNECTION_H

#include "network/tcp_server.h"
#include <cstdint>
#include <string>

enum class ConnState {
    IDLE,
    READING,
    PENDING_RESULT,
    WRITING,
    CLOSED
};

struct Connection {
    socket_t sock;
    uint64_t connection_id;   // Unique per connection (future: logging)
    uint64_t transaction_id;  // 0 = none (future: multi-stmt transactions)
    ConnState state;
    std::string recv_buffer;  // Accumulate until \n
    std::string current_db;   // Per-session USE db
    std::string response;     // Filled by worker, sent when writable
    size_t send_offset;       // Bytes already sent of response
};

#endif  // CONNECTION_H
