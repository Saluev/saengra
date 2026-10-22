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

GraphChanges Graph::apply() {
    if (!vertices_.has_unapplied_changes() && !edges_.has_unapplied_changes()) return {};

    GraphChanges changes{
        vertices_.iter_just_added(),
        vertices_.iter_just_removed(),
        edges_.iter_just_added(),
        edges_.iter_just_removed(),
    };
    vertices_.apply();
    edges_.apply();
    return changes;
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
