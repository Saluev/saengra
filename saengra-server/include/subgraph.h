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
        if (start_position < other.start_position) return true;
        if (other.start_position < start_position) return false;

        if (end_positions < other.end_positions) return true;
        if (other.end_positions < end_positions) return false;

        if (vertices < other.vertices) return true;
        if (other.vertices < vertices) return false;

        if (edges < other.edges) return true;
        if (other.edges < edges) return false;

        return refs < other.refs;
    }

    inline bool operator==(const Subgraph& other) const {
        return (
            start_position == other.start_position
            && end_positions == other.end_positions
            && vertices == other.vertices
            && edges == other.edges
            && refs == other.refs
        );
    }

    inline bool operator!=(const Subgraph& other) const {
        return !(*this == other);
    }
};

using Subgraphs = std::set<Subgraph>;

}
