# Saengra - Unix Socket Server

A simple C++ server application that communicates via Unix domain sockets.

## Features

- Unix socket-based communication
- Length-prefixed message protocol
- Signal handling for graceful shutdown
- Configurable socket path

## Project Structure

```
saengra/
├── CMakeLists.txt              # Build configuration
├── README.md                   # This file
├── include/
│   └── unix_socket_server.h    # Server class header
├── src/
│   ├── main.cpp                # Main server application
│   └── unix_socket_server.cpp  # Server implementation
└── build/                      # Build directory (created by cmake)
```

## Building

```bash
cd /Users/saluev/vic/saengra
mkdir -p build && cd build
cmake ..
make
```

## Running

```bash
# Start the server (default socket: /tmp/saengra.sock)
./saengra_server

# Or specify a custom socket path
./saengra_server /tmp/custom.sock
```

## Testing with netcat

You can test the server using netcat (nc) or socat:

```bash
# Using socat
echo -n -e '\x05\x00\x00\x00Hello' | socat - UNIX-CONNECT:/tmp/saengra.sock | hexdump -C

# The message format is:
# - 4 bytes: message length (little-endian uint32)
# - N bytes: message data
```

## Protocol

Messages are sent with the following format:

1. **Message Length** (4 bytes, uint32_t): Size of the message
2. **Message Data** (N bytes): The actual message content

The server will echo back any received message with "Echo: " prefix.

## Stopping the Server

Press `Ctrl+C` or send `SIGTERM` to gracefully shut down the server.
