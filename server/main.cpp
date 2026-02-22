/**
 * OLTP SQL Server: event-driven, many connections, single DB worker.
 * Uses select() for I/O multiplexing. One transaction at a time.
 */
#include "network/tcp_server.h"
#include "network/select_server.h"
#include "network/connection.h"
#include "network/protocol.h"
#include "orchestrator/orchestrator.h"
#include "storage/db_manager.h"
#include "transaction/transaction_manager.h"
#include <iostream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#endif

static std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

struct ServerContext {
    DatabaseManager db_mgr;
    TransactionManager txn_mgr;
    std::unordered_map<uint64_t, Connection> connections;
    uint64_t next_connection_id = 1;
    std::mutex conn_mutex;

    std::queue<std::pair<uint64_t, std::string>> task_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::atomic<bool> shutdown{false};

    explicit ServerContext(const std::string& root_path) : db_mgr(root_path) {}
};

static void db_worker_thread(ServerContext& ctx) {
    while (!ctx.shutdown.load()) {
        std::pair<uint64_t, std::string> task;
        {
            std::unique_lock<std::mutex> lock(ctx.queue_mutex);
            ctx.queue_cv.wait_for(lock, std::chrono::milliseconds(100), [&ctx] {
                return ctx.shutdown.load() || !ctx.task_queue.empty();
            });
            if (ctx.shutdown.load()) break;
            if (ctx.task_queue.empty()) continue;
            task = std::move(ctx.task_queue.front());
            ctx.task_queue.pop();
        }

        uint64_t conn_id = task.first;
        const std::string& sql = task.second;

        Connection* conn = nullptr;
        {
            std::lock_guard<std::mutex> lock(ctx.conn_mutex);
            auto it = ctx.connections.find(conn_id);
            if (it == ctx.connections.end()) continue;
            conn = &it->second;
        }

        if (conn->current_db.empty() || conn->current_db == "none") {
            ctx.db_mgr.clear_current_db();
        } else {
            try {
                ctx.db_mgr.use_db(conn->current_db);
            } catch (const std::exception&) {
                std::string err_response = protocol::ERR + "\n" + protocol::CURRENT_DB_PREFIX + "none\n"
                    + "Database does not exist: " + conn->current_db + "\n" + protocol::END + "\n";
                {
                    std::lock_guard<std::mutex> lock(ctx.conn_mutex);
                    auto it = ctx.connections.find(conn_id);
                    if (it != ctx.connections.end()) {
                        it->second.response = std::move(err_response);
                        it->second.send_offset = 0;
                        it->second.state = ConnState::WRITING;
                    }
                }
                continue;
            }
        }

        std::ostringstream out_stream;
        std::ostringstream err_stream;
        std::string response;

        try {
            run_query(sql, ctx.db_mgr, ctx.txn_mgr, out_stream, err_stream);
            std::string current_db = ctx.db_mgr.get_current_db();
            if (current_db.empty()) current_db = "none";
            conn->current_db = current_db;

            std::string output = out_stream.str();
            if (!err_stream.str().empty()) output += err_stream.str();
            response = protocol::OK + "\n" + protocol::CURRENT_DB_PREFIX + current_db + "\n" + output + "\n" + protocol::END + "\n";
        } catch (const std::exception& e) {
            std::string current_db = conn->current_db;
            if (current_db.empty()) current_db = "none";
            response = protocol::ERR + "\n" + protocol::CURRENT_DB_PREFIX + current_db + "\n" + std::string(e.what()) + "\n" + protocol::END + "\n";
        }

        {
            std::lock_guard<std::mutex> lock(ctx.conn_mutex);
            auto it = ctx.connections.find(conn_id);
            if (it != ctx.connections.end()) {
                it->second.response = std::move(response);
                it->second.send_offset = 0;
                it->second.state = ConnState::WRITING;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    int port = 5432;
    if (argc >= 2) port = std::atoi(argv[1]);

    if (!init_winsock()) {
        std::cerr << "Failed to initialize Winsock\n";
        return 1;
    }

    const std::string root_path = "@data/";
    ServerContext ctx(root_path);

    socket_t listen_fd = create_listen_socket(port);
    if (listen_fd == INVALID_SOCK) {
        std::cerr << "Failed to create listen socket on port " << port << "\n";
        cleanup_winsock();
        return 1;
    }

    set_socket_nonblocking(listen_fd);

    std::thread worker(db_worker_thread, std::ref(ctx));

    std::cout << "OLTP SQL Server listening on port " << port << "\n";
    std::cout << "Root data: " << root_path << "\n";

    const int timeout_ms = 100;

    while (true) {
        fd_set read_fds;
        fd_set write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        FD_SET(listen_fd, &read_fds);
        socket_t max_fd = listen_fd;

        {
            std::lock_guard<std::mutex> lock(ctx.conn_mutex);
            for (auto& kv : ctx.connections) {
                Connection& c = kv.second;
                if (c.state == ConnState::CLOSED) continue;
                if (c.state == ConnState::IDLE || c.state == ConnState::READING) {
                    FD_SET(c.sock, &read_fds);
                }
                if (c.state == ConnState::WRITING) {
                    FD_SET(c.sock, &write_fds);
                }
                if (c.sock > max_fd) max_fd = c.sock;
            }
        }

        int n = select_ready(&read_fds, &write_fds, max_fd, timeout_ms);
        if (n < 0) {
            continue;
        }
        if (n == 0) {
            continue;
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            socket_t client_fd = accept_client(listen_fd);
            if (client_fd != INVALID_SOCK) {
                set_socket_nonblocking(client_fd);
                std::lock_guard<std::mutex> lock(ctx.conn_mutex);
                uint64_t id = ctx.next_connection_id++;
                Connection conn;
                conn.sock = client_fd;
                conn.connection_id = id;
                conn.transaction_id = 0;
                conn.state = ConnState::IDLE;
                conn.current_db = "none";
                conn.send_offset = 0;
                ctx.connections[id] = std::move(conn);
            }
        }

        std::vector<uint64_t> to_remove;

        {
            std::lock_guard<std::mutex> lock(ctx.conn_mutex);
            for (auto& kv : ctx.connections) {
                Connection& c = kv.second;
                if (c.state == ConnState::CLOSED) continue;

                if ((c.state == ConnState::IDLE || c.state == ConnState::READING) &&
                    FD_ISSET(c.sock, &read_fds)) {
                    c.state = ConnState::READING;
                    int r = recv_available(c.sock, c.recv_buffer);
                    if (r < 0) {
                        close_socket(c.sock);
                        c.state = ConnState::CLOSED;
                        to_remove.push_back(kv.first);
                        continue;
                    }
                    if (r == 0) continue;

                    size_t nl = c.recv_buffer.find('\n');
                    if (nl != std::string::npos) {
                        std::string sql = c.recv_buffer.substr(0, nl);
                        c.recv_buffer.erase(0, nl + 1);

                        std::string sql_lower = to_lower(sql);
                        if (sql_lower == "exit;" || sql_lower == "quit;") {
                            close_socket(c.sock);
                            c.state = ConnState::CLOSED;
                            to_remove.push_back(kv.first);
                            continue;
                        }

                        if (!sql.empty()) {
                            c.state = ConnState::PENDING_RESULT;
                            {
                                std::lock_guard<std::mutex> qlock(ctx.queue_mutex);
                                ctx.task_queue.push({kv.first, sql});
                            }
                            ctx.queue_cv.notify_one();
                        } else {
                            c.state = ConnState::IDLE;
                        }
                    }
                }

                if (c.state == ConnState::WRITING && FD_ISSET(c.sock, &write_fds)) {
                    int s = send_remaining(c.sock, c.response, c.send_offset);
                    if (s < 0) {
                        close_socket(c.sock);
                        c.state = ConnState::CLOSED;
                        to_remove.push_back(kv.first);
                        continue;
                    }
                    if (s > 0) {
                        c.send_offset += static_cast<size_t>(s);
                        if (c.send_offset >= c.response.size()) {
                            c.response.clear();
                            c.send_offset = 0;
                            c.state = ConnState::IDLE;
                            while (true) {
                                size_t nl = c.recv_buffer.find('\n');
                                if (nl == std::string::npos) break;
                                std::string sql2 = c.recv_buffer.substr(0, nl);
                                c.recv_buffer.erase(0, nl + 1);
                                std::string sql_lower2 = to_lower(sql2);
                                if (sql_lower2 == "exit;" || sql_lower2 == "quit;") {
                                    close_socket(c.sock);
                                    c.state = ConnState::CLOSED;
                                    to_remove.push_back(kv.first);
                                    break;
                                }
                                if (!sql2.empty()) {
                                    c.state = ConnState::PENDING_RESULT;
                                    {
                                        std::lock_guard<std::mutex> qlock(ctx.queue_mutex);
                                        ctx.task_queue.push({kv.first, sql2});
                                    }
                                    ctx.queue_cv.notify_one();
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            for (uint64_t id : to_remove) {
                ctx.connections.erase(id);
            }
        }
    }

    ctx.shutdown.store(true);
    ctx.queue_cv.notify_all();
    worker.join();

    close_socket(listen_fd);
    cleanup_winsock();
    return 0;
}
