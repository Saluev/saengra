#include "debug.h"
#include "matcher.h"
#include <algorithm>
#include <boost/dynamic_bitset.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>
#include <unordered_set>

namespace saengra {

// ============================================================================
// Helper functions
// ============================================================================

template<typename T>
static void sort_and_deduplicate(std::vector<T>& items) {
    std::sort(items.begin(), items.end());
    items.erase(std::unique(items.begin(), items.end()), items.end());
}

Subgraph SubgraphDraft::finalize() && {
    sort_and_deduplicate(end_positions);
    sort_and_deduplicate(vertices);
    sort_and_deduplicate(edges);
    return Subgraph{start_position, std::move(end_positions), std::move(vertices), std::move(edges), std::move(refs)};
}

QuerySet QuerySetDraft::finalize() && {
    // Sorted subgraphs are grouped by start positions, see Subgraph::operator<.
    std::vector<Subgraph> finalized_subgraphs;
    finalized_subgraphs.reserve(subgraphs.size());
    for (auto& draft : subgraphs) {
        finalized_subgraphs.push_back(std::move(draft).finalize());
    }
    sort_and_deduplicate(finalized_subgraphs);

    return QuerySet{std::move(finalized_subgraphs), std::move(deps), std::move(start_deps)};
}

// ============================================================================
// Match visitor
// ============================================================================

// What the caller needs from the matched subgraphs.
enum class MatchMode {
    Normal,    // entire subgraphs
    First,     // only whether anything matches, so the first match is enough; its contents aren't used
    RefsOnly,  // all matches, but only their refs and end positions, not vertices and edges
};

// Matches a plan. In First mode, matching stops as soon as a subgraph is found. Dependencies of
// the subgraphs found by then are enough: the result can only change once they stop matching.
template<MatchMode Mode>
class MatchVisitor : public boost::static_visitor<void> {
    static constexpr bool collects_contents = Mode == MatchMode::Normal;
    // Mode for operands whose matches are all needed, but whose contents are needed only in Normal mode.
    static constexpr MatchMode all_matches_mode = collects_contents ? MatchMode::Normal : MatchMode::RefsOnly;
    // Mode for operands whose contents are never used.
    static constexpr MatchMode no_contents_mode = Mode == MatchMode::First ? MatchMode::First : MatchMode::RefsOnly;

public:
    MatchVisitor(const PreprocessedQuery& query, const Start& start, QuerySetDraft& dest)
        : query_(query), start_(start), dest_(dest), base_(dest.subgraphs.size()) {}

    void operator()(const MatchVertex& plan) const {
        Refs new_refs;
        const auto match = match_vertex(plan, start_.position, start_.refs, new_refs);
        if (match == VertexMatch::None) {
            return;
        }
        auto& subgraph = dest_.subgraphs.emplace_back(SubgraphDraft{
            start_.origin,
            {start_.position.with_kind(PositionKind::ORBIT)},
            {},
            {},
            match == VertexMatch::NewRefs ? std::move(new_refs) : start_.refs,
        });
        if constexpr (collects_contents) {
            subgraph.vertices.push_back(start_.position.vertex_id);
        }
    }

    void operator()(const MatchEdge& plan) const {
        for_each_edge_match(plan, start_.position, [this](const Edge* edges, size_t count, VertexID to) {
            auto& subgraph = dest_.subgraphs.emplace_back(SubgraphDraft{
                start_.position,
                {Position(to, PositionKind::CORE)},
                {},
                {},
                start_.refs,
            });
            if constexpr (collects_contents) {
                subgraph.edges.assign(edges, edges + count);
            }
            return enough();
        });
    }

    void operator()(const MatchNoop& plan) const {
        // Noop matches an empty graph at the current position
        dest_.subgraphs.push_back(SubgraphDraft{start_.origin, {start_.position}, {}, {}, start_.refs});
    }

    void operator()(const MatchPathToVertex& plan) const {
        auto lookup = [this, &plan](const Position& start_position, const Position& position, const Refs& refs,
                                    const std::vector<VertexID>& vertices, const std::vector<Edge>& edges) {
            return match_edge_to_vertex(plan.edge, plan.vertex, start_position, position, refs, vertices, edges);
        };
        const size_t length = plan.path.vertices.size() + plan.path.edges.size();
        const bool from_core = plan.path.start == PositionKind::CORE;
        switch (length) {
            case 0: lookup(start_.position, start_.position, start_.refs, {}, {}); return;
            case 1: return from_core ? match_path<1, true>(plan.path, lookup) : match_path<1, false>(plan.path, lookup);
            case 2: return from_core ? match_path<2, true>(plan.path, lookup) : match_path<2, false>(plan.path, lookup);
            case 3: return from_core ? match_path<3, true>(plan.path, lookup) : match_path<3, false>(plan.path, lookup);
        }
        static_assert(MatchPath::max_length == 5);
        throw std::runtime_error("unsupported path length " + std::to_string(length) + " before an edge to a known vertex");
    }

