#pragma once

#include <set>
#include "absl/container/flat_hash_set.h"
#include "absl/container/flat_hash_map.h"
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

using ObservableSet = absl::flat_hash_set<Observable, std::hash<Observable>>;
using DepToStartPositions = absl::flat_hash_map<Observable, absl::flat_hash_set<ObservableStartPosition, std::hash<ObservableStartPosition>>, std::hash<Observable>>;
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
    ObservableSet gather_deps_for_sps(const std::unordered_set<Position>& sps) const;
    void clear_deps_for_sps(const std::unordered_set<Position>& sps);
    std::unordered_set<Position> iter_start_positions_needing_rematch(const Observations& os) const;
    SubgraphChanges find_subgraph_changes(
        const std::unordered_set<Position>& sps,
        const PositionToSubgraphs& last_sg_by_sp,
        const PositionToSubgraphs& curr_sg_by_sp) const;
};

}
