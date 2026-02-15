%code requires {
#include <string>
#include <vector>
#include <memory>
#include "expression.h"
#include "graph.h"

using namespace saengra;
}

%code {
#include <iostream>

int yylex();
void yyerror(const char* s);

// Result of parsing (raw pointer, will be wrapped in unique_ptr later)
saengra::Expression* parse_result_raw = nullptr;
// Vertices container for internalizing type names
saengra::Graph* parse_graph = nullptr;
}

%union {
    std::string* str;
    int edge_token;
    Expression* expr;
    std::vector<std::string>* str_vec;
    std::vector<EdgeLabel>* label_vec;
    std::vector<Expression>* expr_vec;
    OperationType op_type;
    int direction;
}

%token TOKEN_ALL TOKEN_IF TOKEN_MAYBE TOKEN_UNLESS TOKEN_SKIP
%token TOKEN_ASSERT TOKEN_NEG_ASSERT TOKEN_NON_CAPTURE
%token TOKEN_REPEAT TOKEN_AS
%token TOKEN_PLACEHOLDER TOKEN_WILDCARD TOKEN_DOT
%token TOKEN_OR TOKEN_AND
%token TOKEN_LPAREN TOKEN_RPAREN TOKEN_COLON
%token TOKEN_DOUBLE_DASH
%token <edge_token> TOKEN_EDGE_START TOKEN_EDGE_END TOKEN_DASH
%token <str> TOKEN_IDENTIFIER TOKEN_INTEGER TOKEN_INTEGER_X

%type <expr> expression or and concatenation atom operation group vertex edge noop
%type <label_vec> edge_labels edge_label_list
%type <expr_vec> atom_list
%type <op_type> operation_name
%type <edge_token> edge_start edge_end

%left TOKEN_OR
%left TOKEN_AND

%%

expression:
    or { $$ = $1; parse_result_raw = $$; }
    ;

or:
    and { $$ = $1; }
    | or TOKEN_OR and {
        if (auto* or_expr = boost::get<OrExpr>($1)) {
            or_expr->operands.push_back(std::move(*$3));
            delete $3;
            $$ = $1;
        } else {
            OrExpr new_or;
            new_or.operands.push_back(std::move(*$1));
            new_or.operands.push_back(std::move(*$3));
            delete $1;
            delete $3;
            $$ = new Expression{std::move(new_or)};
        }
    }
    ;

and:
    concatenation { $$ = $1; }
    | and TOKEN_AND concatenation {
        if (auto* and_expr = boost::get<AndExpr>($1)) {
            and_expr->operands.push_back(std::move(*$3));
            delete $3;
            $$ = $1;
        } else {
            AndExpr new_and;
            new_and.operands.push_back(std::move(*$1));
            new_and.operands.push_back(std::move(*$3));
            delete $1;
            delete $3;
            $$ = new Expression{std::move(new_and)};
        }
    }
    ;

concatenation:
    /* empty */ { $$ = new Expression{ConcatenationExpr{}}; }
    | atom_list {
        if ($1->size() == 1) {
            $$ = new Expression{std::move($1->front())};
            delete $1;
        } else {
            ConcatenationExpr concat;
            concat.operands = std::move(*$1);
            delete $1;
            $$ = new Expression{std::move(concat)};
        }
    }
    ;

atom_list:
    atom {
        auto* vec = new std::vector<Expression>();
        vec->push_back(std::move(*$1));
        delete $1;
        $$ = vec;
    }
    | atom_list atom {
        $1->push_back(std::move(*$2));
        delete $2;
        $$ = $1;
    }
    ;

atom:
    operation { $$ = $1; }
    | group { $$ = $1; }
    | vertex { $$ = $1; }
    | edge { $$ = $1; }
    | noop { $$ = $1; }
    ;

group:
    TOKEN_LPAREN expression TOKEN_RPAREN { $$ = $2; }
    ;

vertex:
    TOKEN_IDENTIFIER {
        VertexTypeName type_name = parse_graph->internalize_type_name(*$1);
        VertexExpr v(type_name);
        delete $1;
        $$ = new Expression{std::move(v)};
    }
    | TOKEN_IDENTIFIER TOKEN_AS TOKEN_IDENTIFIER {
        VertexTypeName type_name = parse_graph->internalize_type_name(*$1);
        VertexExpr v(type_name);
        v.set_ref = *$3;
        delete $1;
        delete $3;
        $$ = new Expression{std::move(v)};
    }
    | TOKEN_PLACEHOLDER {
        VertexExpr v;
        v.placeholder_idx = 0;
        $$ = new Expression{std::move(v)};
    }
    | TOKEN_PLACEHOLDER TOKEN_AS TOKEN_IDENTIFIER {
        VertexExpr v;
        v.placeholder_idx = 0;
        v.set_ref = *$3;
        delete $3;
        $$ = new Expression{std::move(v)};
    }
    | TOKEN_WILDCARD {
        VertexExpr v;
        $$ = new Expression{std::move(v)};
    }
    | TOKEN_WILDCARD TOKEN_AS TOKEN_IDENTIFIER {
        VertexExpr v;
        v.set_ref = *$3;
        delete $3;
        $$ = new Expression{std::move(v)};
    }
    ;

