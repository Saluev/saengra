#pragma once

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

}
