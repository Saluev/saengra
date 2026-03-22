#include "debug.h"
#include "graph.h"

namespace saengra {

Graph::Graph() = default;

Graph::ApplyUpdatesDirectionalContext::ApplyUpdatesDirectionalContext(Trunk& trunk):
    trunk_(trunk) {
}

bool Graph::ApplyUpdatesDirectionalContext::focus_on_branch(VertexID from_id, bool create_branch_if_missing) {
    if (last_vertex_id_ != from_id) {
        last_vertex_id_ = from_id;
        last_branch_ = create_branch_if_missing ? &trunk_.get_or_create_branch(from_id) : trunk_.get_branch_or_null(from_id);
        last_label_text_ = nullptr;
        last_leaf_ = nullptr;
    } else if (create_branch_if_missing && !last_branch_) {
        last_branch_ = &trunk_.get_or_create_branch(from_id);
    }
    return last_branch_;
}

bool Graph::ApplyUpdatesDirectionalContext::focus_on_leaf(EdgeLabel label, bool create_leaf_if_missing) {
    // Here we assume that branch is already correctly focused.
    if (last_label_text_ != label.text) {
        last_label_text_ = label.text;
        last_leaf_ = create_leaf_if_missing ? &last_branch_->get_or_create_leaf(label) : last_branch_->get_leaf_or_null(label);
    } else if (create_leaf_if_missing && !last_leaf_) {
        last_leaf_ = &last_branch_->get_or_create_leaf(label);
    }
    return last_leaf_;
}

std::vector<std::pair<EdgeLabel, VertexID>> Graph::ApplyUpdatesDirectionalContext::collect_all_from_branch_and_remove() {
    static const std::vector<std::pair<EdgeLabel, VertexID>> no_edges;
    if (last_branch_) {
        std::vector<std::pair<EdgeLabel, VertexID>> result;
        last_branch_->iter_present_and_remove(result);
        if (result.size() > 0) {
            mark_branch_as_just_removed();
        }
        return result;
    }
    return no_edges;
}

std::vector<VertexID> Graph::ApplyUpdatesDirectionalContext::collect_all_from_leaf() {
    static const std::vector<VertexID> no_edges;
    if (!last_leaf_) return no_edges;

    std::vector<VertexID> result;
    boost::apply_visitor([&result](auto& l) { l.iter_present(result); }, *last_leaf_);
    return result;
}

void Graph::ApplyUpdatesDirectionalContext::remove_all_from_leaf() {
    if (boost::apply_visitor([this](auto& l) { return l.remove_all_from_leaf(*last_leaf_); }, *last_leaf_)) {
        mark_branch_as_just_removed();
    }
}

void Graph::ApplyUpdatesDirectionalContext::remove_edge(VertexID to_id) {
    if (boost::apply_visitor([to_id, this](auto& l) { return l.discard_from_leaf(to_id, *last_leaf_); }, *last_leaf_)) {
        mark_branch_as_just_removed();
    }
}

void Graph::ApplyUpdatesDirectionalContext::add_edge(VertexID to_id) {
    // Here we assume that branch and leaf are correctly focused.
    if (boost::apply_visitor([to_id, this](auto& l) { return l.add_to_leaf(to_id, *last_leaf_); }, *last_leaf_)) {
        mark_branch_as_just_added();
    }
}

Graph::ApplyUpdatesContext::ApplyUpdatesContext(MutableVerticesContainer& vertices, MutableEdgesContainer& edges):
    forward(edges.get_forward_trunk()), inverse(edges.get_inverse_trunk()) {
}

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
    has_pending_updates_ = true;

    ApplyUpdatesContext context(vertices_, edges_);
    for (const auto& update : updates) {
        switch (update.kind()) {
        case ProtoUpdateKind::AddVertex:
            add_vertex_impl<Accessor>(update, context);
            break;
        case ProtoUpdateKind::RemoveVertex:
            remove_vertex_impl<Accessor>(update, context);
            break;
        case ProtoUpdateKind::AddEdge:
            add_edge_impl<Accessor>(update, context);
            break;
        case ProtoUpdateKind::RemoveEdge:
            remove_edge_impl<Accessor>(update, context);
            break;
        case ProtoUpdateKind::RemoveEdgesToAll:
            remove_edges_to_all_impl<Accessor>(update, context);
            break;
        }
    }
}


