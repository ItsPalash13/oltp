#include "network/select_server.h"
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#endif

bool set_socket_nonblocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

int select_ready(fd_set* read_fds, fd_set* write_fds, socket_t max_fd, int timeout_ms) {
    struct timeval tv;
    struct timeval* tvp = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

#ifdef _WIN32
    (void)max_fd;  // Windows ignores first arg
    return select(0, read_fds, write_fds, nullptr, tvp);
#else
    return select(static_cast<int>(max_fd) + 1, read_fds, write_fds, nullptr, tvp);
#endif
}

int recv_available(socket_t sock, std::string& buf) {
    char tmp[4096];
    int n = recv(sock, tmp, sizeof(tmp), 0);
    if (n > 0) {
        buf.append(tmp, static_cast<size_t>(n));
        return n;
    }
#ifdef _WIN32
    if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return 0;  // Would block, no data
    }
#else
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;  // Would block, no data
    }
#endif
    return -1;  // Closed or error
}

int send_remaining(socket_t sock, const std::string& data, size_t offset) {
    if (offset >= data.size()) return 0;
    size_t remaining = data.size() - offset;
    int to_send = static_cast<int>(remaining > 65536 ? 65536 : remaining);
    int n = send(sock, data.data() + offset, to_send, 0);
    if (n > 0) {
        return n;
    }
#ifdef _WIN32
    if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
        return 0;  // Would block
    }
#else
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;  // Would block
    }
#endif
    return -1;  // Error
}
