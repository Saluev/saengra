#pragma once

#include "vertex.h"
#include "edge.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_set.h"
#include <boost/variant.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace saengra {

/* Leaves store targets of edges with a particular label from (or, in the inverse trunk, to)
 * a particular vertex. They only hold the current state: uncommitted changes are tracked
 * by MutableEdgesContainer's undo log. */

class UniLeaf;
class MultiLeaf;
using Leaf = boost::variant<UniLeaf, MultiLeaf>;

class UniLeaf {
public:
    // Returns whether the edge was actually added (removed).
    bool add(VertexID to, Leaf& self);
    bool remove(VertexID to);

    inline bool is_empty() const { return to_ == nullptr; }
    inline bool contains(VertexID to) const { return to_ == to; }

    template <typename F>
    void for_each(F&& f) const {
        if (to_) f(to_);
    }

private:
    VertexID to_ = nullptr;
};

class MultiLeaf {
public:
    MultiLeaf(VertexID first, VertexID second) : targets_{first, second} {}

    bool add(VertexID to, Leaf& self);
    bool remove(VertexID to);

    inline bool is_empty() const { return targets_.empty(); }
    inline bool contains(VertexID to) const { return targets_.contains(to); }

    template <typename F>
    void for_each(F&& f) const {
        for (VertexID to : targets_) f(to);
    }

private:
    absl::flat_hash_set<VertexID> targets_;
};

class Branch {
public:
    Branch(VertexID from, bool is_inverse);

    // Modifications; return whether anything actually changed.
    bool add(const EdgeLabel& label, VertexID to);
    bool remove(const EdgeLabel& label, VertexID to);
    void remove_all_via_label(const EdgeLabel& label, std::vector<VertexID>& removed);
    void remove_all(std::vector<std::pair<EdgeLabel, VertexID>>& removed);

    // Iterators
    void iter_present(std::vector<Edge>& dest) const;
    void iter_present_via_label(const EdgeLabel& label, std::vector<Edge>& dest) const;
    void iter_present_to_vertex_via_labels(VertexID to, const std::vector<EdgeLabel>& labels, std::vector<Edge>& dest) const;

    bool is_empty() const { return leaves_.empty(); }

private:
    void push_edge(std::vector<Edge>& dest, const EdgeLabel& label, VertexID to) const;

    VertexID from_;
    bool is_inverse_;
    std::unordered_map<EdgeLabel, Leaf> leaves_;
};

class Trunk {
public:
    Trunk(bool is_inverse);

    // Modifications; return whether anything actually changed. Empty branches are erased.
    bool add(VertexID from, const EdgeLabel& label, VertexID to);
    bool remove(VertexID from, const EdgeLabel& label, VertexID to);
    void remove_all_via_label(VertexID from, const EdgeLabel& label, std::vector<VertexID>& removed);
    void remove_all(VertexID from, std::vector<std::pair<EdgeLabel, VertexID>>& removed);

    // Iterators
    void iter_present(std::vector<Edge>& dest) const;
    void iter_present_via_labels(const std::vector<EdgeLabel>& labels, std::vector<Edge>& dest) const;

    // Access
    const Branch* get_branch(VertexID from) const;

private:
    bool is_inverse_;
    std::unordered_map<VertexID, Branch> branches_;
};

class MutableEdgesContainer {
public:
    MutableEdgesContainer();

    EdgeLabel internalize_label(const std::string& label);

    // Modifications; the forward and inverse trunks are kept in sync, and changes are logged.
    void add_edge(VertexID from, const EdgeLabel& label, VertexID to);
    void remove_edge(VertexID from, const EdgeLabel& label, VertexID to);
    void remove_edges_from_via_label(VertexID from, const EdgeLabel& label);
    void remove_edges_of_vertex(VertexID vertex);

    // Transaction control
    void apply();
    void commit();
    void rollback();
    inline bool has_unapplied_changes() const { return applied_ < log_.size(); }

    // Queries
    std::vector<Edge> iter_present() const;
    /* Usage: for exporting entire graphs via API. */

    std::vector<Edge> iter_present_from_vertex(VertexID from) const;
    std::vector<Edge> iter_present_from_vertex_via_labels(VertexID from, const std::vector<EdgeLabel>& label) const;

    std::vector<Edge> iter_present_to_vertex(VertexID to) const;
    std::vector<Edge> iter_present_to_vertex_via_labels(VertexID from, const std::vector<EdgeLabel>& label) const;

    std::vector<Edge> iter_present_via_labels(const std::vector<EdgeLabel>& labels) const;

    // Edges between two vertices via any of the labels, or via any label if there are none.
    std::vector<Edge> iter_present_from_vertex_to_vertex(VertexID from, VertexID to, const std::vector<EdgeLabel>& labels) const;

    // Net changes since the last apply().
    std::vector<Edge> iter_just_added() const;
    std::vector<Edge> iter_just_removed() const;

private:
    struct Change {
        Edge edge;
        bool added;
    };

    bool insert(VertexID from, const EdgeLabel& label, VertexID to);
    bool erase(VertexID from, const EdgeLabel& label, VertexID to);
    std::vector<Edge> iter_just_changed(bool added) const;

    // Labels never move, so that EdgeLabel can point to them.
    absl::node_hash_set<std::string> labels_;
    Trunk forward_;
    Trunk inverse_;

    // Undo log of all changes since the last commit; log_[applied_:] are not applied yet.
    std::vector<Change> log_;
    size_t applied_ = 0;
};

}
