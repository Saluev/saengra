#include "plan.h"
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>
#include <sstream>

namespace saengra {

namespace {
    struct ToStringVisitor : boost::static_visitor<std::string> {
        template<typename T>
        std::string operator()(const T& plan) const {
            return plan.to_string();
        }
    };

    struct IterNestedPlansVisitor : boost::static_visitor<NestedPlans> {
        template<typename T>
        NestedPlans operator()(T& plan) const {
            return plan.iter_nested_plans();
        }
    };

    struct ConstIterNestedPlansVisitor : boost::static_visitor<ConstNestedPlans> {
        template<typename T>
        ConstNestedPlans operator()(const T& plan) const {
            return plan.iter_nested_plans();
        }
    };

    std::string join(const char* name, ConstNestedPlans plans) {
        std::ostringstream oss;
        oss << name << "(";
        for (size_t i = 0; i < plans.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << plans[i].to_string();
        }
        oss << ")";
        return oss.str();
    }

    std::string vertex_to_string(const MatchVertex& vertex) {
        std::ostringstream oss;
        if (vertex.placeholder_idx.has_value()) {
            oss << "?";
        } else if (vertex.type_name.has_value()) {
            oss << *vertex.type_name.value().text;
        } else {
            oss << "*";
        }
        if (vertex.set_ref.has_value()) {
            oss << " as " << vertex.set_ref.value();
        }
        if (vertex.match_ref.has_value()) {
            oss << " == " << vertex.match_ref.value();
        }
        return oss.str();
    }

    std::string edge_to_string(const MatchEdge& edge) {
        return EdgeExpr{edge.labels, edge.direction}.to_string();
    }