    void operator()(const MatchPath& plan) const {
        auto emit = [this](const Position& start_position, const Position& position, const Refs& refs,
                           const std::vector<VertexID>& vertices, const std::vector<Edge>& edges) {
            dest_.subgraphs.push_back(SubgraphDraft{start_position, {position}, vertices, edges, refs});
            return enough();
        };
        const size_t length = plan.vertices.size() + plan.edges.size();
        const bool from_core = plan.start == PositionKind::CORE;
        switch (length) {
            case 2: return from_core ? match_path<2, true>(plan, emit) : match_path<2, false>(plan, emit);
            case 3: return from_core ? match_path<3, true>(plan, emit) : match_path<3, false>(plan, emit);
            case 4: return from_core ? match_path<4, true>(plan, emit) : match_path<4, false>(plan, emit);
            case 5: return from_core ? match_path<5, true>(plan, emit) : match_path<5, false>(plan, emit);
        }
        static_assert(MatchPath::max_length == 5);
        throw std::runtime_error("unsupported path length " + std::to_string(length));
    }

    void operator()(const MatchConcatenation& plan) const {
        match_concatenation_tail(plan, plan.operands.begin());
    }

    void match_concatenation_tail(const MatchConcatenation& plan, Plans::const_iterator it) const {
        if (it + 1 == plan.operands.end()) {
            boost::apply_visitor(*this, *it);
            return;
        }

        // Any subgraph of the head may be the one continued by the tail, so all of them are needed.
        QuerySetDraft start_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor<all_matches_mode>(query_, start_, start_qs), *it);

        for (const auto& start_sg : start_qs.subgraphs) {
            for (const auto& end_pos : start_sg.end_positions) {
                QuerySetDraft tail_qs{dest_.deps};
                Start tail_start(start_.origin, end_pos, start_sg.refs);
                MatchVisitor(query_, tail_start, tail_qs).match_concatenation_tail(plan, it + 1);

                for (auto& tail_sg : tail_qs.subgraphs) {
                    dest_.subgraphs.push_back(concat_subgraph(start_sg, tail_sg));
                }
                if (enough()) {
                    return;
                }
            }
        }
    }

    SubgraphDraft concat_subgraph(const SubgraphDraft& start_sg, SubgraphDraft& tail_sg) const {
        tail_sg.start_position = start_sg.start_position;

        std::vector<VertexID>& vertices = tail_sg.vertices;
        vertices.reserve(start_sg.vertices.size() + tail_sg.vertices.size());
        vertices.insert(vertices.end(), start_sg.vertices.begin(), start_sg.vertices.end());

        std::vector<Edge>& edges = tail_sg.edges;
        edges.reserve(start_sg.edges.size() + tail_sg.edges.size());
        edges.insert(edges.end(), start_sg.edges.begin(), start_sg.edges.end());

        return std::move(tail_sg);
    }

    void operator()(const MatchOr& plan) const {
        for (const Plan& operand : plan.operands) {
            boost::apply_visitor(*this, operand);
            if (enough()) {
                return;
            }
        }
    }

    void operator()(const MatchAndWithoutSetRef& plan) const {
        // All subgraphs have the refs of the start, so they're all join-compatible.
        QuerySetDraft operand_qs{dest_.deps};
        for (const Plan& operand : plan.operands) {
            const auto size_before = operand_qs.subgraphs.size();
            boost::apply_visitor(MatchVisitor(query_, start_, operand_qs), operand);
            if (operand_qs.subgraphs.size() == size_before) {
                // Nothing matched for one of the operands — quick exit.
                return;
            }
        }
        std::move(operand_qs.subgraphs.begin(), operand_qs.subgraphs.end(), std::back_inserter(dest_.subgraphs));
    }

