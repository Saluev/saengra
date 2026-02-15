#include "matcher.h"
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

namespace saengra {

bool is_negative_assertion(const Expression& expr) {
    auto* op_expr = boost::get<OperationExpr>(&expr);
    return op_expr != nullptr && op_expr->op_type == OperationType::Unless;
}

template<typename T>
inline void add_positions(StartPositionsDraft& dest, const T& container, PositionKind kind) {
    for (const auto& pos : container) {
        dest.positions.emplace_back(pos, kind);
    }
}

class FindStartPositionsVisitor : public boost::static_visitor<void> {
public:
    FindStartPositionsVisitor(const Query& query, StartPositionsDraft& dest)
        : query_(query), dest_(dest) {}

    void operator()(const VertexExpr& expr) const {
        const auto& vertices = query_.graph.get_vertices();

        if (expr.placeholder_idx.has_value()) {
            // TODO validate number of placeholders against the expression
            const auto value = query_.placeholder_values[expr.placeholder_idx.value()];
            const auto vertex_id = vertices.get_vertex_id(value);
            if (vertex_id.has_value()) {
                dest_.positions.emplace_back(vertex_id.value(), PositionKind::CORE);
            } else {
                dest_.deps.emplace_back(ParticularVertex{value}, AbsentPosition{value, PositionKind::CORE});
            }
            return;
        }

        if (expr.match_ref.has_value()) {
            // TODO validate expression after parsing
            throw std::runtime_error("first vertex can't be a named reference");
        }

        if (expr.type_name.has_value()) {
            const auto& type_name = expr.type_name.value();
            auto matching_vertices = vertices.iter_present_by_type_name(type_name);
            add_positions(dest_, matching_vertices, PositionKind::CORE);
            dest_.deps.emplace_back(NewVertexOfType{type_name}, EveryAddedVertex{});
        } else {
            auto matching_vertices = vertices.iter_present();
            add_positions(dest_, matching_vertices, PositionKind::CORE);
            dest_.deps.emplace_back(NewVertex{}, EveryAddedVertex{});
        }
    }

    void operator()(const EdgeExpr& expr) const {
        const auto& vertices = query_.graph.get_vertices();

        if (expr.labels.empty()) {
            auto matching_vertices = vertices.iter_present();
            add_positions(dest_, matching_vertices, PositionKind::ORBIT);
            dest_.deps.emplace_back(NewEdge{}, EveryAddedEdge{/* TODO direction?.. */});
            return;
        }

        const auto edges = query_.graph.get_edges().iter_present_via_labels(expr.labels);
        for (const Edge& edge : edges) {
            if (expr.direction != Direction::Backward) {
                dest_.positions.emplace_back(edge.from, PositionKind::ORBIT);
            }
            if (expr.direction != Direction::Forward) {
                dest_.positions.emplace_back(edge.to, PositionKind::ORBIT);
            }
        }

        for (const EdgeLabel& label : expr.labels) {
            dest_.deps.emplace_back(NewEdgeWithLabel{label}, EveryAddedEdge{/* TODO direction?.. */});
        }
    }

    void operator()(const NoopExpr& expr) const {
        // Noop can start at any vertex with any position kind
        // since it matches an empty graph at the current position
        const auto& vertices = query_.graph.get_vertices();
        auto all_vertices = vertices.iter_present();
        add_positions(dest_, all_vertices, PositionKind::CORE);
        add_positions(dest_, all_vertices, PositionKind::ORBIT);
        dest_.deps.emplace_back(NewVertex{}, EveryAddedVertex{});
    }

    void operator()(const ConcatenationExpr& expr) const {
        handle_concatenation_or_and(expr.operands);
    }

    void operator()(const AndExpr& expr) const {
        handle_concatenation_or_and(expr.operands);
    }

    void operator()(const OrExpr& expr) const {
        // For Or, find start positions in all operands
        for (const auto& operand : expr.operands) {
            boost::apply_visitor(*this, operand);
        }
    }

    void operator()(const OperationExpr& expr) const {
        // For operations, recursively process the operand
        if (expr.op_type == OperationType::Unless) {
            // NegativeAssertion - not implemented
            throw std::runtime_error("finding start positions for negative assertions not implemented");
        }

        // For other operations (All/Join, If/Assertion, Skip/NonCapturing, Maybe/Optional),
        // just process the operand
        boost::apply_visitor(*this, expr.operand);
    }

    void operator()(const RepetitionExpr& expr) const {
        // For repetition, process the operand
        boost::apply_visitor(*this, expr.operand);
    }

private:
    const Query& query_;
    StartPositionsDraft& dest_;

    void handle_concatenation_or_and(const std::vector<Expression>& operands) const {
        for (const auto& operand : operands) {
            if (is_negative_assertion(operand)) {
                continue;
            }
            boost::apply_visitor(*this, operand);
            return;
        }

        boost::apply_visitor(*this, operands[0]);
    }
};

StartPositions StartPositionsDraft::finalize() const {
    return StartPositions{positions, deps};
}

StartPositions StartPositionFinder::find_start_positions(const Query& query) const {
    StartPositionsDraft draft;
    FindStartPositionsVisitor visitor(query, draft);
    boost::apply_visitor(visitor, query.expression);
    return draft.finalize();
}

}  // namespace saengra
