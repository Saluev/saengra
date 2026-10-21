#include "debug.h"
#include "matcher.h"
#include <boost/dynamic_bitset.hpp>
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>
#include <unordered_set>

namespace saengra {

// ============================================================================
// Helper functions
// ============================================================================

Subgraph SubgraphDraft::finalize() const {
    // Deduplicate end_positions
    std::set<Position> unique_end_positions(end_positions.begin(), end_positions.end());
    std::vector<Position> deduped_end_positions(unique_end_positions.begin(), unique_end_positions.end());

    // Deduplicate vertices
    std::set<VertexID> unique_vertices(vertices.begin(), vertices.end());
    std::vector<VertexID> deduped_vertices(unique_vertices.begin(), unique_vertices.end());

    // Deduplicate edges
    std::set<Edge> unique_edges(edges.begin(), edges.end());
    std::vector<Edge> deduped_edges(unique_edges.begin(), unique_edges.end());

    return Subgraph{start_position, deduped_end_positions, deduped_vertices, deduped_edges, refs};
}

QuerySet QuerySetDraft::finalize() const {
    // Finalize and deduplicate subgraphs
    Subgraphs unique_subgraphs;
    for (const auto& draft : subgraphs) {
        unique_subgraphs.insert(draft.finalize());
    }
    std::vector<Subgraph> finalized_subgraphs(unique_subgraphs.begin(), unique_subgraphs.end());

    return QuerySet{finalized_subgraphs, deps, start_deps};
}

// ============================================================================
// Match visitor
// ============================================================================

class MatchVisitor : public boost::static_visitor<void> {
public:
    MatchVisitor(const PreprocessedQuery& query, const Start& start, QuerySetDraft& dest)
        : query_(query), start_(start), dest_(dest) {}

    void operator()(const VertexExpr& expr) const {
        if (start_.position.kind != PositionKind::CORE) {
            return;
        }

        const auto vertex_id = start_.position.vertex_id;
        const auto& vertex = *vertex_id;
        if (!vertex.present) {
            return;
        }

        if (expr.placeholder_idx.has_value()) {
            const auto expected = query_.placeholder_value_ids[expr.placeholder_idx.value()];
            if (!expected.has_value()) {
                // Placeholder is not in graph (and never has been) — so it can't match the current vertex.
                return;
            }
            if (vertex_id != expected.value()) {
                // Placeholder is present in the graph, but has different vertex ID — can't match.
                return;
            }
        }

        if (expr.type_name.has_value()) {
            if (vertex.type_name != expr.type_name.value()) {
                return;
            }
        }

        auto binding = Refs::Binding::AlreadySet;
        if (expr.ref.has_value()) {
            binding = start_.refs.binding(expr.ref.value(), vertex_id);
            if (binding == Refs::Binding::Impossible) {
                // The ref is set to another vertex, or the vertex is excluded.
                return;
            }
        }

        Refs refs = binding == Refs::Binding::Possible
            ? start_.refs.copy_and_set(expr.ref.value(), vertex_id)
            : start_.refs;

        SubgraphDraft subgraph{
            start_.origin,
            {start_.position.with_kind(PositionKind::ORBIT)},
            {start_.position.vertex_id},
            {},
            std::move(refs)
        };

        dest_.subgraphs.push_back(std::move(subgraph));

        if (start_.origin == start_.position) {
            dest_.deps.emplace_back(ParticularVertex(VertexData(vertex.type_name, vertex.value)), start_.origin);
        }
    }

    void operator()(const EdgeExpr& expr) const {
        if (start_.position.kind != PositionKind::ORBIT) {
            return;
        }

        if (expr.direction == Direction::Both) {
            add_edge_deps<EdgeFrom, EdgeFromWithLabel>(expr);
            add_edge_deps<EdgeTo, EdgeToWithLabel>(expr);
            auto all_from = iter_all_from(expr);
            auto all_to = iter_all_to(expr);
            auto reversed_to = reverse_all(all_to);
            auto bidirectional_edges = sort_and_intersect(all_from, reversed_to);
            for (const auto edge_from : bidirectional_edges) {
                dest_.subgraphs.push_back(double_edge_subgraph(edge_from));
            }
            return;
        }

        if (expr.direction != Direction::Backward) {  // Forward or Any
            add_edge_deps<EdgeFrom, EdgeFromWithLabel>(expr);
            for (auto edge : iter_all_from(expr)) {
                dest_.subgraphs.push_back(single_edge_subgraph(edge, edge.to));
            }
        }

        if (expr.direction != Direction::Forward) {  // Backward or Any
            add_edge_deps<EdgeTo, EdgeToWithLabel>(expr);
            for (auto edge : iter_all_to(expr)) {
                dest_.subgraphs.push_back(single_edge_subgraph(edge, edge.from));
            }
        }
    }