edge:
    edge_start edge_labels edge_end {
        Direction dir;
        if ($1 == -1 && $3 == 1) {
            dir = Direction::Both;
        } else if ($1 == -1) {
            dir = Direction::Backward;
        } else if ($3 == 1) {
            dir = Direction::Forward;
        } else {
            dir = Direction::Any;
        }

        EdgeExpr e;
        e.direction = dir;
        e.labels = std::move(*$2);
        delete $2;
        $$ = new Expression{std::move(e)};
    }
    | edge_start TOKEN_WILDCARD edge_end {
        Direction dir;
        if ($1 == -1 && $3 == 1) {
            dir = Direction::Both;
        } else if ($1 == -1) {
            dir = Direction::Backward;
        } else if ($3 == 1) {
            dir = Direction::Forward;
        } else {
            dir = Direction::Any;
        }

        EdgeExpr e;
        e.direction = dir;
        $$ = new Expression{std::move(e)};
    }
    ;

edge_start:
    TOKEN_EDGE_START {
        $$ = -1;
    }
    | TOKEN_DOUBLE_DASH { $$ = 0; }
    | TOKEN_DASH { $$ = 0; }
    ;

edge_end:
    TOKEN_EDGE_END {
        $$ = 1;
    }
    | TOKEN_DOUBLE_DASH { $$ = 0; }
    | TOKEN_DASH { $$ = 0; }
    ;

edge_labels:
    edge_label_list { $$ = $1; }
    ;

edge_label_list:
    TOKEN_IDENTIFIER {
        auto* vec = new std::vector<EdgeLabel>();
        auto label = parse_graph->internalize_label(*$1);
        vec->push_back(label);
        delete $1;
        $$ = vec;
    }
    | edge_label_list TOKEN_OR TOKEN_IDENTIFIER {
        auto label = parse_graph->internalize_label(*$3);
        $1->push_back(label);
        delete $3;
        $$ = $1;
    }
    ;

operation:
    TOKEN_LPAREN operation_name expression TOKEN_RPAREN {
        OperationExpr op{$2, std::move(*$3)};
        delete $3;
        $$ = new Expression{std::move(op)};
    }
    | TOKEN_LPAREN TOKEN_REPEAT TOKEN_INTEGER_X TOKEN_COLON expression TOKEN_RPAREN {
        std::string num_str = *$3;
        int repeats = std::stoi(num_str.substr(0, num_str.length() - 1));
        delete $3;
        RepetitionExpr rep{repeats, repeats, std::move(*$5)};
        delete $5;
        $$ = new Expression{std::move(rep)};
    }
    | TOKEN_LPAREN TOKEN_REPEAT TOKEN_INTEGER TOKEN_DASH TOKEN_INTEGER_X TOKEN_COLON expression TOKEN_RPAREN {
        int min = std::stoi(*$3);
        std::string max_str = *$5;
        int max = std::stoi(max_str.substr(0, max_str.length() - 1));
        delete $3;
        delete $5;
        RepetitionExpr rep{min, max, std::move(*$7)};
        delete $7;
        $$ = new Expression{std::move(rep)};
    }
    ;

operation_name:
    TOKEN_ALL { $$ = OperationType::All; }
    | TOKEN_IF { $$ = OperationType::If; }
    | TOKEN_ASSERT { $$ = OperationType::If; }
    | TOKEN_UNLESS { $$ = OperationType::Unless; }
    | TOKEN_NEG_ASSERT { $$ = OperationType::Unless; }
    | TOKEN_SKIP { $$ = OperationType::Skip; }
    | TOKEN_NON_CAPTURE { $$ = OperationType::Skip; }
    | TOKEN_MAYBE { $$ = OperationType::Maybe; }
    ;

noop:
    TOKEN_DOT {
        NoopExpr noop;
        $$ = new Expression{std::move(noop)};
    }
    ;

%%

void yyerror(const char* s) {
    std::cerr << "Parse error: " << s << std::endl;
}
