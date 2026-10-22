#include "edges_container.h"
#include <utility>

namespace saengra {

// Undo logs larger than this are freed on commit/rollback instead of being kept for reuse.
static constexpr size_t kMaxRetainedLogCapacity = 4096;

// ============================================================================
// Leaves
// ============================================================================

bool UniLeaf::add(VertexID to, Leaf& self) {
    if (to_ == to) {
        return false;
    }
    if (to_ == nullptr) {
        to_ = to;
        return true;
    }
    const VertexID existing = to_;
    self = MultiLeaf(existing, to);  // destroys *this
    return true;
}

bool UniLeaf::remove(VertexID to) {
    if (to_ != to) {
        return false;
    }
    to_ = nullptr;
    return true;
}

bool MultiLeaf::add(VertexID to, Leaf& self) {
    return targets_.insert(to).second;
}

bool MultiLeaf::remove(VertexID to) {
    return targets_.erase(to) > 0;
}

// ============================================================================
// Branch
// ============================================================================

Branch::Branch(VertexID from, bool is_inverse) : from_(from), is_inverse_(is_inverse) {}

void Branch::push_edge(std::vector<Edge>& dest, const EdgeLabel& label, VertexID to) const {
    if (is_inverse_) dest.emplace_back(to, label, from_);
    else             dest.emplace_back(from_, label, to);
}

bool Branch::add(const EdgeLabel& label, VertexID to) {
    auto [it, inserted] = leaves_.try_emplace(label, UniLeaf());
    Leaf& leaf = it->second;
    if (auto* l = boost::get<UniLeaf>(&leaf)) return l->add(to, leaf);
    return boost::get<MultiLeaf>(leaf).add(to, leaf);
}

bool Branch::remove(const EdgeLabel& label, VertexID to) {
    auto it = leaves_.find(label);
    if (it == leaves_.end()) {
        return false;
    }
    const bool removed = boost::apply_visitor([to](auto& l) { return l.remove(to); }, it->second);
    if (boost::apply_visitor([](const auto& l) { return l.is_empty(); }, it->second)) {
        leaves_.erase(it);
    }
    return removed;
}

void Branch::remove_all_via_label(const EdgeLabel& label, std::vector<VertexID>& removed) {
    auto it = leaves_.find(label);
    if (it == leaves_.end()) {
        return;
    }
    boost::apply_visitor([&removed](const auto& l) { l.for_each([&removed](VertexID to) { removed.push_back(to); }); }, it->second);
    leaves_.erase(it);
}

void Branch::remove_all(std::vector<std::pair<EdgeLabel, VertexID>>& removed) {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([&removed, &label](const auto& l) {
            l.for_each([&removed, &label](VertexID to) { removed.emplace_back(label, to); });
        }, leaf);
    }
    leaves_.clear();
}

void Branch::iter_present(std::vector<Edge>& dest) const {
    for (const auto& [label, leaf] : leaves_) {
        boost::apply_visitor([this, &dest, &label](const auto& l) {
            l.for_each([this, &dest, &label](VertexID to) { push_edge(dest, label, to); });
        }, leaf);
    }
}

void Branch::iter_present_via_label(const EdgeLabel& label, std::vector<Edge>& dest) const {
    auto it = leaves_.find(label);
    if (it == leaves_.end()) {
        return;
    }
    boost::apply_visitor([this, &dest, &label](const auto& l) {
        l.for_each([this, &dest, &label](VertexID to) { push_edge(dest, label, to); });
    }, it->second);
}

void Branch::iter_present_to_vertex_via_labels(VertexID to, const std::vector<EdgeLabel>& labels, std::vector<Edge>& dest) const {
    auto push_if_contains = [this, to, &dest](const EdgeLabel& label, const Leaf& leaf) {
        if (boost::apply_visitor([to](const auto& l) { return l.contains(to); }, leaf)) {
            push_edge(dest, label, to);
        }
    };
    if (labels.empty()) {
        for (const auto& [label, leaf] : leaves_) {
            push_if_contains(label, leaf);
        }
        return;
    }
    for (const EdgeLabel& label : labels) {
        auto it = leaves_.find(label);
        if (it != leaves_.end()) {
            push_if_contains(label, it->second);
        }
    }
}

// ============================================================================
// Trunk
// ============================================================================

Trunk::Trunk(bool is_inverse) : is_inverse_(is_inverse) {}

bool Trunk::add(VertexID from, const EdgeLabel& label, VertexID to) {
    auto [it, inserted] = branches_.try_emplace(from, from, is_inverse_);
    return it->second.add(label, to);
}

bool Trunk::remove(VertexID from, const EdgeLabel& label, VertexID to) {
    auto it = branches_.find(from);
    if (it == branches_.end()) {
        return false;
    }
    const bool removed = it->second.remove(label, to);
    if (it->second.is_empty()) {
        branches_.erase(it);
    }
    return removed;
}

