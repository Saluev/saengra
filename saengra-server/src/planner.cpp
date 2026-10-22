#include "planner.h"
#include <algorithm>
#include <iterator>
#include <set>
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

namespace saengra {

namespace {

using RefNames = std::set<RefName>;

RefNames intersected(const RefNames& a, const RefNames& b) {
    RefNames result;
    std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(result, result.end()));
    return result;
}

struct PlanContext {
    bool nest_operands;
    size_t next_nested_id = 0;
    // Refs that operations matched so far may set or exclude values of. They're specific to every
    // candidate the operation matched, so nested expressions keyed by them are hardly ever reused.
    RefNames set_by_operations = {};
};

Plan plan_expression(const Expression& expr, RefNames& bound, RefNames& changed, PlanContext& context);

// Adds the refs that the expression mentions.
void collect_refs(const Expression& expr, RefNames& refs) {
    if (const auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->ref.has_value()) {
            refs.insert(vertex_expr->ref.value());
        }
    }
    for (const Expression& child : expr.iter_children()) {
        collect_refs(child, refs);
    }
}

// Plans an expression, given the refs `bound` to a vertex in every subgraph matched before it.
// Updates `bound` with the refs bound after it, and adds to `changed` the refs whose values it may
// set or exclude, so that the refs of the matched subgraphs may differ from the refs at the start.
class PlanVisitor : public boost::static_visitor<Plan> {
public:
    PlanVisitor(RefNames& bound, RefNames& changed, PlanContext& context)
        : bound_(bound), changed_(changed), context_(context) {}

    Plan operator()(const VertexExpr& expr) const {
        return Plan{plan_vertex(expr)};
    }

    Plan operator()(const EdgeExpr& expr) const {
        return Plan{plan_edge(expr)};
    }

    Plan operator()(const NoopExpr& expr) const {
        return Plan{MatchNoop{}};
    }

    Plan operator()(const ConcatenationExpr& expr) const {
        std::vector<const Expression*> operands;
        flatten(expr, operands);

        // Runs of vertices and edges are collected to be matched as paths.
        Plans plans;
        std::vector<std::variant<MatchVertex, MatchEdge>> path;
        for (const Expression* operand : operands) {
            if (const auto* vertex_expr = boost::get<VertexExpr>(operand)) {
                path.emplace_back(plan_vertex(*vertex_expr));
            } else if (const auto* edge_expr = boost::get<EdgeExpr>(operand)) {
                path.emplace_back(plan_edge(*edge_expr));
            } else {
                add_paths(std::move(path), plans);
                path.clear();
                plans.push_back(plan_expression(*operand, bound_, changed_, context_));
            }
        }
        add_paths(std::move(path), plans);

        if (plans.size() == 1) {
            return std::move(plans.front());
        }
        return Plan{MatchConcatenation{std::move(plans)}};
    }

    Plan operator()(const OrExpr& expr) const {
        // A ref is bound after the alternatives if it's bound after each of them.
        std::optional<RefNames> bound;
        Plans plans;
        for (const Expression& operand : expr.operands) {
            RefNames operand_bound = bound_;
            plans.push_back(plan_expression(operand, operand_bound, changed_, context_));
            bound = bound.has_value() ? intersected(*bound, operand_bound) : std::move(operand_bound);
        }
        if (bound.has_value()) {
            bound_ = std::move(*bound);
        }
        return Plan{MatchOr{std::move(plans)}};
    }

    Plan operator()(const AndExpr& expr) const {
        // Operands are matched from the same start, and the refs of each of them are bound after them.
        RefNames bound = bound_;
        RefNames changed;
        Plans plans;
        for (const Expression& operand : expr.operands) {
            RefNames operand_bound = bound_;
            plans.push_back(nested(operand, plan_expression(operand, operand_bound, changed, context_)));
            bound.insert(operand_bound.begin(), operand_bound.end());
        }
        bound_ = std::move(bound);
        context_.set_by_operations.insert(changed.begin(), changed.end());
        if (changed.empty()) {
            return Plan{MatchAndWithoutSetRef{std::move(plans)}};
        }
        changed_.insert(changed.begin(), changed.end());
        return Plan{MatchAnd{std::move(plans)}};
    }

