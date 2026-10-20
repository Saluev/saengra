#include "debug.h"
#include "graph.h"

namespace saengra {

Graph::Graph() = default;

VertexData Graph::convert_vertex(const ::saengra_api::Vertex& source) {
    const auto type_name = vertices_.internalize_type_name(source.type_name());
    return VertexData(type_name, source.value());
}

// Update accessor traits for proto and native update types.

struct ProtoUpdateAccessor {
    using UpdateType = ::saengra_api::ApplyUpdates_Update;

    static inline auto kind(const UpdateType& u) { return u.kind(); }
    static inline bool is_add_vertex(const UpdateType& u) { return u.kind() == ProtoUpdateKind::AddVertex; }
    static inline bool is_add_edge(const UpdateType& u) { return u.kind() == ProtoUpdateKind::AddEdge; }
    static inline bool is_remove_vertex(const UpdateType& u) { return u.kind() == ProtoUpdateKind::RemoveVertex; }
    static inline bool is_remove_edge(const UpdateType& u) { return u.kind() == ProtoUpdateKind::RemoveEdge; }
    static inline bool is_remove_edges_to_all(const UpdateType& u) { return u.kind() == ProtoUpdateKind::RemoveEdgesToAll; }

    static inline VertexData convert_from(const UpdateType& u, MutableVerticesContainer& vertices) {
        const auto type_name = vertices.internalize_type_name(u.from().type_name());
        return VertexData(type_name, u.from().value());
    }
    static inline VertexData convert_to(const UpdateType& u, MutableVerticesContainer& vertices) {
        const auto type_name = vertices.internalize_type_name(u.to().type_name());
        return VertexData(type_name, u.to().value());
    }
    static inline EdgeLabel convert_label(const UpdateType& u, MutableEdgesContainer& edges) {
        return edges.internalize_label(u.label());
    }
};

struct NativeUpdateAccessor {
    using UpdateType = NativeUpdate;

    static inline bool is_add_vertex(const UpdateType& u) { return u.kind == NativeUpdateKind::AddVertex; }
    static inline bool is_add_edge(const UpdateType& u) { return u.kind == NativeUpdateKind::AddEdge; }
    static inline bool is_remove_vertex(const UpdateType& u) { return u.kind == NativeUpdateKind::RemoveVertex; }
    static inline bool is_remove_edge(const UpdateType& u) { return u.kind == NativeUpdateKind::RemoveEdge; }
    static inline bool is_remove_edges_to_all(const UpdateType& u) { return u.kind == NativeUpdateKind::RemoveEdgesToAll; }

    static inline VertexData convert_from(const UpdateType& u, MutableVerticesContainer& vertices) {
        const auto type_name = vertices.internalize_type_name(u.from.type_name);
        return VertexData(type_name, u.from.value);
    }
    static inline VertexData convert_to(const UpdateType& u, MutableVerticesContainer& vertices) {
        const auto type_name = vertices.internalize_type_name(u.to.type_name);
        return VertexData(type_name, u.to.value);
    }
    static inline EdgeLabel convert_label(const UpdateType& u, MutableEdgesContainer& edges) {
        return edges.internalize_label(u.label);
    }
};


template<>
void Graph::update_impl<ProtoUpdateAccessor, ProtoUpdates>(const ProtoUpdates& updates) {
    using Accessor = ProtoUpdateAccessor;

    if (updates.size() == 0) return;

    for (const auto& update : updates) {
        switch (update.kind()) {
        case ProtoUpdateKind::AddVertex:
            add_vertex_impl<Accessor>(update);
            break;
        case ProtoUpdateKind::RemoveVertex:
            remove_vertex_impl<Accessor>(update);
            break;
        case ProtoUpdateKind::AddEdge:
            add_edge_impl<Accessor>(update);
            break;
        case ProtoUpdateKind::RemoveEdge:
            remove_edge_impl<Accessor>(update);
            break;
        case ProtoUpdateKind::RemoveEdgesToAll:
            remove_edges_to_all_impl<Accessor>(update);
            break;
        }
    }
}


template<>
void Graph::update_impl<NativeUpdateAccessor, NativeUpdates>(const NativeUpdates& updates) {
    using Accessor = NativeUpdateAccessor;

    if (updates.size() == 0) return;

    for (const auto& update : updates) {
        switch (update.kind) {
        case NativeUpdateKind::AddVertex:
            add_vertex_impl<Accessor>(update);
            break;
        case NativeUpdateKind::RemoveVertex:
            remove_vertex_impl<Accessor>(update);
            break;
        case NativeUpdateKind::AddEdge:
            add_edge_impl<Accessor>(update);
            break;
        case NativeUpdateKind::RemoveEdge:
            remove_edge_impl<Accessor>(update);
            break;
        case NativeUpdateKind::RemoveEdgesToAll:
            remove_edges_to_all_impl<Accessor>(update);
            break;
        }
    }
}


template<typename Accessor, typename UpdateT>
void Graph::add_vertex_impl(const UpdateT& update) {
    const auto from = Accessor::convert_from(update, vertices_);
    vertices_.add_vertex(from);
}

template<typename Accessor, typename UpdateT>
void Graph::add_edge_impl(const UpdateT& update) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    const auto to = Accessor::convert_to(update, vertices_);
    const auto to_id = vertices_.get_vertex_id(to);
    if (!to_id.has_value()) return;
    // Removed vertices keep their IDs, so presence has to be checked separately.
    if (!from_id.value()->present || !to_id.value()->present) return;
    const auto label = Accessor::convert_label(update, edges_);
    edges_.add_edge(from_id.value(), label, to_id.value());
}