template<>
void Graph::update_impl<NativeUpdateAccessor, NativeUpdates>(const NativeUpdates& updates) {
    using Accessor = NativeUpdateAccessor;

    if (updates.size() == 0) return;
    has_pending_updates_ = true;

    ApplyUpdatesContext context(vertices_, edges_);
    for (const auto& update : updates) {
        switch (update.kind) {
        case NativeUpdateKind::AddVertex:
            add_vertex_impl<Accessor>(update, context);
            break;
        case NativeUpdateKind::RemoveVertex:
            remove_vertex_impl<Accessor>(update, context);
            break;
        case NativeUpdateKind::AddEdge:
            add_edge_impl<Accessor>(update, context);
            break;
        case NativeUpdateKind::RemoveEdge:
            remove_edge_impl<Accessor>(update, context);
            break;
        case NativeUpdateKind::RemoveEdgesToAll:
            remove_edges_to_all_impl<Accessor>(update, context);
            break;
        }
    }
}


template<typename Accessor, typename UpdateT>
void Graph::add_vertex_impl(const UpdateT& update, ApplyUpdatesContext& ctx) {
    const auto from = Accessor::convert_from(update, vertices_);
    vertices_.add_vertex(from);
}


template<typename Accessor, typename UpdateT>
void Graph::add_edge_impl(const UpdateT& update, ApplyUpdatesContext& context) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    const auto to = Accessor::convert_to(update, vertices_);
    const auto to_id = vertices_.get_vertex_id(to);
    if (!to_id.has_value()) return;
    const auto label = Accessor::convert_label(update, edges_);

    context.forward.focus_on_branch(from_id.value(), true);
    context.forward.focus_on_leaf(label, true);
    context.forward.add_edge(to_id.value());

    context.inverse.focus_on_branch(to_id.value(), true);
    context.inverse.focus_on_leaf(label, true);
    context.inverse.add_edge(from_id.value());
}

template<typename Accessor, typename UpdateT>
void Graph::remove_vertex_impl(const UpdateT& update, ApplyUpdatesContext& context) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;

    context.forward.focus_on_branch(from_id.value(), false);
    context.inverse.focus_on_branch(from_id.value(), false);
    const auto forward_to_delete = context.forward.collect_all_from_branch_and_remove();
    const auto inverse_to_delete = context.inverse.collect_all_from_branch_and_remove();

    for (const auto& [label, to_id] : inverse_to_delete) {
        context.forward.focus_on_branch(to_id, false);
        context.forward.focus_on_leaf(label, false);
        context.forward.remove_edge(from_id.value());
    }

    for (const auto& [label, to_id] : forward_to_delete) {
        context.inverse.focus_on_branch(to_id, false);
        context.inverse.focus_on_leaf(label, false);
        context.inverse.remove_edge(from_id.value());
    }

    vertices_.discard_vertex(from);
}

template<typename Accessor, typename UpdateT>
void Graph::remove_edge_impl(const UpdateT& update, ApplyUpdatesContext& context) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    const auto to = Accessor::convert_to(update, vertices_);
    const auto to_id = vertices_.get_vertex_id(to);
    if (!to_id.has_value()) return;
    const auto label = Accessor::convert_label(update, edges_);

    if (context.forward.focus_on_branch(from_id.value(), false)) {
        if (context.forward.focus_on_leaf(label, false)) {
            context.forward.remove_edge(to_id.value());
        }
    }

    if (context.inverse.focus_on_branch(to_id.value(), false)) {
        if (context.inverse.focus_on_leaf(label, false)) {
            context.inverse.remove_edge(from_id.value());
        }
    }
}

template<typename Accessor, typename UpdateT>
void Graph::remove_edges_to_all_impl(const UpdateT& update, ApplyUpdatesContext& context) {
    const auto from = Accessor::convert_from(update, vertices_);
    const auto from_id = vertices_.get_vertex_id(from);
    if (!from_id.has_value()) return;
    const auto label = Accessor::convert_label(update, edges_);

    if (!context.forward.focus_on_branch(from_id.value(), false)) return;
    if (!context.forward.focus_on_leaf(label, false)) return;
    const auto forward_to_delete = context.forward.collect_all_from_leaf();
    context.forward.remove_all_from_leaf();

    for (const auto to_id : forward_to_delete) {
        context.inverse.focus_on_branch(to_id, false);
        context.inverse.focus_on_leaf(label, false);
        context.inverse.remove_edge(from_id.value());
    }
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
    if (!has_pending_updates_) return {};

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
    has_pending_updates_ = false;

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
    has_pending_updates_ = false;
}

}
