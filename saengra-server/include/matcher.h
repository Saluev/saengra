#pragma once

#include "subgraph.h"
#include "start_positions.h"

namespace saengra {

struct PreprocessedQuery {
    const Graph& graph;
    Expression expression;
    std::vector<std::optional<VertexID>> placeholder_value_ids;
};

struct SubgraphDraft {
    Position start_position;
    std::vector<Position> end_positions;
    std::vector<VertexID> vertices;
    std::vector<Edge> edges;
    Refs refs;

    Subgraph finalize() const;

    inline bool operator<(const SubgraphDraft& other) const {
        return std::tie(start_position, end_positions, vertices, edges, refs)
             < std::tie(other.start_position, other.end_positions, other.vertices, other.edges, other.refs);
    }

    inline bool operator==(const SubgraphDraft& other) const {
         return std::tie(start_position, end_positions, vertices, edges, refs)
             == std::tie(other.start_position, other.end_positions, other.vertices, other.edges, other.refs);
    }
};

struct QuerySetDraft {
    std::vector<SubgraphDraft> subgraphs;
    std::vector<QuerySetObservable>& deps;
    std::vector<QuerySetObservable> start_deps;

    QuerySetDraft(std::vector<QuerySetObservable>& deps) : deps(deps) {}
    QuerySet finalize() const;
};

struct Start {
    Position origin;
    Position position;
    Refs refs;
};

class Matcher {
public:
    QuerySet match(const Query& query, const StartPositions& start_positions) const;

private:
    PreprocessedQuery preprocess_query(const Query& query) const;
    void match_(const PreprocessedQuery& query, const Expression& expr, const Start& start, QuerySetDraft& dest) const;
};

}
