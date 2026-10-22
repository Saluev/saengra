#pragma once

#include "subgraph.h"
#include "start_positions.h"

namespace saengra {

// Subgraphs of nested expressions, cached by the incremental matcher.
class NestedResults {
public:
    virtual ~NestedResults() = default;
    // Subgraphs that the nested expression matches from the position with the refs (only the ones it mentions).
    virtual const std::vector<Subgraph>& get(const MatchNestedExpression& plan, const Position& position, const Refs& refs) = 0;
};

struct PreprocessedQuery {
    const Graph& graph;
    const PlaceholderValues& placeholder_values;
    std::vector<std::optional<VertexID>> placeholder_value_ids;
    NestedResults* nested_results;  // if null, nested expressions are matched in place
};

struct SubgraphDraft {
    Position start_position;
    std::vector<Position> end_positions;
    std::vector<VertexID> vertices;
    std::vector<Edge> edges;
    Refs refs;

    Subgraph finalize() &&;

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
    QuerySet finalize() &&;
};

struct Start {
    Position origin;
    Position position;
    Refs refs;
};

class Matcher {
public:
    QuerySet match(const Query& query, const StartPositions& start_positions, NestedResults* nested_results = nullptr) const;
    // Matches a nested expression of the query; its dependencies have the position as their start.
    QuerySet match_nested(const Query& query, const MatchNestedExpression& plan, const Position& position, const Refs& refs,
                          NestedResults* nested_results) const;

private:
    PreprocessedQuery preprocess_query(const Query& query, NestedResults* nested_results) const;
    void match_(const PreprocessedQuery& query, const Plan& plan, const Start& start, QuerySetDraft& dest) const;
    void expand_negatives(const Graph& graph, const Position& start_pos, size_t first, QuerySetDraft& result) const;
};

}