    void operator()(const NoopExpr& expr) const {
        // Noop matches an empty graph at the current position
        SubgraphDraft subgraph{
            start_.origin,
            {start_.position},  // End at the same position
            {},  // No vertices
            {},  // No edges
            start_.refs  // Keep refs unchanged
        };
        dest_.subgraphs.push_back(std::move(subgraph));
    }

    Edges iter_all_from(const EdgeExpr& expr) const {
        const auto& edges_ = query_.graph.get_edges();
        return (
            expr.labels.empty()
            ? edges_.iter_present_from_vertex(start_.position.vertex_id)
            : edges_.iter_present_from_vertex_via_labels(start_.position.vertex_id, expr.labels)
        );
    }

    Edges iter_all_to(const EdgeExpr& expr) const {
        const auto& edges_ = query_.graph.get_edges();
        return (
            expr.labels.empty()
            ? edges_.iter_present_to_vertex(start_.position.vertex_id)
            : edges_.iter_present_to_vertex_via_labels(start_.position.vertex_id, expr.labels)
        );
    }

    template<typename EdgeObservable, typename EdgeWithLabelObservable>
    void add_edge_deps(const EdgeExpr& expr) const {
        if (expr.labels.empty()) {
            dest_.deps.emplace_back(EdgeObservable{start_.position.vertex_id}, start_.origin);
            return;
        }
        for (const auto label : expr.labels) {
            dest_.deps.emplace_back(EdgeWithLabelObservable{start_.position.vertex_id, label}, start_.origin);
        }
    }

    SubgraphDraft double_edge_subgraph(const Edge& edge_from) const {
        return SubgraphDraft{
            start_.position,
            {Position(edge_from.to, PositionKind::CORE)},
            {},
            {edge_from, edge_from.reverse()},
            start_.refs,
        };
    }

    SubgraphDraft single_edge_subgraph(const Edge& edge, VertexID to) const {
        return SubgraphDraft{
            start_.position,
            {Position(to, PositionKind::CORE)},
            {},
            {edge},
            start_.refs,
        };
    }

    void operator()(const ConcatenationExpr& expr) const {
        match_concatenation_tail(expr, expr.operands.begin());
    }

