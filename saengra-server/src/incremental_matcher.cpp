#include <algorithm>
#include <functional>
#include <map>
#include <unordered_set>
#include "debug.h"
#include "logging.h"
#include "incremental_matcher.h"
#include "start_positions.h"

namespace saengra {

IncrementalMatcher::IncrementalMatcher(const Matcher& matcher, const Query& query)
    : matcher_(matcher), query_(query) {}

Observables IncrementalMatcher::reinitialize() {
    StartPositionFinder finder;
    auto start_positions = finder.find_start_positions(query_);
    auto last_qs = matcher_.match(query_, start_positions);

    last_sp_to_sgs_ = calc_sg_by_sp(std::move(last_qs.subgraphs));
    start_deps_ = std::move(last_qs.start_deps);
    dep_to_positions_.clear();
    position_to_deps_.clear();

    // Return observables that we have to subscribe to immediately.
    Observables result;
    ObservableSet removed;
    fill_deps(last_qs.deps, result, removed);
    fill_deps(start_deps_, result, removed);
    return result;
}

PositionToSubgraphs IncrementalMatcher::calc_sg_by_sp(std::vector<Subgraph>&& subgraphs) {
    // Subgraphs are sorted, so they stay sorted within each start position.
    PositionToSubgraphs sg_by_sp;
    for (Subgraph& sg : subgraphs) {
        auto& sgs = sg_by_sp[sg.start_position];
        sgs.push_back(std::move(sg));
    }
    return sg_by_sp;
}

void IncrementalMatcher::fill_deps(const QuerySetObservables& qsos, Observables& added_deps, ObservableSet& removed_deps) {
    for (const QuerySetObservable& d : qsos) {
        auto& positions = dep_to_positions_[d.observable];
        const bool was_unused = positions.empty();
        if (!positions.insert(d.start_position).second) {
            continue;
        }
        if (std::holds_alternative<Position>(d.start_position)) {
            position_to_deps_[std::get<Position>(d.start_position)].push_back(d.observable);
        }
        if (was_unused && removed_deps.erase(d.observable) == 0) {
            added_deps.push_back(d.observable);
        }
    }
}

void IncrementalMatcher::clear_deps_for_sps(const std::unordered_set<Position>& sps, ObservableSet& removed_deps) {
    for (const Position& sp : sps) {
        auto deps_it = position_to_deps_.find(sp);
        if (deps_it == position_to_deps_.end()) {
            continue;
        }

        const Observables& deps = deps_it->second;
        for (const Observable& d : deps) {
            auto dep_it = dep_to_positions_.find(d);
            if (dep_it != dep_to_positions_.end()) {
                dep_it->second.erase(sp);
                if (dep_it->second.empty()) {
                    removed_deps.insert(d);
                }
            }
        }

        position_to_deps_.erase(deps_it);
    }
}

std::unordered_set<Position> IncrementalMatcher::iter_start_positions_needing_rematch(const Observations& os) const {
    std::unordered_set<Position> result;
    for (const Observation& o : os) {
        auto it = dep_to_positions_.find(o.observable);
        if (it == dep_to_positions_.end()) {
            continue;
        }
        for (const ObservableStartPosition& arg : it->second) {
            if (std::holds_alternative<EveryAddedVertex>(arg)) {
                for (VertexID vid : o.added_vertex_ids) {
                    result.emplace(vid, PositionKind::CORE);
                }
            } else if (std::holds_alternative<EveryAddedEdge>(arg)) {
                for (const Edge& edge : o.added_edges) {
                    // TODO only necessary directions?..
                    result.emplace(edge.from, PositionKind::ORBIT);
                    result.emplace(edge.to, PositionKind::ORBIT);
                }
            } else if (std::holds_alternative<Position>(arg)) {
                result.insert(std::get<Position>(arg));
            }
        }
    }
    return result;
}

static const std::vector<Subgraph>& find_subgraphs(const PositionToSubgraphs& sg_by_sp, const Position& sp) {
    static const std::vector<Subgraph> none;
    auto it = sg_by_sp.find(sp);
    return it != sg_by_sp.end() ? it->second : none;
}

// Subgraphs of a start position with the same refs, before and after rematching; both sorted.
struct SubgraphsWithRefs {
    std::vector<const Subgraph*> last;
    std::vector<const Subgraph*> curr;
};

// Adds subgraphs of sorted `from` that aren't in sorted `except` to `dest`.
static void add_difference(const std::vector<const Subgraph*>& from, const std::vector<const Subgraph*>& except, Subgraphs& dest) {
    auto it = except.begin();
    for (const Subgraph* sg : from) {
        while (it != except.end() && **it < *sg) {
            ++it;
        }
        if (it == except.end() || *sg < **it) {
            dest.insert(*sg);
        }
    }
}

SubgraphChanges IncrementalMatcher::find_subgraph_changes(
    const std::unordered_set<Position>& sps,
    const PositionToSubgraphs& last_sg_by_sp,
    const PositionToSubgraphs& curr_sg_by_sp) const {

    Subgraphs added_subgraphs;
    ChangedSubgraphs changed_subgraphs;
    Subgraphs removed_subgraphs;

    for (const Position& sp : sps) {
        const auto& all_last_sgs = find_subgraphs(last_sg_by_sp, sp);
        const auto& all_curr_sgs = find_subgraphs(curr_sg_by_sp, sp);
        if (all_last_sgs == all_curr_sgs) {
            continue;
        }

        std::map<std::reference_wrapper<const Refs>, SubgraphsWithRefs, std::less<Refs>> sgs_by_refs;
        for (const Subgraph& sg : all_last_sgs) {
            sgs_by_refs[sg.refs].last.push_back(&sg);
        }
        for (const Subgraph& sg : all_curr_sgs) {
            sgs_by_refs[sg.refs].curr.push_back(&sg);
        }

        for (const auto& [refs, sgs] : sgs_by_refs) {
            if (sgs.last.size() == 1 && sgs.curr.size() == 1) {
                if (*sgs.last.front() != *sgs.curr.front()) {
                    changed_subgraphs.emplace(*sgs.last.front(), *sgs.curr.front());
                }
                continue;
            }
            add_difference(sgs.last, sgs.curr, removed_subgraphs);
            add_difference(sgs.curr, sgs.last, added_subgraphs);
        }
    }

    return {std::move(added_subgraphs), std::move(changed_subgraphs), std::move(removed_subgraphs)};
}

IncrementalUpdate IncrementalMatcher::match_incrementally(const Observations& os) {
    auto start_positions = iter_start_positions_needing_rematch(os);

    std::vector<Position> sp_vec(start_positions.begin(), start_positions.end());
    StartPositions sp_struct{std::move(sp_vec), {}};

    QuerySet curr_qs = matcher_.match(query_, sp_struct);

    spdlog::debug("Incremental matcher triggered with {} observations, matched from {} start positions, found {} subgraphs", BRIGHT_WHITE(os.size()), BRIGHT_WHITE(start_positions.size()), BRIGHT_WHITE(curr_qs.subgraphs.size()));

    PositionToSubgraphs curr_sg_by_sp = calc_sg_by_sp(std::move(curr_qs.subgraphs));
    auto changes = find_subgraph_changes(
        start_positions, last_sp_to_sgs_, curr_sg_by_sp
    );

    // Deps that nothing depends on anymore are removed; deps that nothing depended on are added.
    Observables added_deps;
    ObservableSet unused_deps;
    clear_deps_for_sps(start_positions, unused_deps);
    fill_deps(curr_qs.deps, added_deps, unused_deps);
    Observables removed_deps;
    for (const Observable& d : unused_deps) {
        dep_to_positions_.erase(d);
        removed_deps.push_back(d);
    }

    for (const auto& sp : start_positions) {
        last_sp_to_sgs_.erase(sp);
    }
    for (auto& [sp, sgs] : curr_sg_by_sp) {
        last_sp_to_sgs_[sp] = std::move(sgs);
    }

    return {
        std::move(added_deps),
        std::move(removed_deps),
        std::move(changes.added_subgraphs),
        std::move(changes.changed_subgraphs),
        std::move(changes.removed_subgraphs)
    };
}

}
