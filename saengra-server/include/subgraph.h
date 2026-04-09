#pragma once

#include "absl/hash/hash.h"
#include "vertex.h"
#include "observable.h"
#include "expression.h"
#include "graph.h"
#include "refs.h"

namespace saengra {

enum class PositionKind {
    CORE,
    ORBIT
};

struct Position {
    /* Describes a position within a graph.*/

    VertexID vertex_id;
    PositionKind kind;

    Position with_kind(PositionKind new_kind) const {
        return Position{vertex_id, new_kind};
    }

    inline bool operator==(const Position& other) const {
        return vertex_id == other.vertex_id && kind == other.kind;
    }

    inline bool operator<(const Position& other) const {
        return vertex_id < other.vertex_id || (vertex_id == other.vertex_id && kind < other.kind);
    }
};

struct Subgraph {
    Position start_position;
    std::vector<Position> end_positions;
    std::vector<VertexID> vertices;
    std::vector<Edge> edges;
    Refs refs;

    inline bool operator<(const Subgraph& other) const {
        return std::tie(start_position, end_positions, vertices, edges, refs)
             < std::tie(other.start_position, other.end_positions, other.vertices, other.edges, other.refs);
    }

    inline bool operator==(const Subgraph& other) const {
         return std::tie(start_position, end_positions, vertices, edges, refs)
             == std::tie(other.start_position, other.end_positions, other.vertices, other.edges, other.refs);
    }

    inline bool operator!=(const Subgraph& other) const {
        return !(*this == other);
    }
};

using Subgraphs = std::set<Subgraph>;

}
