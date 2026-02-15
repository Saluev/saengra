#pragma once

#include <string>
#include <boost/variant.hpp>
#include "vertex.h"
#include "edge.h"

namespace saengra {

struct NewVertex {
    inline bool operator==(const NewVertex&) const { return true; }
    inline bool operator<(const NewVertex&) const { return false; }
};

struct NewVertexOfType {
    VertexTypeName type_name;

    inline bool operator==(const NewVertexOfType& other) const = default;
    inline bool operator<(const NewVertexOfType& other) const {
        return type_name < other.type_name;
    }
};

struct ParticularVertex {
    VertexData vertex;  // TODO can it be VertexID? Like, is it possible that this is an absent vertex?

    inline bool operator==(const ParticularVertex& other) const = default;
    inline bool operator<(const ParticularVertex& other) const {
        return vertex < other.vertex;
    }
};

struct NewEdge {
    inline bool operator==(const NewEdge& other) const { return true; }
    inline bool operator<(const NewEdge&) const { return false; }
};

struct NewEdgeWithLabel {
    EdgeLabel label;

    inline bool operator==(const NewEdgeWithLabel& other) const = default;
    inline bool operator<(const NewEdgeWithLabel& other) const {
        return label < other.label;
    }
};

struct EdgeFrom {
    VertexID from_id;

    inline bool operator==(const EdgeFrom& other) const = default;
    inline bool operator<(const EdgeFrom& other) const {
        return from_id < other.from_id;
    }
};

struct EdgeFromWithLabel {
    VertexID from_id;
    EdgeLabel label;

    inline bool operator==(const EdgeFromWithLabel& other) const = default;
    inline bool operator<(const EdgeFromWithLabel& other) const {
        return from_id < other.from_id || from_id == other.from_id && label < other.label;
    }
};

struct EdgeTo {
    VertexID to_id;

    inline bool operator==(const EdgeTo& other) const = default;
    inline bool operator<(const EdgeTo& other) const {
        return to_id < other.to_id;
    }
};

struct EdgeToWithLabel {
    VertexID to_id;
    EdgeLabel label;

    inline bool operator==(const EdgeToWithLabel& other) const = default;
    inline bool operator<(const EdgeToWithLabel& other) const {
        return to_id < other.to_id || to_id == other.to_id && label < other.label;
    }
};

using Observable = boost::variant<
    NewVertex,
    NewVertexOfType,
    ParticularVertex,
    NewEdge,
    NewEdgeWithLabel,
    EdgeFrom,
    EdgeFromWithLabel,
    EdgeTo,
    EdgeToWithLabel
>;

using Observables = std::vector<Observable>;

struct Observation {
    Observation(Observable obs): observable(obs) {}

    Observable observable;
    std::vector<VertexID> added_vertex_ids;
    std::vector<Edge> added_edges;

    Observation with_added_vertex_ids(const std::vector<VertexID>& added_vertex_ids) const { // for tests
        Observation result(*this);
        result.added_vertex_ids.insert(result.added_vertex_ids.end(), added_vertex_ids.begin(), added_vertex_ids.end());
        return result;;
    }

    Observation with_added_edges(const std::vector<Edge>& added_edges) const { // for tests
        Observation result(*this);
        result.added_edges.insert(result.added_edges.end(), added_edges.begin(), added_edges.end());
        return result;;
    }

    inline void track_added_vertex(VertexID vertex_id) {
        added_vertex_ids.push_back(vertex_id);
    }

    inline void track_added_edge(Edge edge) {
        added_edges.push_back(edge);
    }

    inline bool operator<(const Observation& other) const {
        return observable < other.observable;
    }

    inline bool operator==(const Observation& other) const {
        return observable == other.observable &&
               added_vertex_ids == other.added_vertex_ids &&
               added_edges == other.added_edges;
    }
};

using Observations = std::vector<Observation>;

std::ostream& operator<<(std::ostream& os, const NewVertex& obj);
std::ostream& operator<<(std::ostream& os, const NewVertexOfType& obj);
std::ostream& operator<<(std::ostream& os, const ParticularVertex& obj);
std::ostream& operator<<(std::ostream& os, const NewEdge& obj);
std::ostream& operator<<(std::ostream& os, const NewEdgeWithLabel& obj);
std::ostream& operator<<(std::ostream& os, const EdgeFrom& obj);
std::ostream& operator<<(std::ostream& os, const EdgeFromWithLabel& obj);
std::ostream& operator<<(std::ostream& os, const EdgeTo& obj);
std::ostream& operator<<(std::ostream& os, const EdgeToWithLabel& obj);
std::ostream& operator<<(std::ostream& os, const Observable& obj);
std::ostream& operator<<(std::ostream& os, const Observation& obj);

}

namespace std {

template <>
struct hash<saengra::NewVertex> {
    std::size_t operator()(const saengra::NewVertex& v) const {
        return (1 << 30);  // to avoid collisions with ParticularVertex()
    }
};

template <>
struct hash<saengra::NewVertexOfType> {
    std::size_t operator()(const saengra::NewVertexOfType& v) const {
        return (size_t)(v.type_name.text);
    }
};

template <>
struct hash<saengra::ParticularVertex> {
    std::size_t operator()(const saengra::ParticularVertex& v) const {
        return std::hash<saengra::VertexData>{}(v.vertex);
    }
};

template <>
struct hash<saengra::NewEdgeWithLabel> {
    std::size_t operator()(const saengra::NewEdgeWithLabel& v) const {
        return (size_t)(v.label.text);
    }
};

template <>
struct hash<saengra::EdgeFrom> {
    std::size_t operator()(const saengra::EdgeFrom& v) const {
        return (1 << 20) + (size_t)v.from_id;
    }
};

template <>
struct hash<saengra::EdgeFromWithLabel> {
    std::size_t operator()(const saengra::EdgeFromWithLabel& v) const {
        return absl::Hash<std::tuple<saengra::VertexID, size_t>>()(make_tuple(v.from_id, (size_t)(v.label.text)));
    }
};

template <>
struct hash<saengra::EdgeTo> {
    std::size_t operator()(const saengra::EdgeTo& v) const {
        return (1 << 25) + (size_t)v.to_id;
    }
};

template <>
struct hash<saengra::EdgeToWithLabel> {
    std::size_t operator()(const saengra::EdgeToWithLabel& v) const {
        return absl::Hash<std::tuple<saengra::VertexID, size_t>>()(make_tuple(v.to_id, (size_t)(v.label.text)));
    }
};

template <>
struct hash<saengra::NewEdge> {
    std::size_t operator()(const saengra::NewEdge& v) const {
        return (1 << 30) + 1;  // to avoid collisions with ParticularVertex() and NewVertex()
    }
};

namespace {
    struct HashVisitor : boost::static_visitor<size_t> {
        template<typename T>
        size_t operator()(const T& observable) const {
            return hash<T>{}(observable);
        }
    };
}

template<>
struct hash<saengra::Observable> {
    std::size_t operator()(const saengra::Observable& observable) const {
        return boost::apply_visitor(HashVisitor(), observable);
    }
};

}
