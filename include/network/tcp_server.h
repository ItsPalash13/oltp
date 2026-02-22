#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define close_socket close
#endif

#include <string>

bool init_winsock();
void cleanup_winsock();

socket_t create_listen_socket(int port);
socket_t accept_client(socket_t listen_fd);

bool read_line(socket_t sock, std::string& out);
bool send_all(socket_t sock, const std::string& data);

#endif  // TCP_SERVER_H
