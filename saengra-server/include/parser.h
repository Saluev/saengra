#pragma once

#include "expression.h"
#include "vertices_container.h"
#include "edges_container.h"
#include "graph.h"
#include <string>
#include <memory>
#include <set>

namespace saengra {

class Parser {
public:
    Parser(Graph& graph) : graph_(graph) {}
    Expression parse(const std::string& input);
private:
    Graph& graph_;

    void update_placeholder_indices(Expression& expr, int& counter) const;
    void collect_ref_names(const Expression& expr, std::set<std::string>& names) const;
    void resolve_refs(Expression& expr, const std::set<std::string>& names) const;
};

} // namespace saengra
