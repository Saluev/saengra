#pragma once

#include <string>
#include "graph.h"
#include "observer_container.h"

// Forward declarations
namespace saengra_api {
    class Request;
    class Response;
    class Connect;
    class ConnectResponse;
    class ApplyUpdates;
    class ApplyUpdatesResponse;
    class Commit;
    class CommitResponse;
    class Rollback;
    class RollbackResponse;
    class Observe;
    class ObserveResponse;
    class Find;
    class FindResponse;
    class Match;
    class MatchResponse;
}

namespace saengra {

using GraphID = std::string;

struct Environment {
    explicit Environment(GraphID graph_id): graph_id(graph_id), graph(), observer_container(graph) {}
    void rollback();

    GraphID graph_id;
    Graph graph;
    ObserverContainer observer_container;
};

class Worker {
public:
    explicit Worker(int client_fd);
    ~Worker();

    // Delete copy constructor and assignment
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // Run the worker loop (processes messages until SIGTERM or connection closes)
    void run();

private:
    // Request handlers
    void handleConnect(const saengra_api::Connect& request, saengra_api::ConnectResponse& response);
    void handleApplyUpdates(const saengra_api::ApplyUpdates& request, saengra_api::ApplyUpdatesResponse& response);
    void handleCommit(const saengra_api::Commit& request, saengra_api::CommitResponse& response);
    void handleRollback(const saengra_api::Rollback& request, saengra_api::RollbackResponse& response);
    void handleObserve(const saengra_api::Observe& request, saengra_api::ObserveResponse& response);
    void handleFind(const saengra_api::Find& request, saengra_api::FindResponse& response);
    void handleMatch(const saengra_api::Match& request, saengra_api::MatchResponse& response);

    // Helper methods
    std::string receiveMessage();
    bool sendMessage(const std::string& message);
    bool sendResponse(const saengra_api::Response& response);

    int client_fd_;

    std::map<GraphID, std::unique_ptr<Environment>> envs_;
    Environment* current_env_ = nullptr;
};

} // namespace saengra