    void operator()(const MatchAnd& plan) const {
        QuerySetDraft operand_qs{dest_.deps};
        std::vector<size_t> operand_idx;
        if (!match_operands(plan.operands, operand_qs, operand_idx)) {
            return;
        }

        // Now we group subgraphs the same way (all:) does, but don't merge them.

        std::map<Refs, std::vector<size_t>> subgraph_idxs_by_refs;
        for (size_t j = 0; j < operand_qs.subgraphs.size(); ++j) {
            subgraph_idxs_by_refs[operand_qs.subgraphs[j].refs].push_back(j);
        }

        if (subgraph_idxs_by_refs.size() == 1) {
            // All matched subgraphs share exactly the same refs — means
            // they are all join-compatible and should be returned.
            std::move(operand_qs.subgraphs.begin(), operand_qs.subgraphs.end(), std::back_inserter(dest_.subgraphs));
            return;
        }

        std::vector<Refs> distinct_refs;
        std::vector<const std::vector<size_t>*> idxs_by_distinct_refs;
        for (const auto& [refs, idxs] : subgraph_idxs_by_refs) {
            distinct_refs.push_back(refs);
            idxs_by_distinct_refs.push_back(&idxs);
        }

        for (const auto& cell : arrange(distinct_refs)) {
            if (!covers_all_operands(cell, idxs_by_distinct_refs, operand_idx, plan.operands.size())) {
                // In this part of the refspace, one of the operands doesn't match.
                continue;
            }
            for (const Refs& refs : cell.terms) {
                for (size_t r : cell.members) {
                    for (size_t j : *idxs_by_distinct_refs[r]) {
                        // A subgraph may belong to several cells, so copy it.
                        SubgraphDraft sg = operand_qs.subgraphs[j];
                        sg.refs = refs;
                        dest_.subgraphs.push_back(std::move(sg));
                    }
                }
            }
        }
    }

    void operator()(const MatchAllAnd& plan) const {
        // Same as (all: ) of the & operator, but each part of the refspace where all operands match
        // is merged right away.
        QuerySetDraft operand_qs{dest_.deps};
        std::vector<size_t> operand_idx;
        if (!match_operands(plan.operands, operand_qs, operand_idx)) {
            return;
        }

        std::map<Refs, std::vector<size_t>> subgraph_idxs_by_refs;
        for (size_t j = 0; j < operand_qs.subgraphs.size(); ++j) {
            subgraph_idxs_by_refs[operand_qs.subgraphs[j].refs].push_back(j);
        }

        if (subgraph_idxs_by_refs.size() == 1) {
            dest_.subgraphs.push_back(merge(subgraph_idxs_by_refs.begin()->first, {&operand_qs.subgraphs}));
            return;
        }

        std::vector<Refs> distinct_refs;
        std::vector<const std::vector<size_t>*> idxs_by_distinct_refs;
        std::vector<std::vector<SubgraphDraft>> subgraphs_by_distinct_refs;
        for (const auto& [refs, idxs] : subgraph_idxs_by_refs) {
            distinct_refs.push_back(refs);
            idxs_by_distinct_refs.push_back(&idxs);
            auto& subgraphs = subgraphs_by_distinct_refs.emplace_back();
            for (size_t j : idxs) {
                subgraphs.push_back(std::move(operand_qs.subgraphs[j]));
            }
        }

        for (auto& cell : arrange(distinct_refs)) {
            if (!covers_all_operands(cell, idxs_by_distinct_refs, operand_idx, plan.operands.size())) {
                continue;
            }
            std::vector<const std::vector<SubgraphDraft>*> subgraph_lists;
            for (size_t r : cell.members) {
                subgraph_lists.push_back(&subgraphs_by_distinct_refs[r]);
            }
            for (Refs& refs : cell.terms) {
                dest_.subgraphs.push_back(merge(std::move(refs), subgraph_lists));
            }
        }
    }

