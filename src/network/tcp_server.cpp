#include "network/tcp_server.h"
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
#else
    return true;
#endif
}

void cleanup_winsock() {
#ifdef _WIN32
    if (g_winsock_init) {
        WSACleanup();
        g_winsock_init = false;
    }
#endif
}

socket_t create_listen_socket(int port) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) {
        return INVALID_SOCK;
    }

#ifdef _WIN32
    char opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        close_socket(fd);
        return INVALID_SOCK;
    }
#else
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        close_socket(fd);
        return INVALID_SOCK;
    }
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(fd);
        return INVALID_SOCK;
    }

    if (listen(fd, 5) != 0) {
        close_socket(fd);
        return INVALID_SOCK;
    }

    return fd;
}

socket_t accept_client(socket_t listen_fd) {
    struct sockaddr_in client_addr;
#ifdef _WIN32
    int len = sizeof(client_addr);
#else
    socklen_t len = sizeof(client_addr);
#endif
    socket_t client = accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &len);
    return client;
}

bool read_line(socket_t sock, std::string& out) {
    out.clear();
    char c;
    while (true) {
        int n = recv(sock, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\n') break;
        out += c;
    }
    return true;
}

bool send_all(socket_t sock, const std::string& data) {
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
