#include "edges_container.h"
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace saengra {

MultiLeaf::MultiLeaf(VertexID from, const EdgeLabel& label, bool is_inverse) : from_(from), label_(label), is_inverse_(is_inverse) {}

void MultiLeaf::iter_bitmask(const boost::dynamic_bitset<>& bitmask, std::vector<VertexID>& result) const {
    if (bitmask.none()) {
        return;
    }
    for (const auto& [to, idx] : index_) {
        if (idx < bitmask.size() && bitmask[idx]) {
            result.push_back(to);
        }
    }
}

void MultiLeaf::iter_bitmask(const boost::dynamic_bitset<>& bitmask, std::vector<std::pair<EdgeLabel, VertexID>>& result) const {
    if (bitmask.none()) {
        return;
    }
    for (const auto& [to, idx] : index_) {
        if (idx < bitmask.size() && bitmask[idx]) {
            result.emplace_back(label_, to);
        }
    }
}

void MultiLeaf::iter_bitmask(const boost::dynamic_bitset<>& bitmask, std::vector<Edge>& result) const {
    if (bitmask.none()) {
        return;
    }
    for (const auto& [to, idx] : index_) {
        if (idx < bitmask.size() && bitmask[idx]) {
            if (is_inverse_) {
                result.emplace_back(to, label_, from_);
            } else {
                result.emplace_back(from_, label_, to);
            }
        }
    }
}

void MultiLeaf::iter_present(std::vector<VertexID>& dest) const {
    return iter_bitmask(present_, dest);
}

void MultiLeaf::iter_just_added(std::vector<VertexID>& dest) const {
    return iter_bitmask(just_added_, dest);
}

void MultiLeaf::iter_just_removed(std::vector<VertexID>& dest) const {
    return iter_bitmask(just_removed_, dest);
}

void MultiLeaf::iter_present(std::vector<std::pair<EdgeLabel, VertexID>>& dest) const {
    return iter_bitmask(present_, dest);
}

void MultiLeaf::iter_just_added(std::vector<std::pair<EdgeLabel, VertexID>>& dest) const {
    return iter_bitmask(just_added_, dest);
}

void MultiLeaf::iter_just_removed(std::vector<std::pair<EdgeLabel, VertexID>>& dest) const {
    return iter_bitmask(just_removed_, dest);
}

void MultiLeaf::iter_present(std::vector<Edge>& dest) const {
    return iter_bitmask(present_, dest);
}

void MultiLeaf::iter_just_added(std::vector<Edge>& dest) const {
    return iter_bitmask(just_added_, dest);
}

void MultiLeaf::iter_just_removed(std::vector<Edge>& dest) const {
    return iter_bitmask(just_removed_, dest);
}

void MultiLeaf::apply(Leaf& self) {
    boost::dynamic_bitset<> temp = just_added_ & ~committed_;
    added_ ^= temp;
    added_ &= ~just_removed_;
    removed_ |= (just_removed_ & committed_);
    removed_ &= ~just_added_;
    just_added_.reset();
    just_removed_.reset();
}

void MultiLeaf::commit(Leaf& self) {
    committed_ |= added_;
    committed_ &= ~removed_;
    added_.reset();
    removed_.reset();
    compactify_after_commit();
}

void MultiLeaf::rollback() {
    added_.reset();
    removed_.reset();
    just_added_.reset();
    just_removed_.reset();
    present_ = committed_;
    compactify_after_commit();
}

void MultiLeaf::compactify_after_commit() {
    size_t useful_space = present_.count();
    if (useful_space == 0) {
        index_.clear();
        // don't care about bitmasks, we'll reuse them
        return;
    }

    size_t wasted_space = index_.size() - useful_space;
    if (wasted_space < 8 || wasted_space < useful_space) {
        return;
    }

    // Rebuild index with compacted indices
    LeafIndex new_index;
    size_t new_idx = 0;

    for (const auto& [to, old_idx] : index_) {
        if (old_idx < present_.size() && present_[old_idx]) {
            new_index[to] = new_idx++;
        }
    }

    index_ = std::move(new_index);

    // Rebuild bitmasks
    size_t new_size = useful_space;
    present_.resize(new_size);
    committed_.resize(new_size);
    added_.resize(new_size);
    removed_.resize(new_size);
    just_added_.resize(new_size);
    just_removed_.resize(new_size);
    present_.set();    // Set all bits to 1
    committed_.set();  // Set all bits to 1
    // other bitmasks are already set to 0
}

JustAdded MultiLeaf::add_to_leaf(VertexID to, Leaf& self) {
    auto it = index_.find(to);
    size_t idx;
    if (it == index_.end()) {
        idx = index_.size();
        index_[to] = idx;
        present_.resize(idx + 1);
        committed_.resize(idx + 1);
        added_.resize(idx + 1);
        removed_.resize(idx + 1);
        just_added_.resize(idx + 1);
        just_removed_.resize(idx + 1);
    } else {
        idx = it->second;
    }
    if (present_[idx]) {
        return false;
    }
    present_[idx] = true;
    if (just_removed_[idx]) {
        just_removed_[idx] = false;
        return false;
    } else {
        just_added_[idx] = true;
        return true;
    }
}

JustRemoved MultiLeaf::discard_from_leaf(VertexID to, Leaf& self) {
    auto it = index_.find(to);
    if (it == index_.end()) {
        return false;
    }
    size_t idx = it->second;
    if (!present_[idx]) {
        return false;
    }
    present_[idx] = false;
    if (just_added_[idx]) {
        just_added_[idx] = false;
        return false;
    } else {
        just_removed_[idx] = true;
        return true;
    }
}

JustRemoved MultiLeaf::remove_all_from_leaf(Leaf& self) {
    just_removed_ |= (present_ & ~just_added_);
    present_.reset();
    just_added_.reset();
    return just_removed_.any();
}

// ============================================================================
// Branch implementation
// ============================================================================

Branch::Branch(VertexID from, bool is_inverse) : from_(from), is_inverse_(is_inverse) {}

void Branch::apply() {
    for (auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&leaf](auto& l) { l.apply(leaf); }, leaf);
    }
}