template<typename Accessor, typename UpdateT>
void Graph::remove_vertex_impl(const UpdateT& update) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    edges_.remove_edges_of_vertex(from_id.value());
    vertices_.discard_vertex(from);
}

template<typename Accessor, typename UpdateT>
void Graph::remove_edge_impl(const UpdateT& update) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    const auto to = Accessor::convert_to(update, vertices_);
    const auto to_id = vertices_.get_vertex_id(to);
    if (!to_id.has_value()) return;
    const auto label = Accessor::convert_label(update, edges_);
    edges_.remove_edge(from_id.value(), label, to_id.value());
}

template<typename Accessor, typename UpdateT>
void Graph::remove_edges_to_all_impl(const UpdateT& update) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    const auto label = Accessor::convert_label(update, edges_);
    edges_.remove_edges_from_via_label(from_id.value(), label);
}

void Graph::update(const ProtoUpdates& updates) {
    update_impl<ProtoUpdateAccessor>(updates);
}

void Graph::update(const NativeUpdates& updates) {
    update_impl<NativeUpdateAccessor>(updates);
}

class ObservationMap : public std::unordered_map<Observable, Observation> {
public:
    Observation& ensure(Observable observable) {
        auto [it, inserted] = try_emplace(observable, observable);
        return it->second;
    }
};

Observations Graph::apply() {
    if (!vertices_.has_unapplied_changes() && !edges_.has_unapplied_changes()) return {};

    // Trigger observers, check if any of them prevents commit from happening.
    ObservationMap observations;
    auto& new_vertex_observation = observations.ensure(NewVertex()); // optimized faster access to observables
    auto& new_edge_observation = observations.ensure(NewEdge());     // that are most likely present

    for (const auto vertex_id : vertices_.iter_just_added()) {
        const auto& v = *vertex_id;
        new_vertex_observation.track_added_vertex(vertex_id);
        observations.ensure(NewVertexOfType(v.type_name)).track_added_vertex(vertex_id);
        observations.ensure(ParticularVertex(VertexData(v.type_name, v.value)));
    }
    for (const auto vertex_id : vertices_.iter_just_removed()) {
        const auto& v = *vertex_id;
        observations.ensure(ParticularVertex(VertexData(v.type_name, v.value)));
    }
    for (const auto& e : edges_.iter_just_added()) {
        new_edge_observation.track_added_edge(e);
        observations.ensure(NewEdgeWithLabel(e.label)).track_added_edge(e);
        observations.ensure(EdgeFrom(e.from));
        observations.ensure(EdgeFromWithLabel(e.from, e.label));
        observations.ensure(EdgeTo(e.to));
        observations.ensure(EdgeToWithLabel(e.to, e.label));
    }
    for (const auto& e : edges_.iter_just_removed()) {
        observations.ensure(EdgeFrom(e.from));
        observations.ensure(EdgeFromWithLabel(e.from, e.label));
        observations.ensure(EdgeTo(e.to));
        observations.ensure(EdgeToWithLabel(e.to, e.label));
    }

    // making sure that optimization doesn't change actual output
    if (new_vertex_observation.added_vertex_ids.size() == 0) {
        observations.erase(NewVertex());
    }
    if (new_edge_observation.added_edges.size() == 0) {
        observations.erase(NewEdge());
    }

    vertices_.apply();
    edges_.apply();

    Observations flat_observations;
    flat_observations.reserve(observations.size());
    for (auto& [observable, observation] : observations) {
        flat_observations.push_back(std::move(observation));
    }
    return flat_observations;
}

void Graph::commit() {
    vertices_.commit();
    edges_.commit();
}

void Graph::rollback() {
    vertices_.rollback();
    edges_.rollback();
}

}
