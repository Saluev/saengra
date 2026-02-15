#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include "logging.h"
#include "unix_socket_server.h"
#include "worker.h"

namespace saengra {

UnixSocketServer::UnixSocketServer(const std::string& socket_path)
    : socket_path_(socket_path), server_fd_(-1), running_(false) {
}

UnixSocketServer::~UnixSocketServer() {
    stop();
}

void UnixSocketServer::setMessageHandler(MessageHandler handler) {
    message_handler_ = handler;
}

bool UnixSocketServer::start() {
    if (running_) {
        spdlog::error("Server already running");
        return false;
    }

    // Remove existing socket file
    unlink(socket_path_.c_str());

    // Create socket
    server_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        spdlog::error("Failed to create socket: {}", strerror(errno));
        return false;
    }

    // Bind socket
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("Failed to bind socket: {}", strerror(errno));
        close(server_fd_);
        return false;
    }

    // Listen
    if (listen(server_fd_, 5) < 0) {
        spdlog::error("Failed to listen on socket: {}", strerror(errno));
        close(server_fd_);
        return false;
    }

    running_ = true;

    spdlog::info("Server started on {}", BRIGHT_WHITE(socket_path_));

    // Accept connections and fork worker processes
    while (running_) {
        int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (running_) {
                spdlog::error("Failed to accept connection: {}", strerror(errno));
            }
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            spdlog::error("Failed to fork worker process: {}", strerror(errno));
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            // Child process (worker)
            running_ = false;
            close(server_fd_); // Worker doesn't need the server socket
            Worker worker(client_fd);
            worker.run();
            close(client_fd);
            exit(0);
        } else {
            // Parent process
            close(client_fd); // Parent doesn't need the client socket

            // Reap zombie processes (non-blocking)
            while (waitpid(-1, nullptr, WNOHANG) > 0) {
                // Continue reaping
            }
        }
    }

    return true;
}

void UnixSocketServer::stop() {
    if (running_) {
        running_ = false;
        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }
        unlink(socket_path_.c_str());
        spdlog::info("Server stopped");
    }
}

bool UnixSocketServer::isRunning() const {
    return running_;
}

} // namespace saengra
