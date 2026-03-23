#pragma once

#include "vertex.h"
#include "edge.h"
#include <boost/dynamic_bitset.hpp>
#include <boost/variant.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace saengra {

using JustAdded = bool;
using JustRemoved = bool;

//using LeafIndex = std::variant<
//    std::map<VertexID, size_t>,
//    std::unordered_map<VertexID, size_t>,
//>;

using LeafIndex = std::map<VertexID, size_t>;

class UniLeaf;
class MultiLeaf;
using Leaf = boost::variant<UniLeaf, MultiLeaf>;

class UniLeaf {
public:
    UniLeaf() = default;

    // Iterators
    void iter_present(std::vector<VertexID>& dest) const;
    void iter_just_added(std::vector<VertexID>& dest) const;
    void iter_just_removed(std::vector<VertexID>& dest) const;

    void iter_present(std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;
    void iter_just_added(std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;
    void iter_just_removed(std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;

    void iter_present(std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;
    void iter_just_added(std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;
    void iter_just_removed(std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;

    // Transaction control
    void apply(Leaf& self);
    void commit(Leaf& self);
    void rollback();

    // Modifications
    JustAdded add_to_leaf(VertexID to, Leaf& self);
    JustRemoved discard_from_leaf(VertexID to, Leaf& self);
    JustRemoved remove_all_from_leaf(Leaf& self);

    // Check if empty
    inline bool is_committed_empty() const { return !committed_; }
    inline bool is_present_empty() const { return !just_replaced_; }
private:
    void convert_to_multi_leaf(Leaf& self);

    std::optional<VertexID> committed_;
    std::optional<VertexID> replaced_;
    std::optional<VertexID> just_replaced_;
};

struct MultiLeafTxData {
    boost::dynamic_bitset<> committed_;
    boost::dynamic_bitset<> added_;
    boost::dynamic_bitset<> removed_;
    boost::dynamic_bitset<> just_added_;
    boost::dynamic_bitset<> just_removed_;
};

class MultiLeaf {
public:
    MultiLeaf():
        tx_(std::make_unique<MultiLeafTxData>()) {}

    MultiLeaf(const MultiLeaf& other):
        index_(other.index_), present_(other.present_),
        tx_(std::make_unique<MultiLeafTxData>(*other.tx_)) {}

    MultiLeaf& operator=(const MultiLeaf& other) {
        // Copy constructor is required by boost::variant. See edges_container.h comment.
        if (this != &other) {
            index_ = other.index_;
            present_ = other.present_;
            *tx_ = *other.tx_;
        }
        return *this;
    }

    MultiLeaf(MultiLeaf&&) = default;
    MultiLeaf& operator=(MultiLeaf&&) = default;

    // Iterators
    void iter_present(std::vector<VertexID>& dest) const;
    void iter_just_added(std::vector<VertexID>& dest) const;
    void iter_just_removed(std::vector<VertexID>& dest) const;

    void iter_present(std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;
    void iter_just_added(std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;
    void iter_just_removed(std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;

    void iter_present(std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;
    void iter_just_added(std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;
    void iter_just_removed(std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;

    // Transaction control
    void apply(Leaf& self);
    void commit(Leaf& self);
    void rollback();

    // Modifications
    JustAdded add_to_leaf(VertexID to, Leaf& self);
    JustRemoved discard_from_leaf(VertexID to, Leaf& self);
    JustRemoved remove_all_from_leaf(Leaf& self);

    // Check if empty
    inline bool is_committed_empty() const { return tx_->committed_.none(); }
    inline bool is_present_empty() const { return present_.none(); }

private:
    void iter_bitmask(const boost::dynamic_bitset<>& bitmask, std::vector<VertexID>& dest) const;
    void iter_bitmask(const boost::dynamic_bitset<>& bitmask, std::vector<std::pair<EdgeLabel, VertexID>>& dest, EdgeLabel label) const;
    void iter_bitmask(const boost::dynamic_bitset<>& bitmask, std::vector<Edge>& dest, VertexID from, EdgeLabel label, bool is_inverse) const;
    void compactify_after_commit();

    LeafIndex index_;  // Maps VertexID to index in the bitsets
    boost::dynamic_bitset<> present_;

    // The rest of the fields (needed mostly during graph updates) are stored separately
    // to make UniLeaf and MultiLeaf be roughly the same size and avoid excessive memory allocation.
    std::unique_ptr<MultiLeafTxData> tx_;

    friend class UniLeaf;
};

// Forward declaration
class Trunk;

class Branch {
public:
    Branch(VertexID from, bool is_inverse);

    // Transaction control
    void apply();
    void commit();
    void rollback();

    // Inspecting
    inline Leaf& get_or_create_leaf(const EdgeLabel& label) {
        auto [it, inserted] = leaves_.try_emplace(label, UniLeaf{});
        return it->second;
    }

    inline Leaf* get_leaf_or_null(const EdgeLabel& label) {
        auto it = leaves_.find(label);
        if (it == leaves_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // Modifications
    void iter_present_and_remove(std::vector<std::pair<EdgeLabel, VertexID>>& dest);
    void iter_present_and_remove(std::vector<Edge>& dest);

    // Iterators
    std::vector<EdgeLabel> iter_present_labels() const;

    void iter_present(std::vector<std::pair<EdgeLabel, VertexID>>& dest) const;
    void iter_just_added(std::vector<std::pair<EdgeLabel, VertexID>>& dest) const;
    void iter_just_removed(std::vector<std::pair<EdgeLabel, VertexID>>& dest) const;
    void iter_present_via_label(const EdgeLabel& label, std::vector<std::pair<EdgeLabel, VertexID>>& dest) const;

    void iter_present(std::vector<Edge>& dest) const;
    void iter_just_added(std::vector<Edge>& dest) const;
    void iter_just_removed(std::vector<Edge>& dest) const;
    void iter_present_via_label(const EdgeLabel& label, std::vector<Edge>& dest) const;

    // Check if empty
    bool is_empty() const { return leaves_.empty(); }

private:
    VertexID from_;
    bool is_inverse_;
    std::unordered_map<EdgeLabel, Leaf> leaves_;
};

class Trunk {
public:
    Trunk(bool is_inverse);

    // Set inverse relationship
    void set_inverse(Trunk* inverse) { inverse_ = inverse; }

    // Transaction control
    void apply();
    void commit();
    void rollback();

    // Inspecting
    inline Branch& get_or_create_branch(VertexID from) {
        auto [it, inserted] = branches_.try_emplace(from, from, is_inverse_);
        return it->second;
    }

    inline Branch* get_branch_or_null(VertexID from) {
        auto it = branches_.find(from);
        if (it == branches_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    // Modifications
    inline void mark_just_added(VertexID from, Branch* branch) {
        have_just_added_[from] = branch;
    }

    inline void mark_just_removed(VertexID from, Branch* branch) {
        have_just_removed_[from] = branch;
    }

    // Iterators
    void iter_just_added(std::vector<Edge>& dest) const;
    void iter_just_removed(std::vector<Edge>& dest) const;
    void iter_present(std::vector<Edge>& dest) const;
    void iter_present_via_labels(const std::vector<EdgeLabel>& labels, std::vector<Edge>& dest) const;

    // Access
    Branch* get_branch(VertexID from);
    const Branch* get_branch(VertexID from) const;

private:
    bool is_inverse_;
    std::unordered_map<VertexID, Branch> branches_;
    std::unordered_map<VertexID, Branch*> have_added_;
    std::unordered_map<VertexID, Branch*> have_removed_;
    std::unordered_map<VertexID, Branch*> have_just_added_;
    std::unordered_map<VertexID, Branch*> have_just_removed_;
    Trunk* inverse_;
};

struct InternalizedEdgeLabel {
    std::unique_ptr<std::string> label_;

    inline bool operator==(const InternalizedEdgeLabel& other) const {
        return *label_ == *other.label_;
    }
};

}


namespace std {

template <>
struct hash<saengra::InternalizedEdgeLabel> {
    std::size_t operator()(const saengra::InternalizedEdgeLabel& v) const {
        return std::hash<std::string>{}(*v.label_);
    }
};

}

namespace saengra {

class MutableEdgesContainer {
public:
    MutableEdgesContainer();

    EdgeLabel internalize_label(const std::string& label);

    // Transaction control
    void apply();
    void commit();
    void rollback();

    // Inspecting
    inline Trunk& get_forward_trunk() { return forward_; }
    inline Trunk& get_inverse_trunk() { return inverse_; }

    // Queries
    std::vector<Edge> iter_present() const;
    /* Usage: for exporting entire graphs via API. */

    std::vector<Edge> iter_present_from_vertex(VertexID from) const;
    std::vector<Edge> iter_present_from_vertex_via_labels(VertexID from, const std::vector<EdgeLabel>& label) const;

    std::vector<Edge> iter_present_to_vertex(VertexID to) const;
    std::vector<Edge> iter_present_to_vertex_via_labels(VertexID from, const std::vector<EdgeLabel>& label) const;

    std::vector<Edge> iter_present_via_labels(const std::vector<EdgeLabel>& labels) const;
    std::vector<Edge> iter_just_added() const;
    std::vector<Edge> iter_just_removed() const;

private:
    std::unordered_set<InternalizedEdgeLabel> labels_;
    Trunk forward_;
    Trunk inverse_;
};

}