    std::string path_to_string(const MatchPath& path) {
        std::ostringstream oss;
        const bool from_core = path.start == PositionKind::CORE;
        const size_t length = path.vertices.size() + path.edges.size();
        for (size_t i = 0; i < length; ++i) {
            if (i > 0) oss << " ";
            oss << ((i % 2 == 0) == from_core ? vertex_to_string(path.vertices[i / 2]) : edge_to_string(path.edges[i / 2]));
        }
        return oss.str();
    }
}

std::string Plan::to_string() const {
    return boost::apply_visitor(ToStringVisitor(), *this);
}

NestedPlans Plan::iter_nested_plans() {
    return boost::apply_visitor(IterNestedPlansVisitor(), *this);
}

ConstNestedPlans Plan::iter_nested_plans() const {
    return boost::apply_visitor(ConstIterNestedPlansVisitor(), *this);
}

std::string MatchVertex::to_string() const {
    return "Vertex(" + vertex_to_string(*this) + ")";
}

NestedPlans MatchVertex::iter_nested_plans() {
    return {};
}

ConstNestedPlans MatchVertex::iter_nested_plans() const {
    return {};
}

std::string MatchEdge::to_string() const {
    return "Edge(" + edge_to_string(*this) + ")";
}

NestedPlans MatchEdge::iter_nested_plans() {
    return {};
}

ConstNestedPlans MatchEdge::iter_nested_plans() const {
    return {};
}

std::string MatchNoop::to_string() const {
    return "Noop()";
}

NestedPlans MatchNoop::iter_nested_plans() {
    return {};
}

ConstNestedPlans MatchNoop::iter_nested_plans() const {
    return {};
}

std::string MatchPath::to_string() const {
    return "Path(" + path_to_string(*this) + ")";
}

NestedPlans MatchPath::iter_nested_plans() {
    return {};
}

ConstNestedPlans MatchPath::iter_nested_plans() const {
    return {};
}

std::string MatchPathToVertex::to_string() const {
    const auto prefix = path_to_string(path);
    return "PathToVertex(" + (prefix.empty() ? "" : prefix + " ") + edge_to_string(edge) + " " + vertex_to_string(vertex) + ")";
}

NestedPlans MatchPathToVertex::iter_nested_plans() {
    return {};
}

ConstNestedPlans MatchPathToVertex::iter_nested_plans() const {
    return {};
}

MatchConcatenation::MatchConcatenation(Plans ops) : operands(std::move(ops)) {}

std::string MatchConcatenation::to_string() const {
    return join("Concatenation", operands);
}

NestedPlans MatchConcatenation::iter_nested_plans() {
    return operands;
}

ConstNestedPlans MatchConcatenation::iter_nested_plans() const {
    return operands;
}

MatchOr::MatchOr(Plans ops) : operands(std::move(ops)) {}

std::string MatchOr::to_string() const {
    return join("Or", operands);
}

NestedPlans MatchOr::iter_nested_plans() {
    return operands;
}

ConstNestedPlans MatchOr::iter_nested_plans() const {
    return operands;
}

MatchAndWithoutSetRef::MatchAndWithoutSetRef(Plans ops) : operands(std::move(ops)) {}

std::string MatchAndWithoutSetRef::to_string() const {
    return join("AndWithoutSetRef", operands);
}

NestedPlans MatchAndWithoutSetRef::iter_nested_plans() {
    return operands;
}

ConstNestedPlans MatchAndWithoutSetRef::iter_nested_plans() const {
    return operands;
}

MatchAnd::MatchAnd(Plans ops) : operands(std::move(ops)) {}

std::string MatchAnd::to_string() const {
    return join("And", operands);
}

NestedPlans MatchAnd::iter_nested_plans() {
    return operands;
}

ConstNestedPlans MatchAnd::iter_nested_plans() const {
    return operands;
}

MatchAllAnd::MatchAllAnd(Plans ops) : operands(std::move(ops)) {}

std::string MatchAllAnd::to_string() const {
    return join("AllAnd", operands);
}

NestedPlans MatchAllAnd::iter_nested_plans() {
    return operands;
}

ConstNestedPlans MatchAllAnd::iter_nested_plans() const {
    return operands;
}

std::string MatchAllWithoutSetRef::to_string() const {
    return join("AllWithoutSetRef", iter_nested_plans());
}

NestedPlans MatchAllWithoutSetRef::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchAllWithoutSetRef::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchAll::to_string() const {
    return join("All", iter_nested_plans());
}

NestedPlans MatchAll::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchAll::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchIfWithoutSetRef::to_string() const {
    return join("IfWithoutSetRef", iter_nested_plans());
}

NestedPlans MatchIfWithoutSetRef::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchIfWithoutSetRef::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchIf::to_string() const {
    return join("If", iter_nested_plans());
}

NestedPlans MatchIf::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchIf::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchUnlessWithoutSetNonLocalRef::to_string() const {
    return join("UnlessWithoutSetNonLocalRef", iter_nested_plans());
}

NestedPlans MatchUnlessWithoutSetNonLocalRef::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchUnlessWithoutSetNonLocalRef::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchUnless::to_string() const {
    return join("Unless", iter_nested_plans());
}

NestedPlans MatchUnless::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchUnless::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchSkip::to_string() const {
    return join("Skip", iter_nested_plans());
}

NestedPlans MatchSkip::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchSkip::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchMaybe::to_string() const {
    return join("Maybe", iter_nested_plans());
}

NestedPlans MatchMaybe::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchMaybe::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::string MatchNestedExpression::to_string() const {
    return "Nested#" + std::to_string(id) + "(" + plan.to_string() + ")";
}

NestedPlans MatchNestedExpression::iter_nested_plans() {
    return NestedPlans(&plan, 1);
}

ConstNestedPlans MatchNestedExpression::iter_nested_plans() const {
    return ConstNestedPlans(&plan, 1);
}

MatchRepetition::MatchRepetition(int min, int max, Plan op)
        : operand(std::move(op)), min_repeats(min), max_repeats(max) {}

std::string MatchRepetition::to_string() const {
    std::ostringstream oss;
    oss << "Repetition" << min_repeats << "-" << max_repeats << "(" << operand.to_string() << ")";
    return oss.str();
}

NestedPlans MatchRepetition::iter_nested_plans() {
    return NestedPlans(&operand, 1);
}

ConstNestedPlans MatchRepetition::iter_nested_plans() const {
    return ConstNestedPlans(&operand, 1);
}

std::ostream& operator<<(std::ostream& os, const Plan& obj) {
    os << "Plan(" << obj.to_string() << ")";
    return os;
}

} // namespace saengra
