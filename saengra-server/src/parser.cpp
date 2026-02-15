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

        std::vector<std::string> refs;
        update_set_refs(*parse_result_raw, refs);

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

void Parser::update_set_refs(Expression& expr, std::vector<std::string>& refs) const {
    if (auto* vertex_expr = boost::get<VertexExpr>(&expr)) {
        if (vertex_expr->set_ref.has_value()) {
            const auto& ref = vertex_expr->set_ref.value();
            const auto it = std::find(refs.begin(), refs.end(), ref);
            if (it != refs.end()) {
                // Might actually be OK in some cases.
                // throw std::runtime_error("ref " + ref + " set twice");
            } else {
                refs.push_back(ref);
            }
        }

        if (vertex_expr->type_name.has_value()) {
            const auto& name = vertex_expr->type_name.value().text;
            const auto it = std::find(refs.begin(), refs.end(), *name);
            if (it != refs.end()) {
                vertex_expr->type_name.reset();
                vertex_expr->match_ref = *it;
            }
        }
    }
    for (auto& child : expr.iter_children()) {
        update_set_refs(child, refs);
    }
}

} // namespace saengra
