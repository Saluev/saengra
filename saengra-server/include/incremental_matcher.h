#pragma once

#include <set>
#include "observable.h"
#include "subgraph.h"
#include "graph.h"
#include "matcher.h"

namespace saengra {

struct ChangedSubgraph {
    Subgraph before;
    Subgraph after;

    inline bool operator<(const ChangedSubgraph& other) const {
        if (before < other.before) return true;
        if (other.before < before) return false;
        return after < other.after;
    }
};

using ChangedSubgraphs = std::set<ChangedSubgraph>;

struct SubgraphChanges {
    Subgraphs added_subgraphs;
    ChangedSubgraphs changed_subgraphs;
    Subgraphs removed_subgraphs;
};

struct IncrementalUpdate {
    Observables added_deps;
    Observables removed_deps;

    Subgraphs added_subgraphs;
    ChangedSubgraphs changed_subgraphs;
    Subgraphs removed_subgraphs;
};

using DepToStartPositions = std::unordered_map<Observable, std::unordered_set<ObservableStartPosition>>;
using PositionToSubgraphs = std::unordered_map<Position, Subgraphs>;

class IncrementalMatcher {
public:
    IncrementalMatcher(const Matcher& matcher, const Query& query);
    Observables reinitialize();
    IncrementalUpdate match_incrementally(const Observations& os);

private:
    const Matcher& matcher_;
    Query query_;

    PositionToSubgraphs last_sp_to_sgs_;
    QuerySetObservables start_deps_;
    DepToStartPositions dep_to_positions_;
    std::unordered_map<Position, Observables> position_to_deps_;

    PositionToSubgraphs calc_sg_by_sp(const QuerySet& qs) const;
    void fill_deps_from_qs(const QuerySet& qs);
    void fill_deps_from_query_set_observables(const QuerySetObservables& qsos);
    std::unordered_set<Observable> gather_deps_for_sps(const std::unordered_set<Position>& sps) const;
    void clear_deps_for_sps(const std::unordered_set<Position>& sps);
    std::unordered_set<Position> iter_start_positions_needing_rematch(const Observations& os) const;
    SubgraphChanges find_subgraph_changes(
        const std::unordered_set<Position>& sps,
        const PositionToSubgraphs& last_sg_by_sp,
        const PositionToSubgraphs& curr_sg_by_sp) const;
};

}
