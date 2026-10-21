#include "parser.h"
#include <sstream>
#include <cstring>

// Forward declarations from generated parser
extern saengra::Expression* parse_result_raw;
extern saengra::Graph* parse_graph;
extern int yyparse();

// Flex/Bison interface
typedef struct yy_buffer_state * YY_BUFFER_STATE;
extern YY_BUFFER_STATE yy_scan_string(const char * str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);

namespace saengra {

Expression Parser::parse(const std::string& input) {
    parse_result_raw = nullptr;
    parse_graph = &graph_;

    try {
        YY_BUFFER_STATE buffer = yy_scan_string(input.c_str());
        int result = yyparse();

        yy_delete_buffer(buffer);
        parse_graph = nullptr;

        if (result != 0) {
            throw std::runtime_error("parse error");
        }

        int counter = 0;
        update_placeholder_indices(*parse_result_raw, counter);

        // Names are resolved for the whole expression at once, so that a reference may precede
        // its `as` (e.g., when the ref is bound in a parallel or optional branch, or in a lookahead).
        std::set<std::string> ref_names;
        collect_ref_names(*parse_result_raw, ref_names);
        resolve_refs(*parse_result_raw, ref_names);
        const auto ref_counts = resolve_shared_refs(*parse_result_raw, {});
        resolve_shared_refs(*parse_result_raw, ref_counts);

        // Wrap raw pointer in unique_ptr
        return *parse_result_raw;
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(std::string("parse error: ") + e.what() + " in '" + input + "'");
    }
}

void Parser::update_placeholder_indices(Expression& expr, int& counter) const {
    if (auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->placeholder_idx.has_value()) {
            vertex_expr->placeholder_idx = counter++;
        }
    }
    for (auto& child : expr.iter_children()) {
        update_placeholder_indices(child, counter);
    }
}

void Parser::collect_ref_names(const Expression& expr, std::set<std::string>& names) const {
    if (auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->ref.has_value()) {
            names.insert(vertex_expr->ref.value());
        }
    }
    for (const auto& child : expr.iter_children()) {
        collect_ref_names(child, names);
    }
}

void Parser::resolve_refs(Expression& expr, const std::set<std::string>& names) const {
    if (auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->type_name.has_value()) {
            const auto& name = *vertex_expr->type_name.value().text;
            if (names.contains(name)) {
                if (vertex_expr->ref.has_value()) {
                    throw std::runtime_error("'" + name + "' is used both as a type name and as a ref name");
                }
                // A reference `x` is the same as `* as x`.
                vertex_expr->type_name.reset();
                vertex_expr->ref = name;
            }
        }
    }
    for (auto& child : expr.iter_children()) {
        resolve_refs(child, names);
    }
}

// Returns how many times each ref occurs in the expression. With the counts for the whole pattern,
// also sets shared refs of negations: those occurring outside of them.
std::map<std::string, int> Parser::resolve_shared_refs(Expression& expr, const std::map<std::string, int>& total_counts) const {
    std::map<std::string, int> counts;
    if (auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->ref.has_value()) {
            ++counts[vertex_expr->ref.value()];
        }
    }
    for (auto& child : expr.iter_children()) {
        for (const auto& [name, count] : resolve_shared_refs(child, total_counts)) {
            counts[name] += count;
        }
    }
    if (auto* operation_expr = boost::get<OperationExpr>(&expr)) {
        if (operation_expr->op_type == OperationType::Unless && !total_counts.empty()) {
            operation_expr->shared_refs.clear();
            for (const auto& [name, count] : counts) {
                if (total_counts.at(name) > count) {
                    operation_expr->shared_refs.push_back(name);
                }
            }
        }
    }
    return counts;
}

} // namespace saengra