void Branch::commit() {
    // Iterate with copy to allow safe deletion
    std::vector<EdgeLabel> to_delete;
    for (auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&leaf](auto& l) { l.commit(leaf); }, leaf);
        if (boost::apply_visitor([](auto& l) { return l.is_present_empty(); }, leaf)) {
            to_delete.push_back(label);
        }
    }
    for (const auto& label : to_delete) {
        leaves_.erase(label);
    }
}

void Branch::rollback() {
    std::vector<EdgeLabel> to_delete;
    for (auto& [label, leaf] : leaves_) {
        if (boost::apply_visitor([](auto& l) { return l.is_committed_empty(); }, leaf)) {
            to_delete.push_back(label);
        } else {
            boost::apply_visitor([](auto& l) { l.rollback(); }, leaf);
        }
    }
    for (const auto& label : to_delete) {
        leaves_.erase(label);
    }
}

std::vector<EdgeLabel> Branch::iter_present_labels() const {
    std::vector<EdgeLabel> result;
    for (const auto& [label, leaf] : leaves_) {
        if (!boost::apply_visitor([](const auto& l) { return l.is_present_empty(); }, leaf)) {
            result.push_back(label);
        }
    }
    return result;
}

void Branch::iter_present_and_remove(std::vector<std::pair<EdgeLabel, VertexID>>& result) {
    for (auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result, &leaf](auto& l) { l.iter_present(result); l.remove_all_from_leaf(leaf); }, leaf);
    }
}

void Branch::iter_present(std::vector<std::pair<EdgeLabel, VertexID>>& result) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result](auto& l) { l.iter_present(result); }, leaf);
    }
}

void Branch::iter_just_added(std::vector<std::pair<EdgeLabel, VertexID>>& result) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result](auto& l) { l.iter_just_added(result); }, leaf);
    }
}

void Branch::iter_just_removed(std::vector<std::pair<EdgeLabel, VertexID>>& result) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result](auto& l) { l.iter_just_removed(result); }, leaf);
    }
}

void Branch::iter_present_via_label(const EdgeLabel& label, std::vector<std::pair<EdgeLabel, VertexID>>& result) const {
    auto it = leaves_.find(label);
    if (it == leaves_.end()) {
        return;
    }
    boost::apply_visitor([&result](const auto& l) { l.iter_present(result); }, it->second);
}

void Branch::iter_present_and_remove(std::vector<Edge>& result) {
    for (auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result, &leaf](auto& l) { l.iter_present(result); l.remove_all_from_leaf(leaf); }, leaf);
    }
}


void Branch::iter_present(std::vector<Edge>& result) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result](auto& l) { l.iter_present(result); }, leaf);
    }
}

void Branch::iter_just_added(std::vector<Edge>& result) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result](auto& l) { l.iter_just_added(result); }, leaf);
    }
}

void Branch::iter_just_removed(std::vector<Edge>& result) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&result](auto& l) { l.iter_just_removed(result); }, leaf);
    }
}

void Branch::iter_present_via_label(const EdgeLabel& label, std::vector<Edge>& result) const {
    auto it = leaves_.find(label);
    if (it == leaves_.end()) {
        return;
    }
    boost::apply_visitor([&result](const auto& l) { l.iter_present(result); }, it->second);
}

// ============================================================================
// Trunk implementation
// ============================================================================

Trunk::Trunk(bool is_inverse) : is_inverse_(is_inverse), inverse_(nullptr) {}

void Trunk::apply() {
    // Merge have_just_added and have_just_removed into single set to iterate
    std::unordered_set<VertexID> vertex_ids;
    for (const auto& [from, _] : have_just_added_) {
        vertex_ids.insert(from);
    }
    for (const auto& [from, _] : have_just_removed_) {
        vertex_ids.insert(from);
    }

    // Apply to all affected branches
    for (VertexID from : vertex_ids) {
        auto it = branches_.find(from);
        if (it != branches_.end()) {
            it->second.apply();
        }
    }

    // Update have_added and have_removed
    have_added_.insert(have_just_added_.begin(), have_just_added_.end());
    have_removed_.insert(have_just_removed_.begin(), have_just_removed_.end());
    have_just_added_.clear();
    have_just_removed_.clear();
}