    Plan operator()(const OperationExpr& expr) const {
        RefNames changed;
        switch (expr.op_type) {
            case OperationType::All: {
                // Every resulting subgraph is merged from operand's subgraphs, and has their refs bound.
                // The operands of & are nested expressions themselves, so that (all: ... & ...) stays fused.
                Plan operand = plan_expression(expr.operand, bound_, changed, context_);
                if (boost::get<AndExpr>(&expr.operand) == nullptr) {
                    operand = nested(expr.operand, std::move(operand));
                }
                context_.set_by_operations.insert(changed.begin(), changed.end());
                if (changed.empty()) {
                    return Plan{MatchAllWithoutSetRef{std::move(operand)}};
                }
                changed_.insert(changed.begin(), changed.end());
                if (auto* and_plan = boost::get<MatchAnd>(&operand)) {
                    return Plan{MatchAllAnd{std::move(and_plan->operands)}};
                }
                return Plan{MatchAll{std::move(operand)}};
            }
            case OperationType::If: {
                Plan operand = plan_expression(expr.operand, bound_, changed, context_);
                context_.set_by_operations.insert(changed.begin(), changed.end());
                if (changed.empty()) {
                    return Plan{MatchIfWithoutSetRef{std::move(operand)}};
                }
                changed_.insert(changed.begin(), changed.end());
                return Plan{MatchIf{std::move(operand)}};
            }
            case OperationType::Unless: {
                // The negation binds no refs, it only excludes values of shared ones. Local refs
                // don't change anything outside of it.
                RefNames operand_bound = bound_;
                Plan operand = plan_expression(expr.operand, operand_bound, changed, context_);
                RefNames shared_changed;
                for (const RefName& ref : expr.shared_refs) {
                    if (changed.contains(ref)) {
                        shared_changed.insert(ref);
                    }
                }
                context_.set_by_operations.insert(shared_changed.begin(), shared_changed.end());
                if (shared_changed.empty()) {
                    return Plan{MatchUnlessWithoutSetNonLocalRef{std::move(operand), expr.shared_refs}};
                }
                changed_.insert(shared_changed.begin(), shared_changed.end());
                return Plan{MatchUnless{std::move(operand), expr.shared_refs}};
            }
            case OperationType::Skip: {
                Plan operand = plan_expression(expr.operand, bound_, changed, context_);
                context_.set_by_operations.insert(changed.begin(), changed.end());
                changed_.insert(changed.begin(), changed.end());
                return Plan{MatchSkip{std::move(operand)}};
            }
            case OperationType::Maybe: {
                // If the operand doesn't match, nothing gets bound.
                RefNames operand_bound = bound_;
                Plan operand = nested(expr.operand, plan_expression(expr.operand, operand_bound, changed, context_));
                context_.set_by_operations.insert(changed.begin(), changed.end());
                changed_.insert(changed.begin(), changed.end());
                return Plan{MatchMaybe{std::move(operand)}};
            }
        }
        throw std::runtime_error("unknown operation type");
    }

    Plan operator()(const RepetitionExpr& expr) const {
        RefNames operand_bound = bound_;
        Plan operand = plan_expression(expr.operand, operand_bound, changed_, context_);
        if (expr.min_repeats > 0) {
            bound_ = std::move(operand_bound);
        }
        return Plan{MatchRepetition{expr.min_repeats, expr.max_repeats, std::move(operand)}};
    }

private:
    RefNames& bound_;
    RefNames& changed_;
    PlanContext& context_;

    // Operands of (all: ), (maybe: ) and &, whose subgraphs are joined, are nested expressions:
    // their subgraphs are cached and shared by all observers. Except single steps, which are as cheap
    // to match as to look up, and operands mentioning refs set by earlier operations, which are
    // specific to every candidate the operations matched, so their subgraphs are hardly ever reused.
    Plan nested(const Expression& expr, Plan plan) const {
        if (!context_.nest_operands || is_single_step(plan)) {
            return plan;
        }
        RefNames refs;
        collect_refs(expr, refs);
        if (!intersected(refs, context_.set_by_operations).empty()) {
            return plan;
        }
        return Plan{MatchNestedExpression{context_.next_nested_id++, {refs.begin(), refs.end()}, std::move(plan)}};
    }

