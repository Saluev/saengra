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

// Groups deps by start positions; deps of each start position are sorted and deduplicated.
static std::vector<std::pair<ObservableStartPosition, Observables>> group_deps(QuerySetObservables&& qsos) {
    std::vector<std::pair<ObservableStartPosition, Observables>> groups;
    absl::flat_hash_map<ObservableStartPosition, size_t, std::hash<ObservableStartPosition>> group_by_sp;
    // The matcher matches from one start position at a time, so deps of a start position are
    // usually contiguous: they're moved run by run.
    for (auto run_begin = qsos.begin(); run_begin != qsos.end();) {
        auto run_end = std::find_if(run_begin, qsos.end(), [&run_begin](const QuerySetObservable& d) {
            return !(d.start_position == run_begin->start_position);
        });
        auto [it, inserted] = group_by_sp.try_emplace(run_begin->start_position, groups.size());
        if (inserted) {
            groups.emplace_back(run_begin->start_position, Observables{});
        }
        Observables& deps = groups[it->second].second;
        deps.reserve(deps.size() + (run_end - run_begin));
        for (auto d = run_begin; d != run_end; ++d) {
            deps.push_back(std::move(d->observable));
        }
        run_begin = run_end;
    }
    for (auto& [sp, deps] : groups) {
        std::sort(deps.begin(), deps.end());
        deps.erase(std::unique(deps.begin(), deps.end()), deps.end());
    }
    return groups;
}

Observables IncrementalMatcher::reinitialize() {
    StartPositionFinder finder;
    auto start_positions = finder.find_start_positions(query_);
    auto last_qs = matcher_.match(query_, start_positions);

    last_sp_to_sgs_ = calc_sg_by_sp(std::move(last_qs.subgraphs));
    dep_to_positions_.clear();
    position_to_deps_.clear();

    // Return observables that we have to subscribe to immediately.
    Observables result;
    ObservableSet removed;
    for (auto& [sp, deps] : group_deps(std::move(last_qs.deps))) {
        for (const Observable& dep : deps) {
            subscribe(sp, dep, result, removed);
        }
        if (const auto* position = std::get_if<Position>(&sp)) {
            position_to_deps_[*position] = std::move(deps);
        }
    }
    for (auto& [sp, deps] : group_deps(std::move(last_qs.start_deps))) {
        for (const Observable& dep : deps) {
            subscribe(sp, dep, result, removed);
        }
    }
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

void IncrementalMatcher::subscribe(const ObservableStartPosition& sp, const Observable& dep, Observables& added_deps, ObservableSet& removed_deps) {
    auto& positions = dep_to_positions_[dep];
    const bool was_unused = positions.empty();
    if (positions.insert(sp).second && was_unused && removed_deps.erase(dep) == 0) {
        added_deps.push_back(dep);
    }
}

void IncrementalMatcher::unsubscribe(const Position& sp, const Observable& dep, ObservableSet& removed_deps) {
    auto dep_it = dep_to_positions_.find(dep);
    if (dep_it != dep_to_positions_.end()) {
        dep_it->second.erase(sp);
        if (dep_it->second.empty()) {
            removed_deps.insert(dep);
        }
    }
}

void IncrementalMatcher::resubscribe(const Position& sp, const Observables& old_deps, const Observables& new_deps,
                                     Observables& added_deps, ObservableSet& removed_deps) {
    auto old_it = old_deps.begin();
    auto new_it = new_deps.begin();
    while (old_it != old_deps.end() || new_it != new_deps.end()) {
        if (new_it == new_deps.end() || (old_it != old_deps.end() && *old_it < *new_it)) {
            unsubscribe(sp, *old_it++, removed_deps);
        } else if (old_it == old_deps.end() || *new_it < *old_it) {
            subscribe(sp, *new_it++, added_deps, removed_deps);
        } else {
            ++old_it;
            ++new_it;
        }
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

    // Only deps that changed are updated. Deps that nothing depends on anymore are removed; deps that
    // nothing depended on are added.
    Observables added_deps;
    ObservableSet unused_deps;
    std::unordered_set<Position> sps_with_deps;
    for (auto& [sp, deps] : group_deps(std::move(curr_qs.deps))) {
        const auto* position = std::get_if<Position>(&sp);
        if (position == nullptr) {
            for (const Observable& dep : deps) {
                subscribe(sp, dep, added_deps, unused_deps);
            }
            continue;
        }
        sps_with_deps.insert(*position);
        auto& old_deps = position_to_deps_[*position];
        resubscribe(*position, old_deps, deps, added_deps, unused_deps);
        old_deps = std::move(deps);
    }
    for (const Position& sp : start_positions) {
        if (sps_with_deps.contains(sp)) {
            continue;
        }
        auto it = position_to_deps_.find(sp);
        if (it != position_to_deps_.end()) {
            resubscribe(sp, it->second, {}, added_deps, unused_deps);
            position_to_deps_.erase(it);
        }
    }
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