void Trunk::remove_all_via_label(VertexID from, const EdgeLabel& label, std::vector<VertexID>& removed) {
    auto it = branches_.find(from);
    if (it == branches_.end()) {
        return;
    }
    it->second.remove_all_via_label(label, removed);
    if (it->second.is_empty()) {
        branches_.erase(it);
    }
}

void Trunk::remove_all(VertexID from, std::vector<std::pair<EdgeLabel, VertexID>>& removed) {
    auto it = branches_.find(from);
    if (it == branches_.end()) {
        return;
    }
    it->second.remove_all(removed);
    branches_.erase(it);
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

MutableEdgesContainer::MutableEdgesContainer(): forward_(false), inverse_(true) {}

EdgeLabel MutableEdgesContainer::internalize_label(const std::string& label) {
    auto [it, inserted] = labels_.emplace(std::make_unique<std::string>(label));
    return EdgeLabel(*it->label_);
}

bool MutableEdgesContainer::insert(VertexID from, const EdgeLabel& label, VertexID to) {
    if (!forward_.add(from, label, to)) {
        return false;
    }
    inverse_.add(to, label, from);
    return true;
}

bool MutableEdgesContainer::erase(VertexID from, const EdgeLabel& label, VertexID to) {
    if (!forward_.remove(from, label, to)) {
        return false;
    }
    inverse_.remove(to, label, from);
    return true;
}

void MutableEdgesContainer::add_edge(VertexID from, const EdgeLabel& label, VertexID to) {
    if (insert(from, label, to)) {
        log_.push_back({Edge{from, label, to}, true});
    }
}

void MutableEdgesContainer::remove_edge(VertexID from, const EdgeLabel& label, VertexID to) {
    if (erase(from, label, to)) {
        log_.push_back({Edge{from, label, to}, false});
    }
}

void MutableEdgesContainer::remove_edges_from_via_label(VertexID from, const EdgeLabel& label) {
    std::vector<VertexID> removed;
    forward_.remove_all_via_label(from, label, removed);
    for (VertexID to : removed) {
        inverse_.remove(to, label, from);
        log_.push_back({Edge{from, label, to}, false});
    }
}

void MutableEdgesContainer::remove_edges_of_vertex(VertexID vertex) {
    std::vector<std::pair<EdgeLabel, VertexID>> removed;
    forward_.remove_all(vertex, removed);
    for (const auto& [label, to] : removed) {
        inverse_.remove(to, label, vertex);
        log_.push_back({Edge{vertex, label, to}, false});
    }
    // Loops were already removed above, together with outgoing edges.
    removed.clear();
    inverse_.remove_all(vertex, removed);
    for (const auto& [label, from] : removed) {
        forward_.remove(from, label, vertex);
        log_.push_back({Edge{from, label, vertex}, false});
    }
}

void MutableEdgesContainer::apply() {
    applied_ = log_.size();
}

void MutableEdgesContainer::commit() {
    log_.erase(log_.begin(), log_.begin() + applied_);
    applied_ = 0;
    if (log_.empty() && log_.capacity() > kMaxRetainedLogCapacity) {
        std::vector<Change>().swap(log_);
    }
}

void MutableEdgesContainer::rollback() {
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        const auto& [edge, added] = *it;
        if (added) erase(edge.from, edge.label, edge.to);
        else       insert(edge.from, edge.label, edge.to);
    }
    log_.clear();
    applied_ = 0;
    if (log_.capacity() > kMaxRetainedLogCapacity) {
        std::vector<Change>().swap(log_);
    }
}

std::vector<Edge> MutableEdgesContainer::iter_just_changed(bool added) const {
    // Only actual changes are logged, so changes of a particular edge alternate between
    // adding and removing; the first and the last of them define the net change.
    absl::flat_hash_map<Edge, std::pair<bool, bool>, std::hash<Edge>> first_and_last;
    for (auto it = log_.begin() + applied_; it != log_.end(); ++it) {
        auto [entry, inserted] = first_and_last.try_emplace(it->edge, it->added, it->added);
        if (!inserted) {
            entry->second.second = it->added;
        }
    }
    std::vector<Edge> result;
    for (const auto& [edge, first_and_last_added] : first_and_last) {
        if (first_and_last_added.first == added && first_and_last_added.second == added) {
            result.push_back(edge);
        }
    }
    return result;
}

std::vector<Edge> MutableEdgesContainer::iter_just_added() const {
    return iter_just_changed(true);
}

std::vector<Edge> MutableEdgesContainer::iter_just_removed() const {
    return iter_just_changed(false);
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

std::vector<Edge> MutableEdgesContainer::iter_present_from_vertex_to_vertex(VertexID from, VertexID to, const std::vector<EdgeLabel>& labels) const {
    std::vector<Edge> result;
    const Branch* branch = forward_.get_branch(from);
    if (branch) {
        branch->iter_present_to_vertex_via_labels(to, labels, result);
    }
    return result;
}

}