    static bool is_single_step(const Plan& plan) {
        if (boost::get<MatchVertex>(&plan) || boost::get<MatchEdge>(&plan) || boost::get<MatchNoop>(&plan)) {
            return true;
        }
        if (const auto* path_to_vertex = boost::get<MatchPathToVertex>(&plan)) {
            return path_to_vertex->path.vertices.empty() && path_to_vertex->path.edges.empty();
        }
        return false;
    }

    MatchVertex plan_vertex(const VertexExpr& expr) const {
        MatchVertex vertex;
        vertex.type_name = expr.type_name;
        vertex.placeholder_idx = expr.placeholder_idx;
        if (expr.ref.has_value()) {
            const RefName& ref = expr.ref.value();
            if (bound_.contains(ref)) {
                vertex.match_ref = ref;
            } else {
                vertex.set_ref = ref;
                bound_.insert(ref);
                changed_.insert(ref);
            }
        }
        return vertex;
    }

    static MatchEdge plan_edge(const EdgeExpr& expr) {
        return MatchEdge{expr.labels, expr.direction};
    }

    static void flatten(const ConcatenationExpr& expr, std::vector<const Expression*>& operands) {
        for (const Expression& operand : expr.operands) {
            if (const auto* concatenation = boost::get<ConcatenationExpr>(&operand)) {
                flatten(*concatenation, operands);
            } else {
                operands.push_back(&operand);
            }
        }
    }

    static bool is_known(const MatchVertex& vertex) {
        return vertex.placeholder_idx.has_value() || vertex.match_ref.has_value();
    }

    // Splits a sequence of vertices and edges into alternating paths, which may end with an edge to
    // a known vertex.
    static void add_paths(std::vector<std::variant<MatchVertex, MatchEdge>> items, Plans& plans) {
        std::vector<std::variant<MatchVertex, MatchEdge>> path;
        for (size_t i = 0; i < items.size(); ++i) {
            auto* edge = std::get_if<MatchEdge>(&items[i]);
            auto* next_vertex = i + 1 < items.size() ? std::get_if<MatchVertex>(&items[i + 1]) : nullptr;
            if (edge != nullptr && next_vertex != nullptr && is_known(*next_vertex)) {
                MatchPath prefix{PositionKind::ORBIT, {}, {}};
                if (!path.empty() && std::holds_alternative<MatchVertex>(path.back()) && path.size() + 2 <= MatchPath::max_length) {
                    prefix = make_path(std::move(path));
                } else {
                    add_path(std::move(path), plans);
                }
                path.clear();
                plans.push_back(Plan{MatchPathToVertex{std::move(prefix), std::move(*edge), std::move(*next_vertex)}});
                ++i;
                continue;
            }
            if (!path.empty() && (path.back().index() == items[i].index() || path.size() == MatchPath::max_length)) {
                // Two vertices or two edges in a row don't make a path, and paths have limited length.
                add_path(std::move(path), plans);
                path.clear();
            }
            path.push_back(std::move(items[i]));
        }
        add_path(std::move(path), plans);
    }

    static void add_path(std::vector<std::variant<MatchVertex, MatchEdge>> items, Plans& plans) {
        if (items.empty()) {
            return;
        }
        if (items.size() == 1) {
            std::visit([&plans](auto& item) { plans.push_back(Plan{std::move(item)}); }, items.front());
            return;
        }
        plans.push_back(Plan{make_path(std::move(items))});
    }

    static MatchPath make_path(std::vector<std::variant<MatchVertex, MatchEdge>> items) {
        MatchPath path;
        path.start = std::holds_alternative<MatchVertex>(items.front()) ? PositionKind::CORE : PositionKind::ORBIT;
        for (auto& item : items) {
            if (auto* vertex = std::get_if<MatchVertex>(&item)) {
                path.vertices.push_back(std::move(*vertex));
            } else {
                path.edges.push_back(std::move(std::get<MatchEdge>(item)));
            }
        }
        return path;
    }
};

Plan plan_expression(const Expression& expr, RefNames& bound, RefNames& changed, PlanContext& context) {
    return boost::apply_visitor(PlanVisitor(bound, changed, context), expr);
}

} // namespace

Plan Planner::plan(const Expression& expr, bool nest_operands) const {
    RefNames bound;
    RefNames changed;
    PlanContext context{nest_operands};
    return plan_expression(expr, bound, changed, context);
}

} // namespace saengra
