#pragma once

#include <stdexcept>
#include <string>
#include "expression.h"
#include "graph.h"
#include "vertex.h"

namespace saengra {

using PlaceholderValues = std::vector<VertexData>;

struct Query {
    const Graph& graph;
    Expression expression;
    const PlaceholderValues placeholder_values;
};

inline size_t count_placeholders(const Expression& expr) {
    size_t count = 0;
    if (const auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->placeholder_idx.has_value()) {
            ++count;
        }
    }
    for (const auto& child : expr.iter_children()) {
        count += count_placeholders(child);
    }
    return count;
}

inline Query make_query(const Graph& graph, Expression expression, PlaceholderValues placeholder_values) {
    const size_t expected = count_placeholders(expression);
    if (placeholder_values.size() != expected) {
        throw std::invalid_argument(
            "expression '" + expression.to_string() + "' has " + std::to_string(expected) +
            " placeholder(s), but " + std::to_string(placeholder_values.size()) +
            " placeholder value(s) were provided"
        );
    }
    return Query{graph, std::move(expression), std::move(placeholder_values)};
}

}
