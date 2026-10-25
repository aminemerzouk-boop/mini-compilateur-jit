/*
 * lexer.h — Analyse lexicale (source → tokens)
 * ----------------------------------------------
 * TÂCHE AMINE (front-end)
 */
#ifndef MLANG_LEXER_H
#define MLANG_LEXER_H

#include <stddef.h>

typedef enum
{
    /* littéraux */
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENT,
    /* mots-clés */
    TOK_LET,
    TOK_FN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_RETURN,
    TOK_PRINT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NIL,
    /* opérateurs */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    /* ponctuation */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_SEMI,
    /* fin / erreur */
    TOK_EOF,
    TOK_ERROR
} TokenType;

typedef struct
{
    TokenType type;
    const char *start; /* pointeur dans le source (pas de copie) */
    int len;
    int line;
    double number; /* valide si type == TOK_NUMBER */
} Token;

typedef struct
{
    const char *source;
    const char *cur;
    int line;
} Lexer;

void lexer_init(Lexer *lx, const char *source);
Token lexer_next(Lexer *lx);
const char *token_type_name(TokenType t);

#endif /* MLANG_LEXER_H */
