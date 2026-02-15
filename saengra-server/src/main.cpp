#include <csignal>
#include <memory>
#include <chrono>
#include <google/protobuf/stubs/common.h>
#include "logging.h"
#include "unix_socket_server.h"
#include "graph.h"
#include "messages.pb.h"
#include "subgraph.h"
#include "queryset.h"

std::unique_ptr<saengra::UnixSocketServer> g_server;

void signalHandler(int signal) {
    spdlog::info("Received signal {}, shutting down...", BRIGHT_WHITE(signal));
    if (g_server) {
        g_server->stop();
    }
}

std::string handleMessage(const std::string& data) {
    // Try to parse as protobuf Request
    saengra_api::Request request;
    if (request.ParseFromString(data)) {
//        std::cout << "Received protobuf Request: " << request.message() << std::endl;

        // Create Response
        saengra_api::Response response;
//        response.set_reply("Echo: " + request.message());
//        response.set_timestamp(
//            std::chrono::system_clock::now().time_since_epoch().count()
//        );
//        response.set_success(true);

        return response.SerializeAsString();
    } else {
        // Fallback to plain text echo
        spdlog::debug("Received plain text: {}", data);
        return "Echo: " + data;
    }
}

int main(int argc, char* argv[]) {
    // Configure spdlog
    auto console = spdlog::stdout_color_mt("console", std::getenv("SAENGRA_LOGS_COLOR") ? spdlog::color_mode::always : spdlog::color_mode::automatic);
    console->set_pattern("[saengra-server] [%H:%M:%S.%e] [%^%l%$] %v");
    console->set_level(spdlog::level::debug);
    spdlog::set_default_logger(console);

    // Initialize protobuf
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    std::string socket_path = "/tmp/saengra.sock";
    if (argc > 1) {
        socket_path = argv[1];
    }

    spdlog::info("Starting Saengra server with Protobuf support...");
    spdlog::info("Socket path: {}", socket_path);

    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // Create and start server
    g_server = std::make_unique<saengra::UnixSocketServer>(socket_path);
    g_server->setMessageHandler(handleMessage);

    if (!g_server->start()) {
        spdlog::error("Failed to start server");
        return 1;
    }

    // Cleanup protobuf
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
