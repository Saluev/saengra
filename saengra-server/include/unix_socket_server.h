#pragma once

#include <string>
#include <functional>

namespace saengra {

class UnixSocketServer {
public:
    using MessageHandler = std::function<std::string(const std::string&)>;

    explicit UnixSocketServer(const std::string& socket_path);
    ~UnixSocketServer();

    // Delete copy constructor and assignment
    UnixSocketServer(const UnixSocketServer&) = delete;
    UnixSocketServer& operator=(const UnixSocketServer&) = delete;

    // Set the message handler callback
    void setMessageHandler(MessageHandler handler);

    // Start the server (blocking)
    bool start();

    // Stop the server
    void stop();

    // Check if server is running
    bool isRunning() const;

private:
    std::string socket_path_;
    int server_fd_;
    bool running_;
    MessageHandler message_handler_;
};

} // namespace saengra