    void operator()(const MatchAllWithoutSetRef& plan) const {
        // All subgraphs have the refs of the start, so they're merged into one.
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor(query_, start_, operand_qs), plan.operand);
        if (operand_qs.subgraphs.empty()) {
            return;
        }
        dest_.subgraphs.push_back(merge(start_.refs, {&operand_qs.subgraphs}));
    }

    void operator()(const MatchAll& plan) const {
        // The result is empty if and only if the operand doesn't match, so the operand is matched in the same mode.
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor(query_, start_, operand_qs), plan.operand);

        if (operand_qs.subgraphs.empty()) {
            return;
        }

        std::map<Refs, std::vector<SubgraphDraft>> subgraphs_by_refs;
        for (auto& sg : operand_qs.subgraphs) {
            subgraphs_by_refs[sg.refs].push_back(std::move(sg));
        }

        if (subgraphs_by_refs.size() == 1) {
            const auto& [refs, sgs] = *subgraphs_by_refs.begin();
            dest_.subgraphs.push_back(merge(refs, {&sgs}));
            return;
        }

        // Every point of the refspace gets all subgraphs whose refs cover it, merged. Points
        // covered by the same subgraphs form a cell of the arrangement of the subgraphs' refs.
        std::vector<Refs> distinct_refs;
        std::vector<const std::vector<SubgraphDraft>*> subgraphs_by_distinct_refs;
        for (const auto& [refs, sgs] : subgraphs_by_refs) {
            distinct_refs.push_back(refs);
            subgraphs_by_distinct_refs.push_back(&sgs);
        }

        for (auto& cell : arrange(distinct_refs)) {
            std::vector<const std::vector<SubgraphDraft>*> subgraph_lists;
            for (size_t r : cell.members) {
                subgraph_lists.push_back(subgraphs_by_distinct_refs[r]);
            }
            for (Refs& refs : cell.terms) {
                dest_.subgraphs.push_back(merge(std::move(refs), subgraph_lists));
            }
        }
    }

    void operator()(const MatchIfWithoutSetRef& plan) const {
        // All subgraphs have the refs of the start, so one is enough.
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor<MatchMode::First>(query_, start_, operand_qs), plan.operand);
        if (operand_qs.subgraphs.empty()) {
            return;
        }
        auto& operand_sg = operand_qs.subgraphs.front();
        operand_sg.end_positions = {start_.position};
        operand_sg.vertices.clear();
        operand_sg.edges.clear();
        dest_.subgraphs.push_back(std::move(operand_sg));
    }

    void operator()(const MatchIf& plan) const {
        // Only refs are returned, and in First mode, only whether the operand matches matters.
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor<no_contents_mode>(query_, start_, operand_qs), plan.operand);

        std::set<Refs> different_refs;
        for (auto& operand_sg : operand_qs.subgraphs) {
            auto [it, inserted] = different_refs.insert(operand_sg.refs);
            if (!inserted) {
                continue;
            }
            operand_sg.end_positions = {start_.position};
            operand_sg.vertices.clear();
            operand_sg.edges.clear();
            dest_.subgraphs.push_back(std::move(operand_sg));
        }
    }

    void operator()(const MatchUnlessWithoutSetNonLocalRef& plan) const {
        // Any subgraph matches without narrowing the refs, so one is enough for the negation to fail.
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor<MatchMode::First>(query_, start_, operand_qs), plan.operand);
        if (operand_qs.subgraphs.empty()) {
            dest_.subgraphs.push_back(SubgraphDraft{start_.position, {start_.position}, {}, {}, start_.refs});
        }
    }

    void operator()(const MatchUnless& plan) const {
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor<MatchMode::RefsOnly>(query_, start_, operand_qs), plan.operand);

        // The negation holds in the part of the refspace where the operand doesn't match.
        // Refs local to the operand are projected away: it must match for none of their values.
        std::vector<Refs> terms{start_.refs};
        for (const auto& operand_sg : operand_qs.subgraphs) {
            if (operand_sg.refs == start_.refs) {
                // The operand matched without narrowing the refs, so the negation fails everywhere.
                terms.clear();
                break;
            }
            terms = subtract_from_union(terms, operand_sg.refs.restricted_to(plan.shared_refs));
            if (terms.empty()) {
                break;
            }
        }
        for (Refs& refs : terms) {
            dest_.subgraphs.push_back(SubgraphDraft{start_.position, {start_.position}, {}, {}, std::move(refs)});
        }
    }

    void operator()(const MatchSkip& plan) const {
        QuerySetDraft operand_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor<no_contents_mode>(query_, start_, operand_qs), plan.operand);

        // TODO remember dest_.subgraphs.size(), call without operand_qs and modify new subgraphs?
        dest_.subgraphs.reserve(dest_.subgraphs.size() + operand_qs.subgraphs.size());
        for (auto& operand_sg : operand_qs.subgraphs) {
            operand_sg.vertices.clear();
            operand_sg.edges.clear();
            dest_.subgraphs.push_back(std::move(operand_sg));
        }
    }

    void operator()(const MatchMaybe& plan) const {
        auto num_before = dest_.subgraphs.size();
        boost::apply_visitor(*this, plan.operand);
        auto num_after = dest_.subgraphs.size();
        if (num_before == num_after) {
            dest_.subgraphs.push_back(SubgraphDraft{start_.position, {start_.position}, {}, {}, start_.refs});
        }
    }

    void operator()(const MatchRepetition& plan) const {
        throw std::runtime_error("RepetitionExpr is not implemented");
    }

