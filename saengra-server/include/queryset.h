#pragma once

#include "absl/hash/hash.h"
#include "vertex.h"
#include "observable.h"
#include "subgraph.h"

namespace saengra {

struct AbsentPosition {
    /* Describes a position that is not currently present in the graph.
    Can be used to subscribe to appearance of particular new positions. */

    VertexData vertex;
    PositionKind kind;

    inline bool operator==(const AbsentPosition& other) const {
        return vertex == other.vertex && kind == other.kind;
    }

//    inline bool operator<(const AbsentPosition& other) const {
//        return vertex < other.vertex || (vertex == other.vertex && kind < other.kind);
//    }
};

struct EveryAddedVertex {
    inline bool operator==(const EveryAddedVertex& other) const {
        return true;
    }

    inline bool operator<(const EveryAddedVertex& other) const {
        return false;
    }
};

struct EveryAddedEdge {
    // TODO direction
    inline bool operator==(const EveryAddedEdge& other) const {
        return true;
    }

    inline bool operator<(const EveryAddedEdge& other) const {
        return false;
    }
};

// A nested expression matched from a start position with the refs, whose subgraphs are cached.
struct NestedExpressionStart {
    size_t expression_id;
    Position position;
    Refs refs;

    inline bool operator==(const NestedExpressionStart& other) const {
        return expression_id == other.expression_id && position == other.position && refs == other.refs;
    }

    inline NestedExpressionSubgraph observable() const {
        return NestedExpressionSubgraph{expression_id, position.vertex_id, position.kind == PositionKind::ORBIT, refs};
    }
};

using ObservableStartPosition = std::variant<Position, AbsentPosition, EveryAddedVertex, EveryAddedEdge, NestedExpressionStart>;

struct QuerySetObservable {
    Observable observable;
    ObservableStartPosition start_position;
};

using QuerySetObservables = std::vector<QuerySetObservable>;

struct QuerySet {
    std::vector<Subgraph> subgraphs;  // sorted
    QuerySetObservables deps;
    QuerySetObservables start_deps;
};

}

namespace std {

template<>
struct hash<saengra::Position> {
    size_t operator()(const saengra::Position& position) const {
        return absl::Hash<std::tuple<size_t, saengra::PositionKind>>()(make_tuple((size_t)position.vertex_id, position.kind));
    }
};

template<>
struct hash<saengra::AbsentPosition> {
    size_t operator()(const saengra::AbsentPosition& position) const {
        return absl::Hash<std::tuple<saengra::VertexData, saengra::PositionKind>>()(make_tuple(position.vertex, position.kind));
    }
};

template<>
struct hash<saengra::EveryAddedVertex> {
    size_t operator()(const saengra::EveryAddedVertex& eav) const {
        return 1 << 10;
    }
};

template<>
struct hash<saengra::NestedExpressionStart> {
    size_t operator()(const saengra::NestedExpressionStart& start) const {
        return absl::Hash<std::tuple<size_t, size_t, saengra::PositionKind, size_t>>()(make_tuple(
            start.expression_id, (size_t)start.position.vertex_id, start.position.kind, std::hash<saengra::Refs>{}(start.refs)));
    }
};

template<>
struct hash<saengra::EveryAddedEdge> {
    size_t operator()(const saengra::EveryAddedEdge& eav) const {
        return 1 << 20;
    }
};

}
