#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <cstring>
#include <vector>
#include "debug.h"
#include "logging.h"
#include "worker.h"
#include "messages.pb.h"
#include "parser.h"
#include "matcher.h"
#include "start_positions.h"

namespace saengra {

void Environment::rollback() {
    graph.rollback();
    observer_container.reinitialize_all();
}

// Global flag for signal handling in worker processes
static volatile sig_atomic_t worker_should_exit = 0;

static void worker_signal_handler(int signal) {
    if (signal == SIGTERM) {
        worker_should_exit = 1;
    }
}

Worker::Worker(int client_fd)
    : client_fd_(client_fd) {
}

Worker::~Worker() {
}

void Worker::run() {
    // Set up signal handler for SIGTERM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = worker_signal_handler;
    sigaction(SIGTERM, &sa, nullptr);

    spdlog::info("Worker process {} started", getpid());

    // Process messages until SIGTERM or connection closes
    while (!worker_should_exit) {
        std::string request_data = receiveMessage();
        if (request_data.empty()) {
            // Connection closed or error
            break;
        }

        // Parse protobuf Request
        saengra_api::Request request;
        if (!request.ParseFromString(request_data)) {
            spdlog::error("Failed to parse Request message");
            continue;
        }

        // Handle the request based on its type
        saengra_api::Response response;

        if (request.has_connect()) {
            handleConnect(request.connect(), *response.mutable_connect());
        } else if (request.has_apply_updates()) {
            handleApplyUpdates(request.apply_updates(), *response.mutable_apply_updates());
        } else if (request.has_commit()) {
            handleCommit(request.commit(), *response.mutable_commit());
        } else if (request.has_rollback()) {
            handleRollback(request.rollback(), *response.mutable_rollback());
        } else if (request.has_observe()) {
            handleObserve(request.observe(), *response.mutable_observe());
        } else if (request.has_find()) {
            handleFind(request.find(), *response.mutable_find());
        } else if (request.has_match()) {
            handleMatch(request.match(), *response.mutable_match());
        } else {
            spdlog::error("Unknown request type");
            continue;
        }

        // Send response
        sendResponse(response);
    }

    spdlog::info("Worker process {} exiting", getpid());
}

void Worker::handleConnect(const saengra_api::Connect& request, saengra_api::ConnectResponse& response) {
    spdlog::info("{}(graph_id='{}')", CYAN("Connect"), BRIGHT_WHITE(request.graph_id()));

    GraphID graph_id = request.graph_id();
    if (current_env_ != nullptr && current_env_->graph_id == graph_id) {
        response.set_status(saengra_api::ConnectResponse_Status_OK);
        response.set_created(false);
        spdlog::info("Already connected");
        return;
    }

    if (current_env_ != nullptr) {
        spdlog::info("Disconnected from graph '{}'", current_env_->graph_id);
    }

    if (envs_.contains(graph_id)) {
        current_env_ = envs_[graph_id].get();
        response.set_status(saengra_api::ConnectResponse_Status_OK);
        response.set_created(false);
        spdlog::info("Connected to existing graph '{}'", graph_id);
        return;
    }

    envs_[graph_id] = std::make_unique<Environment>(graph_id);
    current_env_ = envs_[graph_id].get();
    response.set_status(saengra_api::ConnectResponse_Status_OK);
    response.set_created(true);
    spdlog::info("Created new graph '{}'", graph_id);
}

//Update convert_update_from_proto(const saengra_api::ApplyUpdates_Update& proto_update) {
//    UpdateKind kind;
//    switch (proto_update.kind()) {
//        case saengra_api::ApplyUpdates_Update_UpdateKind_ADD_VERTEX:
//            kind = UpdateKind::AddVertex;
//            break;
//        case saengra_api::ApplyUpdates_Update_UpdateKind_ADD_EDGE:
//            kind = UpdateKind::AddEdge;
//            break;
//        case saengra_api::ApplyUpdates_Update_UpdateKind_REMOVE_VERTEX:
//            kind = UpdateKind::RemoveVertex;
//            break;
//        case saengra_api::ApplyUpdates_Update_UpdateKind_REMOVE_EDGE:
//            kind = UpdateKind::RemoveEdge;
//            break;
//        case saengra_api::ApplyUpdates_Update_UpdateKind_REMOVE_EDGES_TO_ALL:
//            kind = UpdateKind::RemoveEdgesToAll;
//            break;
//        default:
//            throw std::runtime_error("unknown update kind");
//    }
//
//    UpdateVertex from{
//        std::string(proto_update.from().type_name()),
//        std::string(proto_update.from().value())
//    };
//
//    std::optional<std::string> label = std::nullopt;
//    if (proto_update.has_label()) {
//        label = proto_update.label();
//    }
//
//    std::optional<UpdateVertex> to = std::nullopt;
//    if (proto_update.has_to()) {
//        to.emplace(
//            std::string(proto_update.to().type_name()),
//            std::string(proto_update.to().value())
//        );
//    }
//
//    return Update{kind, from, label, to};
//}
//
//std::vector<Update> convert_updates_from_proto(const saengra_api::ApplyUpdates& request) {
//    std::vector<Update> updates;
//    updates.reserve(request.updates_size());
//    for (const auto& proto_update : request.updates()) {
//        updates.push_back(convert_update_from_proto(proto_update));
//    }
//    return updates;
//}

void Worker::handleApplyUpdates(const saengra_api::ApplyUpdates& request, saengra_api::ApplyUpdatesResponse& response) {
    spdlog::info("{}({} updates)", CYAN("ApplyUpdates"), BRIGHT_WHITE(request.updates_size()));

    if (current_env_ == nullptr) {
        response.set_status(saengra_api::ApplyUpdatesResponse_Status_NOT_CONNECTED);
        spdlog::error("Can't apply updates — not connected");
        return;
    }

//    auto updates = convert_updates_from_proto(request);

//    for (const auto update : updates) {
//        std::cout << "    " << update << std::endl;
//    }

    current_env_->graph.update(request.updates());
    response.set_status(saengra_api::ApplyUpdatesResponse_Status_OK);
}

void Worker::handleCommit(const saengra_api::Commit& request, saengra_api::CommitResponse& response) {
    spdlog::info("{}()", CYAN("Commit"));

    if (current_env_ == nullptr) {
        response.set_status(saengra_api::CommitResponse_Status_NOT_CONNECTED);
        spdlog::error("Can't commit — not connected");
        return;
    }

    auto graph_observations = current_env_->graph.apply();
    if (graph_observations.empty()) {
        current_env_->graph.commit();
        response.set_status(saengra_api::CommitResponse_Status_OK);
        spdlog::info("Committed — no observations from graph");
        return;
    }

    auto proto_observations = current_env_->observer_container.observe_proto(graph_observations);
    if (proto_observations.empty()) {
        current_env_->graph.commit();
        response.set_status(saengra_api::CommitResponse_Status_OK);
        spdlog::info("Committed — no observations from observers");
        return;
    }

    *response.mutable_observation() = {proto_observations.begin(), proto_observations.end()};
    response.set_status(saengra_api::CommitResponse_Status_REQUIRES_REPEATED_COMMIT);
    spdlog::info("Applied changes — got {} feedback observations", BRIGHT_WHITE(proto_observations.size()));
}

void Worker::handleRollback(const saengra_api::Rollback& request, saengra_api::RollbackResponse& response) {
    spdlog::info("{}()", CYAN("Rollback"));
    if (current_env_ == nullptr) {
        response.set_status(saengra_api::RollbackResponse_Status_NOT_CONNECTED);
        spdlog::error("Can't rollback — not connected");
        return;
    }

    current_env_->rollback();
    response.set_status(saengra_api::RollbackResponse_Status_OK);
}

void Worker::handleObserve(const saengra_api::Observe& request, saengra_api::ObserveResponse& response) {
    spdlog::info("{}({} observers)", CYAN("Observe"), BRIGHT_WHITE(request.observers_size()));
    if (current_env_ == nullptr) {
        response.set_status(saengra_api::ObserveResponse_Status_NOT_CONNECTED);
        spdlog::error("Can't observe — not connected");
        return;
    }

    for (const auto& observer : request.observers()) {
        current_env_->observer_container.register_(observer);
    }
    response.set_status(saengra_api::ObserveResponse_Status_OK);
}

void Worker::handleFind(const saengra_api::Find& request, saengra_api::FindResponse& response) {
    spdlog::info("{}(vertices={}, edges={})", CYAN("Find"), BRIGHT_WHITE(request.vertices()), BRIGHT_WHITE(request.edges()));

    if (current_env_ == nullptr) {
        response.set_status(saengra_api::FindResponse_Status_NOT_CONNECTED);
        spdlog::error("Can't find — not connected");
        return;
    }

    // Find vertices
    if (request.vertices()) {
        const auto& vertices = current_env_->graph.get_vertices();

        VertexIDRange vertex_ids = request.has_with_type_name()
            ? vertices.iter_present_by_type_name(current_env_->graph.internalize_type_name(request.with_type_name()))
            : vertices.iter_present();

        for (VertexID vertex_id : vertex_ids) {
            const auto& vertex = *vertex_id;
            auto* proto_vertex = response.add_vertices();
            proto_vertex->set_type_name(*vertex.type_name.text);
            proto_vertex->set_value(vertex.value);
        }

        spdlog::info("Found {} vertices", response.vertices_size());
    }

    // Find edges
    if (request.edges()) {
        const auto& vertices = current_env_->graph.get_vertices();
        const auto& edges_container = current_env_->graph.get_edges();

        std::vector<Edge> edges;

        // Determine which edges to query based on from/to filters
        if (request.from_size() > 0 && request.to_size() > 0) {
            // Query from both directions and intersect
            std::unordered_set<Edge> edge_set;

            // Add edges from "from" vertices
            for (const auto& proto_from : request.from()) {
                VertexTypeName type_name = current_env_->graph.internalize_type_name(proto_from.type_name());
                VertexData from_data{type_name, proto_from.value()};
                auto from_id = vertices.get_vertex_id(from_data);
                if (!from_id.has_value()) continue;

                std::vector<Edge> from_edges;
                if (request.has_with_label()) {
                    EdgeLabel label = current_env_->graph.internalize_label(request.with_label());
                    from_edges = edges_container.iter_present_from_vertex_via_labels(*from_id, {label});
                } else {
                    from_edges = edges_container.iter_present_from_vertex(*from_id);
                }
                edge_set.insert(from_edges.begin(), from_edges.end());
            }

            // Filter by "to" vertices
            std::vector<Edge> filtered_edges;
            for (const auto& edge : edge_set) {
                for (const auto& proto_to : request.to()) {
                    VertexTypeName to_type = current_env_->graph.internalize_type_name(proto_to.type_name());
                    VertexData to_data{to_type, proto_to.value()};
                    const auto& to_vertex = *edge.to;
                    if (to_vertex.type_name == to_type && to_vertex.value == proto_to.value()) {
                        filtered_edges.push_back(edge);
                        break;
                    }
                }
            }
            edges = std::move(filtered_edges);
        } else if (request.from_size() > 0) {
            // Query only from "from" vertices
            for (const auto& proto_from : request.from()) {
                VertexTypeName type_name = current_env_->graph.internalize_type_name(proto_from.type_name());
                VertexData from_data{type_name, proto_from.value()};
                auto from_id = vertices.get_vertex_id(from_data);
                if (!from_id.has_value()) continue;

                std::vector<Edge> from_edges;
                if (request.has_with_label()) {
                    EdgeLabel label = current_env_->graph.internalize_label(request.with_label());
                    from_edges = edges_container.iter_present_from_vertex_via_labels(*from_id, {label});
                } else {
                    from_edges = edges_container.iter_present_from_vertex(*from_id);
                }
                edges.insert(edges.end(), from_edges.begin(), from_edges.end());
            }
        } else if (request.to_size() > 0) {
            // Query only from "to" vertices
            for (const auto& proto_to : request.to()) {
                VertexTypeName type_name = current_env_->graph.internalize_type_name(proto_to.type_name());
                VertexData to_data{type_name, proto_to.value()};
                auto to_id = vertices.get_vertex_id(to_data);
                if (!to_id.has_value()) continue;

                std::vector<Edge> to_edges;
                if (request.has_with_label()) {
                    EdgeLabel label = current_env_->graph.internalize_label(request.with_label());
                    to_edges = edges_container.iter_present_to_vertex_via_labels(*to_id, {label});
                } else {
                    to_edges = edges_container.iter_present_to_vertex(*to_id);
                }
                edges.insert(edges.end(), to_edges.begin(), to_edges.end());
            }
        } else {
            // No from/to filters, query all edges with optional label filter
            if (request.has_with_label()) {
                EdgeLabel label = current_env_->graph.internalize_label(request.with_label());
                edges = edges_container.iter_present_via_labels({label});
            } else {
                edges = edges_container.iter_present();
            }
        }

        // Convert edges to protobuf
        for (const auto& edge : edges) {
            const auto& from_vertex = *edge.from;
            const auto& to_vertex = *edge.to;
            auto* proto_edge = response.add_edges();
            proto_edge->mutable_from()->set_type_name(*from_vertex.type_name.text);
            proto_edge->mutable_from()->set_value(from_vertex.value);
            proto_edge->set_label(*edge.label.text);
            proto_edge->mutable_to()->set_type_name(*to_vertex.type_name.text);
            proto_edge->mutable_to()->set_value(to_vertex.value);
        }
        spdlog::info("Found {} edges", response.edges_size());
    }

    response.set_status(saengra_api::FindResponse_Status_OK);
}

void Worker::handleMatch(const saengra_api::Match& request, saengra_api::MatchResponse& response) {
    spdlog::info("{}(expression='{}')", CYAN("Match"), request.expression());

    if (current_env_ == nullptr) {
        response.set_status(saengra_api::MatchResponse_Status_NOT_CONNECTED);
        spdlog::error("Can't match — not connected");
        return;
    }

    // Parse the expression
    Parser parser(current_env_->graph);
    Expression expr = parser.parse(request.expression());

    // Convert placeholder values from protobuf to VertexData
    PlaceholderValues placeholder_values;
    placeholder_values.reserve(request.placeholder_values_size());
    for (const auto& proto_vertex : request.placeholder_values()) {
        VertexTypeName type_name = current_env_->graph.internalize_type_name(proto_vertex.type_name());
        VertexValue value = proto_vertex.value();
        placeholder_values.emplace_back(type_name, value);
    }

    // Create the query
    Query query{current_env_->graph, expr, placeholder_values};

    // Find start positions
    StartPositionFinder finder;
    StartPositions start_positions = finder.find_start_positions(query);

    // Perform the match
    Matcher matcher;
    QuerySet result = matcher.match(query, start_positions);

    // Convert results to protobuf
    const auto& vertices_container = current_env_->graph.get_vertices();
    for (const Subgraph& subgraph : result.subgraphs) {
        auto* proto_subgraph = response.add_subgraphs();

        // Set start position
        auto* proto_start = proto_subgraph->mutable_start_position();
        const auto start_vertex = *subgraph.start_position.vertex_id;
        proto_start->mutable_vertex()->set_type_name(*start_vertex.type_name.text);
        proto_start->mutable_vertex()->set_value(start_vertex.value);
        proto_start->set_kind(subgraph.start_position.kind == PositionKind::CORE
            ? saengra_api::Position_PositionKind_CORE
            : saengra_api::Position_PositionKind_ORBIT);

        // Set end positions
        for (const Position& end_pos : subgraph.end_positions) {
            auto* proto_end = proto_subgraph->add_end_positions();
            const auto end_vertex = *end_pos.vertex_id;
            proto_end->mutable_vertex()->set_type_name(*end_vertex.type_name.text);
            proto_end->mutable_vertex()->set_value(end_vertex.value);
            proto_end->set_kind(end_pos.kind == PositionKind::CORE
                ? saengra_api::Position_PositionKind_CORE
                : saengra_api::Position_PositionKind_ORBIT);
        }

        // Set vertices
        for (VertexID vid : subgraph.vertices) {
            const auto vertex = *vid;
            auto* proto_vertex = proto_subgraph->add_vertices();
            proto_vertex->set_type_name(*vertex.type_name.text);
            proto_vertex->set_value(vertex.value);
        }

        // Set edges
        for (const Edge& edge : subgraph.edges) {
            const auto& from_vertex = *edge.from;
            const auto& to_vertex = *edge.to;
            auto* proto_edge = proto_subgraph->add_edges();
            proto_edge->mutable_from()->set_type_name(*from_vertex.type_name.text);
            proto_edge->mutable_from()->set_value(from_vertex.value);
            proto_edge->set_label(*edge.label.text);
            proto_edge->mutable_to()->set_type_name(*to_vertex.type_name.text);
            proto_edge->mutable_to()->set_value(to_vertex.value);
        }

        // Set refs
        for (const auto& [name, vid] : subgraph.refs) {
            const auto& ref_vertex = *vid;
            auto& proto_refs = *proto_subgraph->mutable_refs();
            proto_refs[name].set_type_name(*ref_vertex.type_name.text);
            proto_refs[name].set_value(ref_vertex.value);
        }
    }

    spdlog::info("Matched {} subgraphs", response.subgraphs_size());
    response.set_status(saengra_api::MatchResponse_Status_OK);
}

std::string Worker::receiveMessage() {
    // Read message length (4 bytes)
    uint32_t msg_length;
    ssize_t bytes_read = read(client_fd_, &msg_length, sizeof(msg_length));
    if (bytes_read != sizeof(msg_length)) {
        return "";
    }

    // Read message data
    std::vector<char> buffer(msg_length);
    size_t total_read = 0;
    while (total_read < msg_length) {
        bytes_read = read(client_fd_, buffer.data() + total_read, msg_length - total_read);
        if (bytes_read <= 0) {
            return "";
        }
        total_read += bytes_read;
    }

    return std::string(buffer.data(), msg_length);
}

bool Worker::sendMessage(const std::string& message) {
    // Send message length
    uint32_t msg_length = message.size();
    ssize_t bytes_written = write(client_fd_, &msg_length, sizeof(msg_length));
    if (bytes_written != sizeof(msg_length)) {
        return false;
    }

    // Send message data
    size_t total_written = 0;
    while (total_written < message.size()) {
        bytes_written = write(client_fd_, message.data() + total_written,
                            message.size() - total_written);
        if (bytes_written <= 0) {
            return false;
        }
        total_written += bytes_written;
    }

    return true;
}

bool Worker::sendResponse(const saengra_api::Response& response) {
    std::string serialized;
    if (!response.SerializeToString(&serialized)) {
        spdlog::error("Failed to serialize Response message");
        return false;
    }
    return sendMessage(serialized);
}

} // namespace saengra
