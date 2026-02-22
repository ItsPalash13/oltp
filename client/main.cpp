/**
 * Interactive SQL client: REPL that connects to OLTP server.
 * Reads SQL until ;, sends to server, receives and displays results.
 * Exits if server is not found.
 */
#include "network/tcp_client.h"
#include "network/protocol.h"
#include <iostream>
#include <string>
#include <algorithm>
#include <cctype>

static std::string trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    size_t end = str.find_last_not_of(" \t\n\r");
    return str.substr(start, end - start + 1);
}

static std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

static bool parse_response(const std::string& raw, bool& is_ok, std::string& current_db, std::string& body) {
    size_t pos = 0;
    size_t line1_end = raw.find('\n', pos);
    if (line1_end == std::string::npos) return false;
    std::string status = raw.substr(pos, line1_end - pos);
    pos = line1_end + 1;

    size_t line2_end = raw.find('\n', pos);
    if (line2_end == std::string::npos) return false;
    std::string db_line = raw.substr(pos, line2_end - pos);
    pos = line2_end + 1;

    if (status == protocol::OK) {
        is_ok = true;
    } else if (status == protocol::ERR) {
        is_ok = false;
    } else {
        return false;
    }

    if (db_line.size() >= protocol::CURRENT_DB_PREFIX.size() &&
        db_line.substr(0, protocol::CURRENT_DB_PREFIX.size()) == protocol::CURRENT_DB_PREFIX) {
        current_db = db_line.substr(protocol::CURRENT_DB_PREFIX.size());
    } else {
        current_db = "none";
    }

    body = raw.substr(pos);
    size_t body_end = body.find_last_not_of(" \t\n\r");
    if (body_end != std::string::npos) {
        body = body.substr(0, body_end + 1);
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 5432;
    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);

    if (!init_winsock()) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    socket_t sock = connect_to_server(host, port);
    if (sock == INVALID_SOCK) {
        std::cerr << "Cannot connect to OLTP server at " << host << ":" << port << ".\n";
        std::cerr << "Start the server first.\n";
        cleanup_winsock();
        return 1;
    }

    std::cout << "OLTP SQL Client (connected to " << host << ":" << port << ")\n";
    std::cout << "Type SQL statements ending with ; (or 'exit;' / 'quit;' to exit)\n\n";

    std::string current_db = "none";
    std::string sql_buffer;

    while (true) {
        if (current_db.empty() || current_db == "none") {
            std::cout << "none> " << std::flush;
        } else {
            std::cout << current_db << "> " << std::flush;
        }

        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }

        if (!sql_buffer.empty()) {
            sql_buffer += " ";
        }
        sql_buffer += line;

        if (line.find(';') != std::string::npos) {
            std::string sql = trim(sql_buffer);
            sql_buffer.clear();

            if (sql.empty()) {
                continue;
            }

            std::string sql_lower = to_lower(sql);
            if (sql_lower == "exit;" || sql_lower == "quit;") {
                close_socket(sock);
                cleanup_winsock();
                return 0;
            }

            if (!send_line(sock, sql)) {
                std::cerr << "Connection lost.\n";
                close_socket(sock);
                cleanup_winsock();
                return 1;
            }

            std::string raw_response;
            if (!read_until(sock, protocol::END + "\n", raw_response)) {
                std::cerr << "Connection lost.\n";
                close_socket(sock);
                cleanup_winsock();
                return 1;
            }

            bool is_ok;
            std::string body;
            if (parse_response(raw_response, is_ok, current_db, body)) {
                if (is_ok) {
                    if (!body.empty()) {
                        std::cout << body << "\n";
                    }
                } else {
                    std::cerr << "Error: " << body << "\n";
                }
            }
        }
    }

    close_socket(sock);
    cleanup_winsock();
    return 0;
}