private:
    const PreprocessedQuery& query_;
    const Start& start_;
    QuerySetDraft& dest_;
    const size_t base_;  // subgraphs in dest_ before matching

    bool enough() const {
        if constexpr (Mode == MatchMode::First) {
            return dest_.subgraphs.size() > base_;
        } else {
            return false;
        }
    }

    enum class VertexMatch {
        None,      // the vertex doesn't match
        SameRefs,  // the vertex matches with the same refs
        NewRefs,   // the vertex matches and sets a ref
    };

    // Matches the vertex at the position. If it sets a ref, the new refs are put to `new_refs`.
    VertexMatch match_vertex(const MatchVertex& plan, const Position& position, const Refs& refs, Refs& new_refs) const {
        if (position.kind != PositionKind::CORE) {
            return VertexMatch::None;
        }

        const auto vertex_id = position.vertex_id;
        const auto& vertex = *vertex_id;
        if (!vertex.present) {
            return VertexMatch::None;
        }

        if (plan.placeholder_idx.has_value()) {
            const auto expected = query_.placeholder_value_ids[plan.placeholder_idx.value()];
            if (!expected.has_value()) {
                // Placeholder is not in graph (and never has been) — so it can't match the current vertex.
                return VertexMatch::None;
            }
            if (vertex_id != expected.value()) {
                // Placeholder is present in the graph, but has different vertex ID — can't match.
                return VertexMatch::None;
            }
        }

        if (plan.type_name.has_value()) {
            if (vertex.type_name != plan.type_name.value()) {
                return VertexMatch::None;
            }
        }

        auto match = VertexMatch::SameRefs;
        const auto& ref = plan.set_ref.has_value() ? plan.set_ref : plan.match_ref;
        if (ref.has_value()) {
            switch (refs.binding(ref.value(), vertex_id)) {
                case Refs::Binding::Impossible:
                    // The ref is set to another vertex, or the vertex is excluded.
                    return VertexMatch::None;
                case Refs::Binding::Possible:
                    new_refs = refs.copy_and_set(ref.value(), vertex_id);
                    match = VertexMatch::NewRefs;
                    break;
                case Refs::Binding::AlreadySet:
                    break;
            }
        }

        if (start_.origin == position) {
            dest_.deps.emplace_back(ParticularVertex(VertexData(vertex.type_name, vertex.value)), start_.origin);
        }
        return match;
    }

    // Calls f(edges, count, to) for every match of the edge from the position: one edge, or
    // two for bidirectional edges, leading to vertex `to`. Stops and returns true once f does.
    template<typename F>
    bool for_each_edge_match(const MatchEdge& plan, const Position& position, F&& f) const {
        if (position.kind != PositionKind::ORBIT) {
            return false;
        }
        const VertexID from = position.vertex_id;

        if (plan.direction == Direction::Both) {
            add_edge_deps<EdgeFrom, EdgeFromWithLabel>(plan, from);
            add_edge_deps<EdgeTo, EdgeToWithLabel>(plan, from);
            auto all_from = iter_all_from(plan, from);
            auto all_to = iter_all_to(plan, from);
            auto reversed_to = reverse_all(all_to);
            for (const auto& edge_from : sort_and_intersect(all_from, reversed_to)) {
                const Edge edges[2] = {edge_from, edge_from.reverse()};
                if (f(edges, 2, edge_from.to)) {
                    return true;
                }
            }
            return false;
        }

        if (plan.direction != Direction::Backward) {  // Forward or Any
            add_edge_deps<EdgeFrom, EdgeFromWithLabel>(plan, from);
            for (const auto& edge : iter_all_from(plan, from)) {
                if (f(&edge, 1, edge.to)) {
                    return true;
                }
            }
        }

        if (plan.direction != Direction::Forward) {  // Backward or Any
            add_edge_deps<EdgeTo, EdgeToWithLabel>(plan, from);
            for (const auto& edge : iter_all_to(plan, from)) {
                if (f(&edge, 1, edge.from)) {
                    return true;
                }
            }
        }
        return false;
    }

    Edges iter_all_from(const MatchEdge& plan, VertexID from) const {
        const auto& edges_ = query_.graph.get_edges();
        return (
            plan.labels.empty()
            ? edges_.iter_present_from_vertex(from)
            : edges_.iter_present_from_vertex_via_labels(from, plan.labels)
        );
    }

    Edges iter_all_to(const MatchEdge& plan, VertexID to) const {
        const auto& edges_ = query_.graph.get_edges();
        return (
            plan.labels.empty()
            ? edges_.iter_present_to_vertex(to)
            : edges_.iter_present_to_vertex_via_labels(to, plan.labels)
        );
    }

    void add_edge_deps(const MatchEdge& plan, VertexID vertex_id) const {
        if (plan.direction != Direction::Backward) {
            add_edge_deps<EdgeFrom, EdgeFromWithLabel>(plan, vertex_id);
        }
        if (plan.direction != Direction::Forward) {
            add_edge_deps<EdgeTo, EdgeToWithLabel>(plan, vertex_id);
        }
    }

    // Deps on the edges between two vertices, which are all that matching an edge between them depends on.
    void add_particular_edge_deps(const MatchEdge& plan, VertexID from, VertexID to) const {
        if (plan.labels.empty()) {
            // Edges with any label can't be listed, so the deps are on all edges of the vertex.
            add_edge_deps(plan, from);
            return;
        }
        for (const auto label : plan.labels) {
            if (plan.direction != Direction::Backward) {
                dest_.deps.emplace_back(ParticularEdge{Edge{from, label, to}}, start_.origin);
            }
            if (plan.direction != Direction::Forward) {
                dest_.deps.emplace_back(ParticularEdge{Edge{to, label, from}}, start_.origin);
            }
        }
    }

    template<typename EdgeObservable, typename EdgeWithLabelObservable>
    void add_edge_deps(const MatchEdge& plan, VertexID vertex_id) const {
        if (plan.labels.empty()) {
            dest_.deps.emplace_back(EdgeObservable{vertex_id}, start_.origin);
            return;
        }
        for (const auto label : plan.labels) {
            dest_.deps.emplace_back(EdgeWithLabelObservable{vertex_id, label}, start_.origin);
        }
    }

    // Matches path items in nested loops, keeping the vertices and edges matched so far (only in Normal
    // mode). Once all items match, calls last(start_position, position, refs, vertices, edges), which
    // returns true once enough subgraphs are matched.
    template<size_t N, bool FromCore, typename Last>
    void match_path(const MatchPath& plan, Last& last) const {
        std::vector<VertexID> vertices;
        std::vector<Edge> edges;
        if constexpr (collects_contents) {
            vertices.reserve(plan.vertices.size() + 1);
            edges.reserve(plan.edges.size() * 2 + 2);
        }
        match_path_item<0, N, FromCore>(plan, start_.position, start_.refs, vertices, edges, last);
    }

    // Returns true once enough subgraphs are matched.
    template<size_t I, size_t N, bool FromCore, typename Last>
    bool match_path_item(const MatchPath& plan, const Position& position, const Refs& refs,
                         std::vector<VertexID>& vertices, std::vector<Edge>& edges, Last& last) const {
        if constexpr (I == N) {
            return last(FromCore ? start_.origin : start_.position, position, refs, vertices, edges);
        } else if constexpr ((I % 2 == 0) == FromCore) {
            Refs new_refs;
            const auto match = match_vertex(plan.vertices[I / 2], position, refs, new_refs);
            if (match == VertexMatch::None) {
                return false;
            }
            if constexpr (collects_contents) {
                vertices.push_back(position.vertex_id);
            }
            const bool stop = match_path_item<I + 1, N, FromCore>(
                plan, position.with_kind(PositionKind::ORBIT), match == VertexMatch::NewRefs ? new_refs : refs, vertices, edges, last);
            if constexpr (collects_contents) {
                vertices.pop_back();
            }
            return stop;
        } else {
            return for_each_edge_match(plan.edges[I / 2], position, [&](const Edge* matched, size_t count, VertexID to) {
                if constexpr (collects_contents) {
                    edges.insert(edges.end(), matched, matched + count);
                }
                const bool stop = match_path_item<I + 1, N, FromCore>(
                    plan, Position(to, PositionKind::CORE), refs, vertices, edges, last);
                if constexpr (collects_contents) {
                    edges.erase(edges.end() - count, edges.end());
                }
                return stop;
            });
        }
    }

    // Matches an edge from the position to a known vertex, appending them to the vertices and edges
    // matched before. Returns true once enough subgraphs are matched.
    bool match_edge_to_vertex(const MatchEdge& edge, const MatchVertex& vertex, const Position& start_position,
                              const Position& position, const Refs& refs,
                              const std::vector<VertexID>& vertices, const std::vector<Edge>& edges) const {
        if (position.kind != PositionKind::ORBIT) {
            return false;
        }

        auto emit = [&](VertexID to, const Edge* matched, size_t count, const Refs& new_refs) {
            auto& subgraph = dest_.subgraphs.emplace_back(SubgraphDraft{
                start_position,
                {Position(to, PositionKind::ORBIT)},
                {},
                {},
                new_refs,
            });
            if constexpr (collects_contents) {
                subgraph.vertices.reserve(vertices.size() + 1);
                subgraph.vertices.assign(vertices.begin(), vertices.end());
                subgraph.vertices.push_back(to);
                subgraph.edges.reserve(edges.size() + count);
                subgraph.edges.assign(edges.begin(), edges.end());
                subgraph.edges.insert(subgraph.edges.end(), matched, matched + count);
            }
            return enough();
        };

        std::optional<VertexID> to;
        if (vertex.placeholder_idx.has_value()) {
            const auto placeholder_idx = vertex.placeholder_idx.value();
            to = query_.placeholder_value_ids[placeholder_idx];
            if (!to.has_value()) {
                // Placeholder is not in graph (and never has been) — so no edge can lead to it until it's added.
                dest_.deps.emplace_back(ParticularVertex(query_.placeholder_values[placeholder_idx]), start_.origin);
                return false;
            }
        } else {
            for (const auto& [name, vertex_id] : refs.positives()) {
                if (name == vertex.match_ref.value()) {
                    to = vertex_id;
                    break;
                }
            }
            if (!to.has_value()) {
                // The planner guarantees the ref is set, but just in case, match all edges.
                return for_each_edge_match(edge, position, [&](const Edge* matched, size_t count, VertexID to) {
                    Refs new_refs;
                    const auto match = match_vertex(vertex, Position(to, PositionKind::CORE), refs, new_refs);
                    if (match == VertexMatch::None) {
                        return false;
                    }
                    return emit(to, matched, count, match == VertexMatch::NewRefs ? new_refs : refs);
                });
            }
        }

        const VertexID from = position.vertex_id;
        add_particular_edge_deps(edge, from, *to);
        const auto& graph_edges = query_.graph.get_edges();
        std::vector<Edge> matched;
        switch (edge.direction) {
            case Direction::Forward:
                matched = graph_edges.iter_present_from_vertex_to_vertex(from, *to, edge.labels);
                break;
            case Direction::Backward:
                matched = graph_edges.iter_present_from_vertex_to_vertex(*to, from, edge.labels);
                break;
            case Direction::Any: {
                matched = graph_edges.iter_present_from_vertex_to_vertex(from, *to, edge.labels);
                auto backward = graph_edges.iter_present_from_vertex_to_vertex(*to, from, edge.labels);
                matched.insert(matched.end(), backward.begin(), backward.end());
                break;
            }
            case Direction::Both: {
                auto forward = graph_edges.iter_present_from_vertex_to_vertex(from, *to, edge.labels);
                auto backward = reverse_all(graph_edges.iter_present_from_vertex_to_vertex(*to, from, edge.labels));
                for (const Edge& e : sort_and_intersect(forward, backward)) {
                    matched.push_back(e);
                    matched.push_back(e.reverse());
                }
                break;
            }
        }
        if (matched.empty()) {
            return false;
        }

        Refs new_refs;
        const auto match = match_vertex(vertex, Position(*to, PositionKind::CORE), refs, new_refs);
        if (match == VertexMatch::None) {
            return false;
        }
        const Refs& result_refs = match == VertexMatch::NewRefs ? new_refs : refs;
        const size_t edges_per_match = edge.direction == Direction::Both ? 2 : 1;
        for (size_t i = 0; i < matched.size(); i += edges_per_match) {
            if (emit(*to, matched.data() + i, edges_per_match, result_refs)) {
                return true;
            }
        }
        return false;
    }

    // Matches operands into one query set, remembering the operand of each subgraph. Returns false
    // if one of the operands doesn't match.
    bool match_operands(const Plans& operands, QuerySetDraft& operand_qs, std::vector<size_t>& operand_idx) const {
        MatchVisitor<all_matches_mode> operand_visitor(query_, start_, operand_qs);
        for (size_t i = 0; i < operands.size(); ++i) {
            const auto size_before = operand_qs.subgraphs.size();
            boost::apply_visitor(operand_visitor, operands[i]);
            const auto size_after = operand_qs.subgraphs.size();
            if (size_before == size_after) {
                // Nothing matched for one of the operands — quick exit.
                return false;
            }
            operand_idx.resize(size_after, i);
        }
        return true;
    }

    static bool covers_all_operands(const RefsCell& cell, const std::vector<const std::vector<size_t>*>& idxs_by_distinct_refs,
                                    const std::vector<size_t>& operand_idx, size_t num_operands) {
        boost::dynamic_bitset<> operands(num_operands);
        for (size_t r : cell.members) {
            for (size_t j : *idxs_by_distinct_refs[r]) {
                operands.set(operand_idx[j]);
            }
        }
        return operands.all();
    }

    SubgraphDraft merge(Refs refs, const std::vector<const std::vector<SubgraphDraft>*>& subgraph_lists) const {
        SubgraphDraft result{
            subgraph_lists[0]->front().start_position, {}, {}, {}, std::move(refs)
        };
        for (const auto* subgraphs : subgraph_lists) {
            for (const auto& subgraph : *subgraphs) {
                result.end_positions.insert(result.end_positions.end(), subgraph.end_positions.begin(), subgraph.end_positions.end());
                result.vertices.insert(result.vertices.end(), subgraph.vertices.begin(), subgraph.vertices.end());
                result.edges.insert(result.edges.end(), subgraph.edges.begin(), subgraph.edges.end());
            }
        }
        return result;
    }
};