void Trunk::commit() {
    // Merge have_added and have_removed
    std::unordered_set<VertexID> vertex_ids;
    for (const auto& [from, _] : have_added_) {
        vertex_ids.insert(from);
    }
    for (const auto& [from, _] : have_removed_) {
        vertex_ids.insert(from);
    }

    // Commit all affected branches
    std::vector<VertexID> to_delete;
    for (VertexID from : vertex_ids) {
        auto it = branches_.find(from);
        if (it != branches_.end()) {
            it->second.commit();
            if (it->second.is_empty()) {
                to_delete.push_back(from);
            }
        }
    }

    // Delete empty branches
    for (VertexID from : to_delete) {
        branches_.erase(from);
    }

    have_added_.clear();
    have_removed_.clear();
}

void Trunk::rollback() {
    std::vector<VertexID> to_delete;
    for (auto& [from, branch] : branches_) {
        branch.rollback();
        if (branch.is_empty()) {
            to_delete.push_back(from);
        }
    }

    for (VertexID from : to_delete) {
        branches_.erase(from);
    }

    have_added_.clear();
    have_removed_.clear();
    have_just_added_.clear();
    have_just_removed_.clear();
}

void Trunk::iter_just_added(std::vector<Edge>& result) const {
    for (const auto& [from, branch] : have_just_added_) {
        branch->iter_just_added(result);
    }
}

void Trunk::iter_just_removed(std::vector<Edge>& result) const {
    for (const auto& [from, branch] : have_just_removed_) {
        branch->iter_just_removed(result);
    }
}

void Trunk::iter_present(std::vector<Edge>& result) const {
    for (const auto& [from, branch] : branches_) {
        branch.iter_present(result);
    }
}

void Trunk::iter_present_via_labels(const std::vector<EdgeLabel>& labels, std::vector<Edge>& result) const {
    for (const auto& [from, branch] : branches_) {
        for (const EdgeLabel& label : labels) {
            branch.iter_present_via_label(label, result);
        }
    }
}

Branch* Trunk::get_branch(VertexID from) {
    auto it = branches_.find(from);
    if (it == branches_.end()) {
        return nullptr;
    }
    return &it->second;
}

const Branch* Trunk::get_branch(VertexID from) const {
    auto it = branches_.find(from);
    if (it == branches_.end()) {
        return nullptr;
    }
    return &it->second;
}

// ============================================================================
// MutableEdgesContainer implementation
// ============================================================================

MutableEdgesContainer::MutableEdgesContainer(): forward_(false), inverse_(true) {
    forward_.set_inverse(&inverse_);
    inverse_.set_inverse(&forward_);
}

EdgeLabel MutableEdgesContainer::internalize_label(const std::string& label) {
    auto [it, inserted] = labels_.emplace(std::make_unique<std::string>(label));
    return EdgeLabel(*it->label_);
}

void MutableEdgesContainer::apply() {
    forward_.apply();
    inverse_.apply();
}

void MutableEdgesContainer::commit() {
    forward_.commit();
    inverse_.commit();
}

void MutableEdgesContainer::rollback() {
    forward_.rollback();
    inverse_.rollback();
}

std::vector<Edge> MutableEdgesContainer::iter_present() const {
    std::vector<Edge> result;
    forward_.iter_present(result);
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_present_from_vertex(VertexID from) const {
    std::vector<Edge> result;
    const Branch* branch = forward_.get_branch(from);
    if (branch) {
        branch->iter_present(result);
    }
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_present_to_vertex(VertexID to) const {
    std::vector<Edge> result;
    const Branch* branch = inverse_.get_branch(to);
    if (branch) {
        branch->iter_present(result);
    }
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_present_from_vertex_via_labels(VertexID from, const std::vector<EdgeLabel>& labels) const {
    std::vector<Edge> result;
    const Branch* branch = forward_.get_branch(from);
    if (!branch) {
        return result;
    }

    for (const EdgeLabel& label : labels) {
        branch->iter_present_via_label(label, result);
    }
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_present_to_vertex_via_labels(VertexID to, const std::vector<EdgeLabel>& labels) const {
    std::vector<Edge> result;
    const Branch* branch = inverse_.get_branch(to);
    if (!branch) {
        return result;
    }

    for (const EdgeLabel& label : labels) {
        branch->iter_present_via_label(label, result);
    }
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_present_via_labels(const std::vector<EdgeLabel>& labels) const {
    std::vector<Edge> result;
    forward_.iter_present_via_labels(labels, result);
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_just_added() const {
    std::vector<Edge> result;
    forward_.iter_just_added(result);
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_just_removed() const {
    std::vector<Edge> result;
    forward_.iter_just_removed(result);
    return result;
}

}
