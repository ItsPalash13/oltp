#ifndef SELECT_SERVER_H
#define SELECT_SERVER_H

#include "network/tcp_server.h"
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

/**
 * Set socket to non-blocking mode.
 * Returns true on success.
 */
bool set_socket_nonblocking(socket_t sock);

/**
 * Call select() on read_fds and write_fds.
 * timeout_ms: timeout in milliseconds (0 = return immediately, -1 = block forever).
 * Returns: >0 = count of ready descriptors, 0 = timeout, <0 = error.
 */
int select_ready(fd_set* read_fds, fd_set* write_fds, socket_t max_fd, int timeout_ms);

/**
 * Non-blocking recv: read available bytes into buf.
 * Returns: bytes read (>0), 0 on closed/error.
 * Appends to buf.
 */
int recv_available(socket_t sock, std::string& buf);

/**
 * Non-blocking send: send data from offset.
 * Returns: bytes sent (>0), 0 if would block or error.
 */
int send_remaining(socket_t sock, const std::string& data, size_t offset);

#endif  // SELECT_SERVER_H