// ============================================================================
// Matcher implementation
// ============================================================================

QuerySet Matcher::match(const Query& query, const StartPositions& start_positions) const {
    auto preprocessed_query = preprocess_query(query);

    std::vector<QuerySetObservable> deps;
    QuerySetDraft result{deps};

    // Copy start dependencies
    for (const auto& dep : start_positions.deps) {
        result.start_deps.push_back(dep);
    }

    // Match from each start position
    for (const Position& start_pos : start_positions.positions) {
        Start start{start_pos, start_pos, {}};
        const size_t first = result.subgraphs.size();
        match_(preprocessed_query, query.plan, start, result);
        expand_negatives(query.graph, start_pos, first, result);
    }

    return std::move(result).finalize();
}

void Matcher::expand_negatives(const Graph& graph, const Position& start_pos, size_t first, QuerySetDraft& result) const {
    // Refs of results can only have positive literals, so a ref that is set to anything but some
    // vertices is set to every other vertex of the graph in turn. This is deliberately explosive:
    // a pattern that does this is most likely wrong.
    std::vector<SubgraphDraft> expanded;
    std::set<VertexID> used_values;
    bool expanded_any = false;
    std::vector<VertexID> all_vertices;

    auto it = result.subgraphs.begin() + first;
    while (it != result.subgraphs.end()) {
        if (!it->refs.has_negatives()) {
            ++it;
            continue;
        }
        if (!expanded_any) {
            const auto& present = graph.get_vertices().iter_present();
            all_vertices.assign(present.begin(), present.end());
            expanded_any = true;
        }

        std::vector<Refs> variants{it->refs};
        for (const auto& [name, excluded] : it->refs.excluded_values()) {
            std::vector<Refs> next;
            for (const Refs& refs : variants) {
                for (VertexID vertex_id : all_vertices) {
                    if (std::find(excluded.begin(), excluded.end(), vertex_id) != excluded.end()) {
                        continue;
                    }
                    next.push_back(refs.copy_and_set(name, vertex_id));
                    used_values.insert(vertex_id);
                }
            }
            variants = std::move(next);
        }
        for (Refs& refs : variants) {
            SubgraphDraft sg = *it;
            sg.refs = std::move(refs);
            expanded.push_back(std::move(sg));
        }
        it = result.subgraphs.erase(it);
    }

    if (!expanded_any) {
        return;
    }
    std::move(expanded.begin(), expanded.end(), std::back_inserter(result.subgraphs));

    // Results change whenever a vertex is added, or one of the used vertices is removed.
    result.deps.emplace_back(NewVertex{}, start_pos);
    for (VertexID vertex_id : used_values) {
        const auto& vertex = *vertex_id;
        result.deps.emplace_back(ParticularVertex(VertexData(vertex.type_name, vertex.value)), start_pos);
    }
}

PreprocessedQuery Matcher::preprocess_query(const Query& query) const {
    std::vector<std::optional<VertexID>> placeholder_value_ids;
    for (const auto& placeholder_value : query.placeholder_values) {
        auto vertex_id = query.graph.get_vertices().get_vertex_id(placeholder_value);
        placeholder_value_ids.push_back(vertex_id);
    }
    return PreprocessedQuery{
        query.graph,
        query.placeholder_values,
        placeholder_value_ids,
    };
}

void Matcher::match_(const PreprocessedQuery& query, const Plan& plan, const Start& start, QuerySetDraft& dest) const {
    MatchVisitor<MatchMode::Normal> visitor(query, start, dest);
    boost::apply_visitor(visitor, plan);

//    std::cout << "DEPENDENCIES:" << std::endl;
//    for (const auto& dep : dest.deps) {
//        std::cout << "    " << dep << std::endl;
//    }
}

}  // namespace saengra