    void match_concatenation_tail(const ConcatenationExpr& expr, Expressions::const_iterator it) const {
        if (it + 1 == expr.operands.end()) {
            boost::apply_visitor(*this, *it);
            return;
        }

        QuerySetDraft start_qs{dest_.deps};
        boost::apply_visitor(MatchVisitor(query_, start_, start_qs), *it);

//        std::cout << expr << " ";
//        for (int i = 0; i < it - expr.operands.begin(); i++) std::cout << "  ";
//        std::cout << "START_QS " << start_qs.subgraphs << std::endl;

        for (const auto& start_sg : start_qs.subgraphs) {
            for (const auto& end_pos : start_sg.end_positions) {
                QuerySetDraft tail_qs{dest_.deps};
                Start tail_start(start_.origin, end_pos, start_sg.refs);
                MatchVisitor(query_, tail_start, tail_qs).match_concatenation_tail(expr, it + 1);

//                std::cout << expr << " ";
//                for (int i = 0; i < it - expr.operands.begin(); i++) std::cout << "  ";
//                std::cout << "TAIL_QS " << tail_qs.subgraphs << std::endl;

                for (auto& tail_sg : tail_qs.subgraphs) {
                    dest_.subgraphs.push_back(concat_subgraph(start_sg, tail_sg));
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

//        std::cout << "CONCAT " << tail_sg << std::endl;

        return std::move(tail_sg);
    }

    void operator()(const AndExpr& expr) const {
        QuerySetDraft operand_qs{dest_.deps};
        MatchVisitor operand_visitor(query_, start_, operand_qs);
        std::vector<size_t> operand_idx;
        for (size_t i = 0; i < expr.operands.size(); ++i) {
            const auto& operand = expr.operands[i];
            const auto size_before = operand_qs.subgraphs.size();
            boost::apply_visitor(operand_visitor, operand);
            const auto size_after = operand_qs.subgraphs.size();
            if (size_before == size_after) {
                // Nothing matched for one of the operands — quick exit.
                return;
            }
            operand_idx.resize(size_after, i);
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
            boost::dynamic_bitset<> operands(expr.operands.size());
            for (size_t r : cell.members) {
                for (size_t j : *idxs_by_distinct_refs[r]) {
                    operands.set(operand_idx[j]);
                }
            }
            if (!operands.all()) {
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

    void operator()(const OrExpr& expr) const {
        for (const Expression& operand : expr.operands) {
            boost::apply_visitor(*this, operand);
        }
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

    void operator()(const OperationExpr& expr) const {
        switch (expr.op_type) {
            case OperationType::All: {
                QuerySetDraft operand_qs{dest_.deps};
                MatchVisitor operand_visitor(query_, start_, operand_qs);
                boost::apply_visitor(operand_visitor, expr.operand);

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
                return;
            }
            case OperationType::Skip: {
                QuerySetDraft operand_qs{dest_.deps};
                MatchVisitor operand_visitor(query_, start_, operand_qs);
                boost::apply_visitor(operand_visitor, expr.operand);

                // TODO remember dest_.subgraphs.size(), call without operand_qs and modify new subgraphs?
                dest_.subgraphs.reserve(dest_.subgraphs.size() + operand_qs.subgraphs.size());
                for (auto& operand_sg : operand_qs.subgraphs) {
                    operand_sg.vertices.clear();
                    operand_sg.edges.clear();
                    dest_.subgraphs.push_back(std::move(operand_sg));
                }
                return;
            }
            case OperationType::If: {
                // TODO if isinstance(expr.operand, Or) and not expr.operand.has_set_refs optimization?
                QuerySetDraft operand_qs{dest_.deps};
                MatchVisitor operand_visitor(query_, start_, operand_qs);
                boost::apply_visitor(operand_visitor, expr.operand);

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
                return;
            }
            case OperationType::Unless: {
                QuerySetDraft operand_qs{dest_.deps};
                MatchVisitor operand_visitor(query_, start_, operand_qs);
                boost::apply_visitor(operand_visitor, expr.operand);

                if (operand_qs.subgraphs.empty()) {
                    dest_.subgraphs.push_back(SubgraphDraft{start_.position, {start_.position}, {}, {}, start_.refs});
                }
                return;
            }
            case OperationType::Maybe: {
                auto num_before = dest_.subgraphs.size();
                boost::apply_visitor(*this, expr.operand);
                auto num_after = dest_.subgraphs.size();
                if (num_before == num_after) {
                    dest_.subgraphs.push_back(SubgraphDraft{start_.position, {start_.position}, {}, {}, start_.refs});
                }
                return;
            }
        }
    }

    void operator()(const RepetitionExpr& expr) const {
        throw std::runtime_error("RepetitionExpr is not implemented");
    }

private:
    const PreprocessedQuery& query_;
    const Start& start_;
    QuerySetDraft& dest_;
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
        match_(preprocessed_query, preprocessed_query.expression, start, result);
        expand_negatives(query.graph, start_pos, first, result);
    }

    return result.finalize();
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
        query.expression,
        placeholder_value_ids,
    };
}

void Matcher::match_(const PreprocessedQuery& query, const Expression& expr, const Start& start, QuerySetDraft& dest) const {
    MatchVisitor visitor(query, start, dest);
    boost::apply_visitor(visitor, expr);

//    std::cout << "DEPENDENCIES:" << std::endl;
//    for (const auto& dep : dest.deps) {
//        std::cout << "    " << dep << std::endl;
//    }
}

}  // namespace saengra
