#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "absl/hash/hash.h"
#include "vertices_container.h"
#include "edges_container.h"
#include "observable.h"
#include "messages.pb.h"

namespace saengra {

namespace ProtoUpdateKind {
    const auto AddVertex = ::saengra_api::ApplyUpdates_Update_UpdateKind::ApplyUpdates_Update_UpdateKind_ADD_VERTEX;
    const auto AddEdge = ::saengra_api::ApplyUpdates_Update_UpdateKind::ApplyUpdates_Update_UpdateKind_ADD_EDGE;
    const auto RemoveVertex = ::saengra_api::ApplyUpdates_Update_UpdateKind::ApplyUpdates_Update_UpdateKind_REMOVE_VERTEX;
    const auto RemoveEdge = ::saengra_api::ApplyUpdates_Update_UpdateKind::ApplyUpdates_Update_UpdateKind_REMOVE_EDGE;
    const auto RemoveEdgesToAll = ::saengra_api::ApplyUpdates_Update_UpdateKind::ApplyUpdates_Update_UpdateKind_REMOVE_EDGES_TO_ALL;
};

using ProtoUpdates = ::google::protobuf::RepeatedPtrField<::saengra_api::ApplyUpdates_Update>;

enum class NativeUpdateKind {
    AddVertex,
    AddEdge,
    RemoveVertex,
    RemoveEdge,
    RemoveEdgesToAll
};

struct NativeUpdateVertex {
    std::string type_name;
    std::string value;
};

struct NativeUpdate {
    NativeUpdateKind kind;
    NativeUpdateVertex from;
    std::string label;
    NativeUpdateVertex to;
};

using NativeUpdates = std::vector<NativeUpdate>;

class Graph {
public:
    explicit Graph();

    inline VertexTypeName internalize_type_name(const std::string& type_name) {
        return vertices_.internalize_type_name(type_name);
    }
    inline EdgeLabel internalize_label(const std::string& label) {
        return edges_.internalize_label(label);
    }
    inline const MutableVerticesContainer& get_vertices() const { return vertices_; }
    inline const MutableEdgesContainer& get_edges() const { return edges_; }

    void update(const ProtoUpdates& updates);
    void update(const NativeUpdates& updates);
    Observations apply();
    void commit();
    void rollback();

private:
    class ApplyUpdatesDirectionalContext {
    public:
        ApplyUpdatesDirectionalContext(Trunk& trunk);
        bool focus_on_branch(VertexID from_id, bool create_branch_if_missing);
        bool focus_on_leaf(EdgeLabel label, bool create_leaf_if_missing);
        std::vector<std::pair<EdgeLabel, VertexID>> collect_all_from_branch_and_remove();
        std::vector<VertexID> collect_all_from_leaf();
        void remove_all_from_leaf();
        void remove_edge(VertexID to_id);
        void add_edge(VertexID to_id);
        inline void mark_branch_as_just_added() { trunk_.mark_just_added(last_vertex_id_, last_branch_); }
        inline void mark_branch_as_just_removed() { trunk_.mark_just_removed(last_vertex_id_, last_branch_); }
    private:
        Trunk& trunk_;

        VertexID last_vertex_id_ = nullptr;
        Branch* last_branch_ = nullptr;
        const std::string* last_label_text_ = nullptr;
        MultiLeaf* last_leaf_ = nullptr;
    };

    class ApplyUpdatesContext {
    public:
        ApplyUpdatesContext(MutableVerticesContainer& vertices, MutableEdgesContainer& edges);
        ApplyUpdatesDirectionalContext forward;
        ApplyUpdatesDirectionalContext inverse;
    };

    MutableVerticesContainer vertices_;
    MutableEdgesContainer edges_;
    bool has_pending_updates_ = false;

    VertexData convert_vertex(const ::saengra_api::Vertex& source);

    template<typename Accessor, typename UpdatesT>
    void update_impl(const UpdatesT& updates);

    template<typename Accessor, typename UpdateT>
    void add_vertex_impl(const UpdateT& update, ApplyUpdatesContext& context);

    template<typename Accessor, typename UpdateT>
    void add_edge_impl(const UpdateT& update, ApplyUpdatesContext& context);

    template<typename Accessor, typename UpdateT>
    void remove_vertex_impl(const UpdateT& update, ApplyUpdatesContext& context);

    template<typename Accessor, typename UpdateT>
    void remove_edge_impl(const UpdateT& update, ApplyUpdatesContext& context);

    template<typename Accessor, typename UpdateT>
    void remove_edges_to_all_impl(const UpdateT& update, ApplyUpdatesContext& context);
};

}
