%{
#include <string>
#include <cstring>
#include "parser.tab.h"

// For string management in lexer
std::string* make_string(const char* text) {
    return new std::string(text);
}
%}

%option noyywrap
%option never-interactive
%option nounistd

%%

[ \t\n]+                { /* ignore whitespace */ }

"all:"                  { return TOKEN_ALL; }
"if:"                   { return TOKEN_IF; }
"maybe:"                { return TOKEN_MAYBE; }
"unless:"               { return TOKEN_UNLESS; }
"skip:"                 { return TOKEN_SKIP; }
"?="                    { return TOKEN_ASSERT; }
"?!"                    { return TOKEN_NEG_ASSERT; }
"?:"                    { return TOKEN_NON_CAPTURE; }
"repeat"                { return TOKEN_REPEAT; }
"as"                    { return TOKEN_AS; }

"<-"                    { yylval.edge_token = 0; return TOKEN_EDGE_START; }
"<"                     { yylval.edge_token = 1; return TOKEN_EDGE_START; }
"--"                    { return TOKEN_DOUBLE_DASH; }

"->"                    { yylval.edge_token = 0; return TOKEN_EDGE_END; }
">"                     { yylval.edge_token = 1; return TOKEN_EDGE_END; }

"?"                     { return TOKEN_PLACEHOLDER; }
"*"                     { return TOKEN_WILDCARD; }
"."                     { return TOKEN_DOT; }
"|"                     { return TOKEN_OR; }
"&"                     { return TOKEN_AND; }
"("                     { return TOKEN_LPAREN; }
")"                     { return TOKEN_RPAREN; }
":"                     { return TOKEN_COLON; }

"-"                     { yylval.edge_token = 3; return TOKEN_DASH; }

[1-9][0-9]*x            { yylval.str = make_string(yytext); return TOKEN_INTEGER_X; }
0x                      { yylval.str = make_string(yytext); return TOKEN_INTEGER_X; }

[1-9][0-9]*             { yylval.str = make_string(yytext); return TOKEN_INTEGER; }
0                       { yylval.str = make_string(yytext); return TOKEN_INTEGER; }

[a-zA-Z_][a-zA-Z_0-9]*  {
                            // Check for reserved words
                            if (strcmp(yytext, "as") == 0 ||
                                strcmp(yytext, "all") == 0 ||
                                strcmp(yytext, "if") == 0 ||
                                strcmp(yytext, "maybe") == 0 ||
                                strcmp(yytext, "unless") == 0 ||
                                strcmp(yytext, "skip") == 0 ||
                                strcmp(yytext, "repeat") == 0) {
                                // This shouldn't happen due to ordering, but check anyway
                                REJECT;
                            }
                            yylval.str = make_string(yytext);
                            return TOKEN_IDENTIFIER;
                        }

.                       { return yytext[0]; }

%%
