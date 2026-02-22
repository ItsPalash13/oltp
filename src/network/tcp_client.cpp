#include "network/tcp_client.h"
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
static bool g_winsock_init = false;
#endif

bool init_winsock() {
#ifdef _WIN32
    if (g_winsock_init) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
    g_winsock_init = true;
    return true;
#endif
    return true;
}

void cleanup_winsock() {
#ifdef _WIN32
    if (g_winsock_init) {
        WSACleanup();
        g_winsock_init = false;
    }
#endif
}

socket_t connect_to_server(const std::string& host, int port) {
    struct addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(host.c_str(), port_str, &hints, &res) != 0) {
        return INVALID_SOCK;
    }

    socket_t fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == INVALID_SOCK) {
        freeaddrinfo(res);
        return INVALID_SOCK;
    }

    if (connect(fd, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        close_socket(fd);
        freeaddrinfo(res);
        return INVALID_SOCK;
    }

    freeaddrinfo(res);
    return fd;
}

bool send_line(socket_t sock, const std::string& line) {
    std::string data = line + "\n";
    const char* p = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
        int sent = send(sock, p, static_cast<int>(remaining), 0);
        if (sent <= 0) return false;
        p += sent;
        remaining -= static_cast<size_t>(sent);
    }
    return true;
}

bool read_until(socket_t sock, const std::string& delimiter, std::string& out) {
    out.clear();
    std::string delim_buf;
    char c;
    while (true) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return false;
        delim_buf += c;
        if (delim_buf.size() > delimiter.size()) {
            delim_buf.erase(0, 1);
        }
        out += c;
        if (delim_buf == delimiter) {
            out.erase(out.size() - delimiter.size());
            return true;
        }
    }
}
