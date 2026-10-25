/*
 * parser.h — Analyse syntaxique (tokens → AST)
 * TÂCHE AMINE (front-end)
 */
#ifndef MLANG_PARSER_H
#define MLANG_PARSER_H

#include <stdbool.h>
#include "ast.h"

typedef struct {
    Arena  *arena;
    Lexer   lexer;
    Token   current;
    Token   previous;
    bool    had_error;
    char    error_msg[256];
    int     error_line;
} Parser;

/* Parse un programme complet. Retourne NULL en cas d'erreur. */
Stmt *parse_program(Parser *p, Arena *arena, const char *source);

#endif /* MLANG_PARSER_H */
