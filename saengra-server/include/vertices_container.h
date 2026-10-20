#pragma once

#include "absl/hash/hash.h"
#include "vertex.h"
#include "edge.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace saengra {

struct InternalizedTypeName {
    std::unique_ptr<std::string> type_name;

    inline bool operator==(const InternalizedTypeName& other) const {
        return *type_name == *other.type_name;
    }
};

}

namespace std {

template <>
struct hash<saengra::InternalizedTypeName> {
    std::size_t operator()(const saengra::InternalizedTypeName& v) const {
        return std::hash<std::string>{}(*v.type_name);
    }
};

}

namespace saengra {

using VertexIDRange = const std::unordered_set<VertexID>&;
using VertexIDSet = std::unordered_set<VertexID>;

class MutableVerticesContainer {
public:
    explicit MutableVerticesContainer();

    VertexTypeName internalize_type_name(const std::string& type_name);
    inline size_t num_internalized_type_names() const { return type_names.size(); }

    void add_vertex(const VertexData& v);
    std::optional<VertexID> get_vertex_id(const VertexData& v) const;
    void discard_vertex(const VertexData& v);

    void apply(void);
    void commit(void);
    void rollback(void);
    inline bool has_unapplied_changes() const { return applied_ < log_.size(); }

    VertexIDRange iter_present() const;
    VertexIDRange iter_present_by_type_name(const VertexTypeName& type_name) const;

    // Net changes since the last apply().
    std::vector<VertexID> iter_just_added(void) const;
    std::vector<VertexID> iter_just_removed(void) const;

private:
    struct Change {
        VertexID vertex;
        bool added;
    };

    void set_present(VertexID vertex_id, bool present);
    std::vector<VertexID> iter_just_changed(bool added) const;

    std::unordered_map<VertexData, std::unique_ptr<StoredVertex>> vertex_id_by_vertex;
    std::unordered_set<InternalizedTypeName> type_names;
    VertexIDSet present;
    std::unordered_map<VertexTypeName, VertexIDSet> vertices_by_type_name;

    // Undo log of all changes since the last commit; log_[applied_:] are not applied yet.
    std::vector<Change> log_;
    size_t applied_ = 0;
};

}
