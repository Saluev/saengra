#pragma once

#include "expression.h"
#include "vertices_container.h"
#include "edges_container.h"
#include "graph.h"
#include <string>
#include <memory>

namespace saengra {

class Parser {
public:
    Parser(Graph& graph) : graph_(graph) {}
    Expression parse(const std::string& input);
private:
    Graph& graph_;

    void update_placeholder_indices(Expression& expr, int& counter) const;
    void update_set_refs(Expression& expr, std::vector<std::string>& refs) const;
};

} // namespace saengra
