#pragma once

#include <string>
#include "vertex.h"

namespace saengra {

struct EdgeLabel {
    const std::string* text;

    explicit EdgeLabel(const std::string& text);

    inline bool operator==(const EdgeLabel& other) const {
        return text == other.text;
    }

    inline bool operator<(const EdgeLabel& other) const {
        return (size_t)(text) < (size_t)(other.text);  // fast deterministic order
    }
};

std::ostream& operator<<(std::ostream& os, const EdgeLabel& obj);

struct Edge {
    VertexID from;
    EdgeLabel label;
    VertexID to;

    inline Edge reverse() const {
        return Edge{to, label, from};;
    }

    inline bool operator<(const Edge& other) const {
        return from < other.from || (from == other.from && label < other.label) || (from == other.from && label == other.label && to < other.to);
    }

    inline bool operator==(const Edge& other) const {
        return from == other.from && label == other.label && to == other.to;
    }

    inline bool operator!=(const Edge& other) const {
        return !(*this == other);
    }
};

using Edges = std::vector<Edge>;

std::ostream& operator<<(std::ostream& os, const Edge& obj);

Edges reverse_all(const Edges& edges);
Edges sort_and_intersect(Edges& a, Edges& b);

}

namespace std {

template <>
struct hash<saengra::EdgeLabel> {
    std::size_t operator()(const saengra::EdgeLabel& label) const {
        return (size_t)(label.text);
    }
};

template <>
struct hash<saengra::Edge> {
    std::size_t operator()(const saengra::Edge& edge) const {
        return absl::Hash<std::tuple<saengra::VertexID, saengra::EdgeLabel, saengra::VertexID>>()(make_tuple(edge.from, edge.label, edge.to));
    }
};

}
