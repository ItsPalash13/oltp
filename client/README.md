# OLTP SQL Client

Interactive REPL (Read-Eval-Print Loop) that connects to the OLTP server, sends SQL statements, and displays results.

## Overview

The client is a simple command-line program that:
1. Connects to the OLTP server over TCP
2. Reads SQL from stdin until a semicolon (`;`) is seen
3. Sends the SQL to the server
4. Receives and parses the response
5. Displays results (or errors) to the user

## Files

| File | Description |
|------|-------------|
| `main.cpp` | REPL logic, prompt, I/O, response parsing |
| `build.bat` | Build script (Windows, g++) |

## Dependencies

- `include/network/tcp_client.h` - Socket connection, send_line, read_until
- `include/network/protocol.h` - OK, ERR, END, CURRENT_DB_PREFIX
- `src/network/tcp_client.cpp` - Implementation of TCP client

## Usage

```bash
client.exe [host] [port]
```

- **host** - Server hostname or IP (default: 127.0.0.1)
- **port** - Server port (default: 5432)

Examples:
```
client.exe
client.exe localhost 5432
client.exe 192.168.1.10 5432
```

## Protocol

### Request

The client sends one line per SQL statement:
```
<SQL statement>\n
```

Statements must end with `;`. Multi-line statements are supported (lines joined with spaces).

### Response

The server returns a structured response:

```
<status>\n
CURRENT_DB: <database_name>\n
<body>\n
END\n
```

- **status** - `OK` or `ERR`
- **CURRENT_DB:** - Current database context (updated after USE, CREATE DATABASE, etc.)
- **body** - Result rows, message text, or error message
- **END** - Delimiter marking end of response

## Operation Flow

```
┌─────────────────────────────────────────────────────────────────┐
│ 1. Init Winsock (Windows)                                        │
│ 2. Connect to server (host:port)                                 │
│ 3. REPL loop:                                                    │
│    ┌─────────────────────────────────────────────────────────┐   │
│    │ Show prompt: "none>" or "<dbname>"                       │   │
│    │ Read line from stdin                                     │   │
│    │ Append to sql_buffer                                     │   │
│    │ If line contains ';':                                    │   │
│    │   - Trim buffer → sql                                    │   │
│    │   - If empty: continue                                   │   │
│    │   - If exit/quit: close, exit                            │   │
│    │   - Else: send_line(sql + "\n")                          │   │
│    │   - read_until("END\n", raw_response)                    │   │
│    │   - parse_response() → status, current_db, body          │   │
│    │   - Print body (cout) or error (cerr)                    │   │
│    │   - Update current_db for next prompt                    │   │
│    └─────────────────────────────────────────────────────────┘   │
│ 4. Close socket, cleanup Winsock                                 │
└─────────────────────────────────────────────────────────────────┘
```

## State Machine

```
Init → Idle → Accumulating → StatementReady → Sending → Receiving → Displaying → Idle
  │       │         │               │            │           │
  └───────┴─────────┴───────────────┴────────────┴───────────┴──→ Exit / Error
```

| State | Action |
|-------|--------|
| **Idle** | Show prompt, getline; on EOF → Exit |
| **Accumulating** | Append line; if no `;` → Idle; if `;` → StatementReady |
| **StatementReady** | Trim; if empty → Idle; if exit/quit → Exit; else → Sending |
| **Sending** | send_line; on fail → Error; else → Receiving |
| **Receiving** | read_until; on fail → Error; else → Displaying |
| **Displaying** | Parse, print, update current_db → Idle |

## Key Functions (main.cpp)

| Function | Purpose |
|----------|---------|
| `trim(str)` | Remove leading/trailing whitespace |
| `to_lower(str)` | Convert to lowercase (for exit/quit check) |
| `parse_response(raw, is_ok, current_db, body)` | Parse status line, CURRENT_DB line, and body from raw response |

## TCP Client API (tcp_client.cpp)

| Function | Purpose |
|----------|---------|
| `init_winsock()` | Initialize Winsock (Windows); no-op on Unix |
| `cleanup_winsock()` | Cleanup Winsock (Windows) |
| `connect_to_server(host, port)` | Connect to server, return socket or INVALID_SOCK |
| `send_line(sock, line)` | Send `line + "\n"`; returns false on failure |
| `read_until(sock, delimiter, out)` | Read until delimiter (e.g. "END\n"), append to out; returns false on failure |
| `close_socket(sock)` | Close socket (closesocket on Windows, close on Unix) |

## Build

From the `client/` directory:

```bash
build.bat
```

Produces `client.exe` (Windows). Requires g++ with C++17. Links `ws2_32` for Winsock.

## Error Handling

- **Winsock init failure** - Prints error, exits 1
- **Connection failure** - Prints "Cannot connect... Start the server first.", exits 1
- **Send/recv failure** - Prints "Connection lost.", exits 1
- **Server error (ERR)** - Prints "Error: \<body\>" to stderr
- **EOF on stdin** - Graceful exit 0
