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
using PositionToSubgraphs = std::unordered_map<Position, std::vector<Subgraph>>;  // sorted

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
    // Deps of start positions, sorted.
    absl::flat_hash_map<Position, Observables, std::hash<Position>> position_to_deps_;

    static PositionToSubgraphs calc_sg_by_sp(std::vector<Subgraph>&& subgraphs);
    // Makes the start position depend on the dep. If nothing depended on it before, it's added to
    // `added_deps`, unless it's in `removed_deps`, then it's erased from there.
    void subscribe(const ObservableStartPosition& sp, const Observable& dep, Observables& added_deps, ObservableSet& removed_deps);
    // Makes the start position not depend on the dep. If nothing depends on it anymore, it's added to `removed_deps`.
    void unsubscribe(const Position& sp, const Observable& dep, ObservableSet& removed_deps);
    // Makes the start position depend on the new deps instead of the old ones; both are sorted.
    void resubscribe(const Position& sp, const Observables& old_deps, const Observables& new_deps,
                     Observables& added_deps, ObservableSet& removed_deps);
    std::unordered_set<Position> iter_start_positions_needing_rematch(const Observations& os) const;
    SubgraphChanges find_subgraph_changes(
        const std::unordered_set<Position>& sps,
        const PositionToSubgraphs& last_sg_by_sp,
        const PositionToSubgraphs& curr_sg_by_sp) const;
};

}
