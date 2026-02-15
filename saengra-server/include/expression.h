#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <boost/variant.hpp>
#include "edge.h"
#include "refs.h"

namespace saengra {

// Forward declarations
class Expression;
using ExprPtr = std::unique_ptr<Expression>;
using Expressions = std::vector<Expression>;
using Children = std::span<Expression>;
using ConstChildren = std::span<const Expression>;

// Forward declaration of Expression for circular reference
class VertexExpr;
class EdgeExpr;
class NoopExpr;
class ConcatenationExpr;
class OrExpr;
class AndExpr;
class OperationExpr;
class RepetitionExpr;

class Expression: public boost::variant<
    boost::recursive_wrapper<VertexExpr>,
    boost::recursive_wrapper<EdgeExpr>,
    boost::recursive_wrapper<NoopExpr>,
    boost::recursive_wrapper<ConcatenationExpr>,
    boost::recursive_wrapper<OrExpr>,
    boost::recursive_wrapper<AndExpr>,
    boost::recursive_wrapper<OperationExpr>,
    boost::recursive_wrapper<RepetitionExpr>
> {
public:
    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

using Expressions = Expressions;

// Vertex expression
class VertexExpr {
public:
    std::optional<VertexTypeName> type_name;
    std::optional<RefName> set_ref;
    std::optional<RefName> match_ref;
    std::optional<int> placeholder_idx;

    VertexExpr() = default;
    VertexExpr(VertexTypeName type_name) : type_name(type_name) {}

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

enum class Direction {
    Forward,    // ->
    Backward,   // <-
    Both,       // <> (bidirectional)
    Any         // -- (any direction)
};

// Edge expression
class EdgeExpr {
public:
    std::vector<EdgeLabel> labels;
    Direction direction;

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

// Noop expression (matches empty graph at current position)
class NoopExpr {
public:
    NoopExpr() = default;

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

// Concatenation (sequence)
class ConcatenationExpr {
public:
    Expressions operands;

    ConcatenationExpr() = default;
    explicit ConcatenationExpr(Expressions ops);

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

// Or (|)
class OrExpr {
public:
    Expressions operands;

    OrExpr() = default;
    explicit OrExpr(Expressions ops);

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

// And (&)
class AndExpr {
public:
    Expressions operands;

    AndExpr() = default;
    explicit AndExpr(Expressions ops);

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

// Operation types
enum class OperationType {
    All,        // all:
    If,         // if: or ?=
    Unless,     // unless: or ?!
    Skip,       // skip: or ?:
    Maybe       // maybe:
};

// Operation expression
class OperationExpr {
public:
    Expression operand;
    OperationType op_type;

    OperationExpr(OperationType type, Expression op);

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};

// Repetition expression
class RepetitionExpr {
public:
    Expression operand;
    int min_repeats;
    int max_repeats;

    RepetitionExpr(int min, int max, Expression op);

    std::string to_string() const;
    Children iter_children();
    ConstChildren iter_children() const;
};


std::ostream& operator<<(std::ostream& os, const VertexExpr& obj);
std::ostream& operator<<(std::ostream& os, const EdgeExpr& obj);
std::ostream& operator<<(std::ostream& os, const NoopExpr& obj);
std::ostream& operator<<(std::ostream& os, const ConcatenationExpr& obj);
std::ostream& operator<<(std::ostream& os, const OrExpr& obj);
std::ostream& operator<<(std::ostream& os, const AndExpr& obj);
std::ostream& operator<<(std::ostream& os, const OperationExpr& obj);
std::ostream& operator<<(std::ostream& os, const RepetitionExpr& obj);

} // namespace saengra
