#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <unordered_set>
#include "debug.h"
#include "logging.h"
#include "incremental_matcher.h"
#include "start_positions.h"
#include "utility.h"

namespace saengra {

IncrementalMatcher::IncrementalMatcher(const Matcher& matcher, const Query& query)
    : matcher_(matcher), query_(query) {}

// Helper function to group subgraphs by refs (like Python's group_by)
static std::map<Refs, std::set<Subgraph>> group_subgraphs_by_refs(const Subgraphs& subgraphs) {
    std::map<Refs, std::set<Subgraph>> result;
    for (const Subgraph& sg : subgraphs) {
        result[sg.refs].insert(sg);
    }
    return result;
}

Observables IncrementalMatcher::reinitialize() {
    StartPositionFinder finder;
    auto start_positions = finder.find_start_positions(query_);
    auto last_qs = matcher_.match(query_, start_positions);

    last_sp_to_sgs_ = calc_sg_by_sp(last_qs);
    start_deps_ = last_qs.start_deps;
    dep_to_positions_.clear();
    position_to_deps_.clear();
    fill_deps_from_qs(last_qs);

    // Return observables that we have to subscribe to immediately.
    Observables result;
    for (const auto [observable, ps] : dep_to_positions_) {
        result.push_back(observable);
    }
    return result;
}

PositionToSubgraphs IncrementalMatcher::calc_sg_by_sp(const QuerySet& qs) const {
    PositionToSubgraphs sg_by_sp;
    for (const Subgraph& sg : qs.subgraphs) {
        sg_by_sp[sg.start_position].insert(sg);
    }
    return sg_by_sp;
}

void IncrementalMatcher::fill_deps_from_qs(const QuerySet& qs) {
    fill_deps_from_query_set_observables(qs.deps);
    fill_deps_from_query_set_observables(start_deps_);
}

void IncrementalMatcher::fill_deps_from_query_set_observables(const QuerySetObservables& qsos) {
    for (const QuerySetObservable& d : qsos) {
        dep_to_positions_[d.observable].insert(d.start_position);
        if (std::holds_alternative<Position>(d.start_position)) {
            Position pos = std::get<Position>(d.start_position);
            position_to_deps_[pos].push_back(d.observable);
        }
    }
}

ObservableSet IncrementalMatcher::gather_deps_for_sps(const std::unordered_set<Position>& sps) const {
    ObservableSet deps_set;
    for (const QuerySetObservable& d : start_deps_) {
        deps_set.insert(d.observable);
    }
    for (const Position& sp : sps) {
        auto it = position_to_deps_.find(sp);
        if (it != position_to_deps_.end()) {
            for (const Observable& obs : it->second) {
                deps_set.insert(obs);
            }
        }
    }
    return deps_set;
}

void IncrementalMatcher::clear_deps_for_sps(const std::unordered_set<Position>& sps) {
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

SubgraphChanges IncrementalMatcher::find_subgraph_changes(
    const std::unordered_set<Position>& sps,
    const PositionToSubgraphs& last_sg_by_sp,
    const PositionToSubgraphs& curr_sg_by_sp) const {

    Subgraphs added_subgraphs;
    ChangedSubgraphs changed_subgraphs;
    Subgraphs removed_subgraphs;

    for (const Position& sp : sps) {
        auto all_last_sgs = find_or_default(last_sg_by_sp, sp);
        auto all_curr_sgs = find_or_default(curr_sg_by_sp, sp);
//        std::cout << "Start position " << sp << ", last subgraphs " << all_last_sgs << ", current subgraphs " << all_curr_sgs << std::endl;

        auto last_sgs_by_refs = group_subgraphs_by_refs(all_last_sgs);
        auto curr_sgs_by_refs = group_subgraphs_by_refs(all_curr_sgs);

        std::set<Refs> all_refs;
        for (const auto& [refs, _] : last_sgs_by_refs) {
            all_refs.insert(refs);
        }
        for (const auto& [refs, _] : curr_sgs_by_refs) {
            all_refs.insert(refs);
        }

        for (const Refs& refs : all_refs) {
            auto last_sgs = find_or_default(last_sgs_by_refs, refs);
            auto curr_sgs = find_or_default(curr_sgs_by_refs, refs);

            if (last_sgs.size() == 1 && curr_sgs.size() == 1) {
                const Subgraph& last_sg = *last_sgs.begin();
                const Subgraph& curr_sg = *curr_sgs.begin();
                if (last_sg != curr_sg) {
                    changed_subgraphs.emplace(last_sg, curr_sg);
                }
                continue;
            }
            for (const Subgraph& sg : last_sgs) {
                if (!curr_sgs.contains(sg)) {
                    removed_subgraphs.insert(sg);
                }
            }
            for (const Subgraph& sg : curr_sgs) {
                if (!last_sgs.contains(sg)) {
                    added_subgraphs.insert(sg);
                }
            }
        }
    }

//    std::cerr << "Incremental matcher found " << added_subgraphs.size() << " added subgraphs, " << changed_subgraphs.size() << " changed subgraphs, " << removed_subgraphs.size() << " removed subgraphs" << std::endl;

    return {added_subgraphs, changed_subgraphs, removed_subgraphs};
}

IncrementalUpdate IncrementalMatcher::match_incrementally(const Observations& os) {
    auto t0 = std::chrono::steady_clock::now();

    struct Stats {
        uint64_t calls = 0;
        double total_ms = 0.0;
        std::vector<size_t> last_deps_sizes;
        std::vector<size_t> curr_deps_sizes;
    };
    struct StatsDumper {
        std::map<std::string, Stats>& stats;
        ~StatsDumper() {
            const char* path = std::getenv("SAENGRA_INCREMENTAL_MATCHER_TIMING");
            if (!path) return;

            auto quantile = [](std::vector<size_t> v, double q) -> size_t {
                if (v.empty()) return 0;
                std::sort(v.begin(), v.end());
                size_t idx = static_cast<size_t>(q * (v.size() - 1));
                return v[idx];
            };

            std::vector<std::pair<double, std::string>> sorted;
            for (const auto& [key, s] : stats) {
                sorted.emplace_back(s.total_ms, key);
            }
            std::sort(sorted.rbegin(), sorted.rend());
            std::ofstream f(path);
            f << "total_ms\tcalls\tlast_deps p50\tlast_deps p95\tlast_deps max\tcurr_deps p50\tcurr_deps p95\tcurr_deps max\tquery\n";
            for (const auto& [total_ms, key] : sorted) {
                const Stats& s = stats[key];
                f << total_ms << "\t" << s.calls
                  << "\t" << quantile(s.last_deps_sizes, 0.50)
                  << "\t" << quantile(s.last_deps_sizes, 0.95)
                  << "\t" << quantile(s.last_deps_sizes, 1.00)
                  << "\t" << quantile(s.curr_deps_sizes, 0.50)
                  << "\t" << quantile(s.curr_deps_sizes, 0.95)
                  << "\t" << quantile(s.curr_deps_sizes, 1.00)
                  << "\t" << key << "\n";
            }
        }
    };
    static std::map<std::string, Stats> stats;
    static StatsDumper dumper{stats};

    auto start_positions = iter_start_positions_needing_rematch(os);

    std::vector<Position> sp_vec(start_positions.begin(), start_positions.end());
    StartPositions sp_struct{std::move(sp_vec), {}};

    QuerySet curr_qs = matcher_.match(query_, sp_struct);
    PositionToSubgraphs curr_sg_by_sp = calc_sg_by_sp(curr_qs);

//    std::cerr << "Incremental matcher started from " << sp_struct.positions.size() << " start positions and found " << curr_qs.subgraphs.size() << " subgraphs:" << std::endl;
//    for (const Subgraph& sg : curr_qs.subgraphs) {
//        std::cerr << "    " << sg << std::endl;
//    }

    spdlog::debug("Incremental matcher triggered with {} observations, matched from {} start positions, found {} subgraphs", BRIGHT_WHITE(os.size()), BRIGHT_WHITE(start_positions.size()), BRIGHT_WHITE(curr_qs.subgraphs.size()));
//    for (const auto sp : start_positions) {
//        std::cout << "    " << sp << std::endl;
//    }

    auto changes = find_subgraph_changes(
        start_positions, last_sp_to_sgs_, curr_sg_by_sp
    );

    auto last_deps_set = gather_deps_for_sps(start_positions);
    clear_deps_for_sps(start_positions);
    fill_deps_from_qs(curr_qs);
    auto curr_deps_set = gather_deps_for_sps(start_positions);

    for (const auto& sp : start_positions) {
        last_sp_to_sgs_.erase(sp);
    }
    for (const auto& [sp, sgs] : curr_sg_by_sp) {
        last_sp_to_sgs_[sp] = sgs;
    }

    Observables added_deps;
    for (const Observable& d : curr_deps_set) {
        if (last_deps_set.find(d) == last_deps_set.end()) {
            added_deps.push_back(d);
        }
    }

    Observables removed_deps;
    for (const Observable& d : last_deps_set) {
        if (curr_deps_set.find(d) == curr_deps_set.end()) {
            auto it = dep_to_positions_.find(d);
            if (it == dep_to_positions_.end() || it->second.empty()) {
                removed_deps.push_back(d);
            }
        }
    }

    auto result = IncrementalUpdate{
        added_deps,
        removed_deps,
        std::move(changes.added_subgraphs),
        std::move(changes.changed_subgraphs),
        std::move(changes.removed_subgraphs)
    };

    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    auto& s = stats[query_.expression.to_string()];
    s.calls++;
    s.total_ms += ms;
    s.last_deps_sizes.push_back(last_deps_set.size());
    s.curr_deps_sizes.push_back(curr_deps_set.size());

    return result;
}

}
