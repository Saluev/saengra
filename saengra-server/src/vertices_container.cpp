#include "vertices_container.h"
#include "absl/container/flat_hash_map.h"

namespace saengra {

// Undo logs larger than this are freed on commit/rollback instead of being kept for reuse.
static constexpr size_t kMaxRetainedLogCapacity = 4096;

MutableVerticesContainer::MutableVerticesContainer() {}

VertexTypeName MutableVerticesContainer::internalize_type_name(const std::string& type_name) {
    auto it = type_names.find(type_name);
    if (it == type_names.end()) {
        it = type_names.insert(type_name).first;
    }
    return VertexTypeName(*it);
}

void MutableVerticesContainer::set_present(VertexID vertex_id, bool is_present) {
    vertex_id->present = is_present;
    if (is_present) {
        present.insert(vertex_id);
        vertices_by_type_name[vertex_id->type_name].insert(vertex_id);
    } else {
        present.erase(vertex_id);
        auto type_it = vertices_by_type_name.find(vertex_id->type_name);
        if (type_it != vertices_by_type_name.end()) {
            type_it->second.erase(vertex_id);
        }
    }
}

void MutableVerticesContainer::add_vertex(const VertexData& v) {
    auto it = vertex_id_by_vertex.find(v);
    if (it == vertex_id_by_vertex.end()) {
        it = vertex_id_by_vertex.emplace(v, std::make_unique<StoredVertex>(v.type_name, v.value, false)).first;
    }
    VertexID vertex_id = it->second.get();
    if (vertex_id->present) {
        return;
    }
    set_present(vertex_id, true);
    log_.push_back({vertex_id, true});
}

std::optional<VertexID> MutableVerticesContainer::get_vertex_id(const VertexData& v) const {
    auto it = vertex_id_by_vertex.find(v);
    if (it == vertex_id_by_vertex.end()) {
        return std::nullopt;
    }
    return it->second.get();
}

void MutableVerticesContainer::discard_vertex(const VertexData& v) {
    auto it = vertex_id_by_vertex.find(v);
    if (it == vertex_id_by_vertex.end()) {
        return;
    }
    VertexID vertex_id = it->second.get();
    if (!vertex_id->present) {
        return;
    }
    set_present(vertex_id, false);
    log_.push_back({vertex_id, false});
}

void MutableVerticesContainer::apply() {
    applied_ = log_.size();
}

void MutableVerticesContainer::commit() {
    log_.erase(log_.begin(), log_.begin() + applied_);
    applied_ = 0;
    if (log_.empty() && log_.capacity() > kMaxRetainedLogCapacity) {
        std::vector<Change>().swap(log_);
    }

    // TODO clear VertexData <-> VertexID mappings for removed vertices
    //      (and take care of IncrementalMatchers holding pointers to the now-removed vertices?)
}

void MutableVerticesContainer::rollback() {
    for (auto it = log_.rbegin(); it != log_.rend(); ++it) {
        set_present(it->vertex, !it->added);
    }
    log_.clear();
    applied_ = 0;
    if (log_.capacity() > kMaxRetainedLogCapacity) {
        std::vector<Change>().swap(log_);
    }
}

std::vector<VertexID> MutableVerticesContainer::iter_just_changed(bool added) const {
    // Only actual changes are logged, so changes of a particular vertex alternate between
    // adding and removing; the first and the last of them define the net change.
    absl::flat_hash_map<VertexID, std::pair<bool, bool>> first_and_last;
    for (auto it = log_.begin() + applied_; it != log_.end(); ++it) {
        auto [entry, inserted] = first_and_last.try_emplace(it->vertex, it->added, it->added);
        if (!inserted) {
            entry->second.second = it->added;
        }
    }
    std::vector<VertexID> result;
    for (const auto& [vertex_id, first_and_last_added] : first_and_last) {
        if (first_and_last_added.first == added && first_and_last_added.second == added) {
            result.push_back(vertex_id);
        }
    }
    return result;
}

std::vector<VertexID> MutableVerticesContainer::iter_just_added() const {
    return iter_just_changed(true);
}

std::vector<VertexID> MutableVerticesContainer::iter_just_removed() const {
    return iter_just_changed(false);
}

VertexIDRange MutableVerticesContainer::iter_present() const {
    return present;
}

VertexIDRange MutableVerticesContainer::iter_present_by_type_name(const VertexTypeName& type_name) const {
    static const std::unordered_set<VertexID> empty_set;
    auto it = vertices_by_type_name.find(type_name);
    if (it != vertices_by_type_name.end()) {
        return it->second;
    }
    return empty_set;
}

}
