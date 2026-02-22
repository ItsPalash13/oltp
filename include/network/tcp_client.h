#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

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
#include <arpa/inet.h>
#include <unistd.h>
typedef int socket_t;
#define INVALID_SOCK (-1)
#define close_socket close
#endif

#include <string>

bool init_winsock();
void cleanup_winsock();

socket_t connect_to_server(const std::string& host, int port);

bool send_line(socket_t sock, const std::string& line);
bool read_until(socket_t sock, const std::string& delimiter, std::string& out);

#endif  // TCP_CLIENT_H
