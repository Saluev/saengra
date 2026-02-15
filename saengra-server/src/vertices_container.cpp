#include "vertices_container.h"

namespace saengra {

MutableVerticesContainer::MutableVerticesContainer() {}

VertexTypeName MutableVerticesContainer::internalize_type_name(const std::string& type_name) {
    auto [it, inserted] = type_names.emplace(std::make_unique<std::string>(type_name));
    return VertexTypeName(*it->type_name);
}

void MutableVerticesContainer::add_vertex(const VertexData& v) {
    VertexID vertex_id;
    auto it = vertex_id_by_vertex.find(v);
    if (it == vertex_id_by_vertex.end()) {
        auto [it, _] = vertex_id_by_vertex.emplace(v, std::make_unique<StoredVertex>(v.type_name, v.value, false));
        vertex_id = it->second.get();
    } else {
        vertex_id = it->second.get();
    }
    if (vertex_id->present) {
        return;
    }
    present.insert(vertex_id);
    vertex_id->present = true;
    auto just_removed_it = just_removed.find(vertex_id);
    if (just_removed_it != just_removed.end()) {
        just_removed.erase(just_removed_it);
    } else {
        just_added.insert(vertex_id);
    }
    vertices_by_type_name[v.type_name].insert(vertex_id);
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
    present.erase(vertex_id);
    vertex_id->present = false;
    auto just_added_it = just_added.find(vertex_id);
    if (just_added_it != just_added.end()) {
        just_added.erase(just_added_it);
    } else {
        just_removed.insert(vertex_id);
    }
    auto type_it = vertices_by_type_name.find(v.type_name);
    if (type_it != vertices_by_type_name.end()) {
        type_it->second.erase(vertex_id);
    }
}

void add_all(VertexIDSet& to, const VertexIDSet& from) {
    to.insert(from.begin(), from.end());
}

void add_except(VertexIDSet& to, const VertexIDSet& from, const VertexIDSet& except) {
    for (const auto& id : from) {
        if (!except.contains(id)) {
            to.insert(id);
        }
    }
}

void add_intersection(VertexIDSet& to, const VertexIDSet& from1, const VertexIDSet& from2) {
    if (from2.size() < from1.size()) {
        add_intersection(to, from2, from1);
        return;
    }

    for (const auto& id : from1) {
        if (from2.contains(id)) {
            to.insert(id);
        }
    }
}

void remove_all(VertexIDSet& from, const VertexIDSet& what) {
    for (const auto& id : what) {
        from.erase(id);
    }
}

void MutableVerticesContainer::apply() {
    add_except(added, just_added, committed);
    remove_all(added, just_removed);
    add_intersection(removed, just_removed, committed);
    remove_all(removed, just_added);
    just_added.clear();
    just_removed.clear();
}

void MutableVerticesContainer::commit() {
    add_all(committed, added);
    remove_all(committed, removed);
    added.clear();
    removed.clear();

    // TODO clear VertexData <-> VertexID mappings for `removed`
    //      (and take care of IncrementalMatchers holding pointers to the now-removed vertices?)
}

void MutableVerticesContainer::rollback() {
    VertexIDSet to_add;
    add_except(to_add, removed, just_added);
    add_all(to_add, just_removed);

    VertexIDSet to_remove;
    add_except(to_remove, added, just_removed);
    add_all(to_remove, just_added);

    added.clear();
    removed.clear();
    just_added.clear();
    just_removed.clear();

    add_all(present, to_add);
    for (auto vertex_id : to_add) {
        vertex_id->present = true;
    }
    remove_all(present, to_remove);
    for (auto vertex_id : to_remove) {
        vertex_id->present = false;
    }

    for (const auto& vertex_id : to_add) {
        vertices_by_type_name[vertex_id->type_name].insert(vertex_id);
    }
    for (const auto& vertex_id : to_remove) {
        vertices_by_type_name[vertex_id->type_name].erase(vertex_id);
    }
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

VertexIDRange MutableVerticesContainer::iter_just_added() const {
    return just_added;
}

VertexIDRange MutableVerticesContainer::iter_just_removed() const {
    return just_removed;
}

}
