#include "expression.h"
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>
#include <sstream>

namespace saengra {

namespace {
    struct ToStringVisitor : boost::static_visitor<std::string> {
        template<typename T>
        std::string operator()(const T& expr) const {
            return expr.to_string();
        }
    };

    struct IterChildrenVisitor : boost::static_visitor<Children> {
        template<typename T>
        Children operator()(T& expr) const {
            return expr.iter_children();
        }
    };

    struct ConstIterChildrenVisitor : boost::static_visitor<ConstChildren> {
        template<typename T>
        ConstChildren operator()(const T& expr) const {
            return expr.iter_children();
        }
    };
}

std::string Expression::to_string() const {
    return boost::apply_visitor(ToStringVisitor(), *this);
}

Children Expression::iter_children() {
    return boost::apply_visitor(IterChildrenVisitor(), *this);
}

ConstChildren Expression::iter_children() const {
    return boost::apply_visitor(ConstIterChildrenVisitor(), *this);
}

std::string VertexExpr::to_string() const {
    std::ostringstream oss;
    if (placeholder_idx.has_value()) {
        oss << "?";
    } else if (type_name.has_value()) {
        oss << *type_name.value().text;
    } else {
        oss << "*";
    }

    if (set_ref.has_value()) {
        oss << " as " << set_ref.value();
    }
    return oss.str();
}

Children VertexExpr::iter_children() {
    return {};
}

ConstChildren VertexExpr::iter_children() const {
    return {};
}

std::string EdgeExpr::to_string() const {
    std::ostringstream oss;

    // Start based on direction
    switch (direction) {
        case Direction::Backward:
            oss << "<-";
            break;
        case Direction::Forward:
            oss << "-";
            break;
        case Direction::Both:
            oss << "<";
            break;
        case Direction::Any:
            oss << "--";
            break;
    }

    // Labels
    if (!labels.empty()) {
        for (size_t i = 0; i < labels.size(); ++i) {
            if (i > 0) oss << "|";
            oss << labels[i];
        }
    } else {
        oss << "*";
    }

    // End based on direction
    switch (direction) {
        case Direction::Backward:
            oss << "-";
            break;
        case Direction::Forward:
            oss << "->";
            break;
        case Direction::Both:
            oss << ">";
            break;
        case Direction::Any:
            oss << "--";
            break;
    }

    return oss.str();
}

Children EdgeExpr::iter_children() {
    return {};
}

ConstChildren EdgeExpr::iter_children() const {
    return {};
}

std::string NoopExpr::to_string() const {
    return ".";
}

Children NoopExpr::iter_children() {
    return {};
}

ConstChildren NoopExpr::iter_children() const {
    return {};
}

ConcatenationExpr::ConcatenationExpr(Expressions ops) : operands(std::move(ops)) {}

std::string ConcatenationExpr::to_string() const {
    std::ostringstream oss;
    for (const auto& op : operands) {
        oss << op.to_string() << " ";
    }
    return oss.str();
}

Children ConcatenationExpr::iter_children() {
    return operands;
}

ConstChildren ConcatenationExpr::iter_children() const {
    return operands;
}

OrExpr::OrExpr(Expressions ops) : operands(std::move(ops)) {}

std::string OrExpr::to_string() const {
    std::ostringstream oss;
    for (size_t i = 0; i < operands.size(); ++i) {
        if (i > 0) oss << " | ";
        oss << operands[i].to_string();
    }
    return oss.str();
}

Children OrExpr::iter_children() {
    return operands;
}

ConstChildren OrExpr::iter_children() const {
    return operands;
}

AndExpr::AndExpr(Expressions ops) : operands(std::move(ops)) {}

std::string AndExpr::to_string() const {
    std::ostringstream oss;
    for (size_t i = 0; i < operands.size(); ++i) {
        if (i > 0) oss << " & ";
        oss << operands[i].to_string();
    }
    return oss.str();
}

Children AndExpr::iter_children() {
    return operands;
}

ConstChildren AndExpr::iter_children() const {
    return operands;
}

OperationExpr::OperationExpr(OperationType type, Expression op)
        : op_type(type), operand(std::move(op)) {}

std::string OperationExpr::to_string() const {
    std::ostringstream oss;
    oss << "(";
    switch (op_type) {
        case OperationType::All: oss << "all:"; break;
        case OperationType::If: oss << "if:"; break;
        case OperationType::Unless: oss << "unless:"; break;
        case OperationType::Skip: oss << "skip:"; break;
        case OperationType::Maybe: oss << "maybe:"; break;
    }
    oss << " " << operand.to_string() << ")";
    return oss.str();
}

Children OperationExpr::iter_children() {
    return Children(&operand, 1);
}

ConstChildren OperationExpr::iter_children() const {
    return ConstChildren(&operand, 1);
}

RepetitionExpr::RepetitionExpr(int min, int max, Expression op)
        : min_repeats(min), max_repeats(max), operand(std::move(op)) {}

std::string RepetitionExpr::to_string() const {
    std::ostringstream oss;
    oss << "(repeat " << min_repeats << "-" << max_repeats << "x: "
        << operand.to_string() << ")";
    return oss.str();
}

Children RepetitionExpr::iter_children() {
    return Children(&operand, 1);
}

ConstChildren RepetitionExpr::iter_children() const {
    return ConstChildren(&operand, 1);
}

std::ostream& operator<<(std::ostream& os, const VertexExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const EdgeExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const NoopExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const ConcatenationExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const OrExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const AndExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const OperationExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

std::ostream& operator<<(std::ostream& os, const RepetitionExpr& obj) {
    os << "Expression(" << obj.to_string() << ")";
    return os;
}

} // namespace saengra
