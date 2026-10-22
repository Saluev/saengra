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
#include "expression.h"
#include "subgraph.h"

namespace saengra {

// Forward declarations
class Plan;
using PlanPtr = std::unique_ptr<Plan>;
using Plans = std::vector<Plan>;
using NestedPlans = std::span<Plan>;
using ConstNestedPlans = std::span<const Plan>;

// Forward declaration of Plan for circular reference
class MatchVertex;
class MatchEdge;
class MatchNoop;
class MatchEdgeToVertex;
class MatchPath;
class MatchConcatenation;
class MatchOr;
class MatchAndWithoutSetRef;
class MatchAnd;
class MatchAllAnd;
class MatchAllWithoutSetRef;
class MatchAll;
class MatchIfWithoutSetRef;
class MatchIf;
class MatchUnlessWithoutSetNonLocalRef;
class MatchUnless;
class MatchSkip;
class MatchMaybe;
class MatchRepetition;

class Plan: public boost::variant<
    boost::recursive_wrapper<MatchVertex>,
    boost::recursive_wrapper<MatchEdge>,
    boost::recursive_wrapper<MatchNoop>,
    boost::recursive_wrapper<MatchEdgeToVertex>,
    boost::recursive_wrapper<MatchPath>,
    boost::recursive_wrapper<MatchConcatenation>,
    boost::recursive_wrapper<MatchOr>,
    boost::recursive_wrapper<MatchAndWithoutSetRef>,
    boost::recursive_wrapper<MatchAnd>,
    boost::recursive_wrapper<MatchAllAnd>,
    boost::recursive_wrapper<MatchAllWithoutSetRef>,
    boost::recursive_wrapper<MatchAll>,
    boost::recursive_wrapper<MatchIfWithoutSetRef>,
    boost::recursive_wrapper<MatchIf>,
    boost::recursive_wrapper<MatchUnlessWithoutSetNonLocalRef>,
    boost::recursive_wrapper<MatchUnless>,
    boost::recursive_wrapper<MatchSkip>,
    boost::recursive_wrapper<MatchMaybe>,
    boost::recursive_wrapper<MatchRepetition>
> {
public:
    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

using Plans = Plans;

class MatchVertex {
public:
    std::optional<VertexTypeName> type_name;

    std::optional<RefName> set_ref;
    // `set_ref` is specified if there's a possibility that this vertex expression
    // will assign a value to a ref. Examples: it's the first `as x` expression in the tree;
    // or it is not first, but the previous ones are in parallel branches of & or |; or
    // not in parallel branches, but in optional branches somewhere in the beginning of expression.

    std::optional<RefName> match_ref;
    // `match_ref` is specified otherwise — when the expression guarantees we only have
    // to match the vertex against already assigned ref value.

    std::optional<int> placeholder_idx;

    MatchVertex() = default;
    MatchVertex(VertexTypeName type_name) : type_name(type_name) {}

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchEdge {
public:
    std::vector<EdgeLabel> labels;
    Direction direction;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchNoop {
public:
    MatchNoop() = default;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// -edge> ? or -edge> match_ref — optimized by requesting particular edge and depending only on it
class MatchEdgeToVertex {
public:
    MatchEdge edge;
    MatchVertex vertex;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// vertex -edge> vertex <edge- ... — optimized by implementing simple nested loops (up to X levels deep)
class MatchPath {
public:
    static constexpr size_t max_length = 5;  // vertices and edges

    PositionKind start;  // orbit means we start with edges[0], core — with vertices[0]
    std::vector<MatchEdge> edges;
    std::vector<MatchVertex> vertices;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchConcatenation {
public:
    Plans operands;

    MatchConcatenation() = default;
    explicit MatchConcatenation(Plans ops);

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchOr {
public:
    Plans operands;

    MatchOr() = default;
    explicit MatchOr(Plans ops);

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// (X & Y & ...) without setting new refs — optimized by skipping refs analysis for the resulting subgraphs
class MatchAndWithoutSetRef {
public:
    Plans operands;

    MatchAndWithoutSetRef() = default;
    explicit MatchAndWithoutSetRef(Plans ops);

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchAnd {
public:
    Plans operands;

    MatchAnd() = default;
    explicit MatchAnd(Plans ops);

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// (all: X & Y & ...) — optimized by avoiding double join
class MatchAllAnd {
public:
    Plans operands;

    MatchAllAnd() = default;
    explicit MatchAllAnd(Plans ops);

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// (all: expr without setting new refs) — optimized by merging all subgraphs without refs analysis
class MatchAllWithoutSetRef {
public:
    Plan operand;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchAll {
public:
    Plan operand;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// (?= expr without setting new refs) — optimized by stopping after first found subgraph
class MatchIfWithoutSetRef {
public:
    Plan operand;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchIf {
public:
    Plan operand;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

// (?! expr without setting new non-local refs) — optimized by stopping after first found subgraph
class MatchUnlessWithoutSetNonLocalRef {
public:
    Plan operand;
    std::vector<RefName> shared_refs;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchUnless {
public:
    Plan operand;
    std::vector<RefName> shared_refs;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchSkip {
public:
    Plan operand;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchMaybe {
public:
    Plan operand;

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};

class MatchRepetition {
public:
    Plan operand;
    int min_repeats;
    int max_repeats;

    MatchRepetition(int min, int max, Plan op);

    std::string to_string() const;
    NestedPlans iter_nested_plans();
    ConstNestedPlans iter_nested_plans() const;
};


std::ostream& operator<<(std::ostream& os, const Plan& obj);

} // namespace saengra
