/*
 * lexer.c — Analyse lexicale
 * TÂCHE AMINE (front-end)
 */
#include "lexer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void lexer_init(Lexer *lx, const char *source)
{
    lx->source = source;
    lx->cur = source;
    lx->line = 1;
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_alpha(char c) { return isalpha((unsigned char)c) || c == '_'; }

static char peek(const Lexer *lx) { return *lx->cur; }
static char peek_next(const Lexer *lx) { return lx->cur[0] ? lx->cur[1] : '\0'; }

/* saute espaces, retours à la ligne et commentaires // ... */
static void skip_whitespace(Lexer *lx)
{
    for (;;)
    {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r')
        {
            lx->cur++;
        }
        else if (c == '\n')
        {
            lx->line++;
            lx->cur++;
        }
        else if (c == '/' && peek_next(lx) == '/')
        {
            while (peek(lx) && peek(lx) != '\n')
                lx->cur++;
        }
        else
            break;
    }
}

static Token make_token(Lexer *lx, TokenType type, const char *start, int line)
{
    Token t;
    t.type = type;
    t.start = start;
    t.len = (int)(lx->cur - start);
    t.line = line;
    t.number = 0;
    return t;
}

static TokenType keyword_type(const char *s, int len)
{
    struct
    {
        const char *word;
        TokenType type;
    } kws[] = {
        {"let", TOK_LET},
        {"fn", TOK_FN},
        {"if", TOK_IF},
        {"else", TOK_ELSE},
        {"while", TOK_WHILE},
        {"return", TOK_RETURN},
        {"print", TOK_PRINT},
        {"true", TOK_TRUE},
        {"false", TOK_FALSE},
        {"nil", TOK_NIL},
    };
    for (size_t i = 0; i < sizeof(kws) / sizeof(kws[0]); i++)
    {
        if ((int)strlen(kws[i].word) == len && memcmp(kws[i].word, s, len) == 0)
            return kws[i].type;
    }
    return TOK_IDENT;
}

Token lexer_next(Lexer *lx)
{
    skip_whitespace(lx);

    const char *start = lx->cur;
    int line = lx->line;

    if (!*start)
        return make_token(lx, TOK_EOF, start, line);

    char c = *lx->cur++;

    switch (c)
    {
    case '+':
        return make_token(lx, TOK_PLUS, start, line);
    case '-':
        return make_token(lx, TOK_MINUS, start, line);
    case '*':
        return make_token(lx, TOK_STAR, start, line);
    case '/':
        return make_token(lx, TOK_SLASH, start, line);
    case '%':
        return make_token(lx, TOK_PERCENT, start, line);
    case '(':
        return make_token(lx, TOK_LPAREN, start, line);
    case ')':
        return make_token(lx, TOK_RPAREN, start, line);
    case '{':
        return make_token(lx, TOK_LBRACE, start, line);
    case '}':
        return make_token(lx, TOK_RBRACE, start, line);
    case ',':
        return make_token(lx, TOK_COMMA, start, line);
    case ';':
        return make_token(lx, TOK_SEMI, start, line);
    case '=':
        if (peek(lx) == '=')
        {
            lx->cur++;
            return make_token(lx, TOK_EQ, start, line);
        }
        return make_token(lx, TOK_ASSIGN, start, line);
    case '!':
        if (peek(lx) == '=')
        {
            lx->cur++;
            return make_token(lx, TOK_NEQ, start, line);
        }
        return make_token(lx, TOK_ERROR, start, line);
    case '<':
        if (peek(lx) == '=')
        {
            lx->cur++;
            return make_token(lx, TOK_LE, start, line);
        }
        return make_token(lx, TOK_LT, start, line);
    case '>':
        if (peek(lx) == '=')
        {
            lx->cur++;
            return make_token(lx, TOK_GE, start, line);
        }
        return make_token(lx, TOK_GT, start, line);
    default:
        break;
    }

    if (is_digit(c))
    {
        while (is_digit(peek(lx)))
            lx->cur++;
        if (peek(lx) == '.' && is_digit(peek_next(lx)))
        {
            lx->cur++;
            while (is_digit(peek(lx)))
                lx->cur++;
        }
        Token t = make_token(lx, TOK_NUMBER, start, line);
        t.number = strtod(start, NULL);
        return t;
    }

    if (is_alpha(c))
    {
        while (is_alpha(peek(lx)) || is_digit(peek(lx)))
            lx->cur++;
        Token t = make_token(lx, TOK_IDENT, start, line);
        t.type = keyword_type(start, t.len);
        return t;
    }
    if (c == '"')
    {
        while (peek(lx) != '"' && peek(lx) != '\0' && peek(lx) != '\n')
            lx->cur++;
        if (peek(lx) != '"')
            return make_token(lx, TOK_ERROR, start, line);
        lx->cur++; /* guillemet fermant */
        return make_token(lx, TOK_STRING, start, line);
    }
    return make_token(lx, TOK_ERROR, start, line);
}

const char *token_type_name(TokenType t)
{
    switch (t)
    {
    case TOK_NUMBER:
        return "nombre";
    case TOK_IDENT:
        return "identifiant";
    case TOK_LET:
        return "'let'";
    case TOK_FN:
        return "'fn'";
    case TOK_IF:
        return "'if'";
    case TOK_ELSE:
        return "'else'";
    case TOK_WHILE:
        return "'while'";
    case TOK_RETURN:
        return "'return'";
    case TOK_PRINT:
        return "'print'";
    case TOK_TRUE:
        return "'true'";
    case TOK_FALSE:
        return "'false'";
    case TOK_NIL:
        return "'nil'";
    case TOK_PLUS:
        return "'+'";
    case TOK_MINUS:
        return "'-'";
    case TOK_STAR:
        return "'*'";
    case TOK_SLASH:
        return "'/'";
    case TOK_PERCENT:
        return "'%'";
    case TOK_ASSIGN:
        return "'='";
    case TOK_EQ:
        return "'=='";
    case TOK_NEQ:
        return "'!='";
    case TOK_LT:
        return "'<'";
    case TOK_LE:
        return "'<='";
    case TOK_GT:
        return "'>'";
    case TOK_GE:
        return "'>='";
    case TOK_LPAREN:
        return "'('";
    case TOK_RPAREN:
        return "')'";
    case TOK_LBRACE:
        return "'{'";
    case TOK_RBRACE:
        return "'}'";
    case TOK_COMMA:
        return "','";
    case TOK_SEMI:
        return "';'";
    case TOK_EOF:
        return "fin de fichier";
    case TOK_ERROR:
        return "caractère invalide";
    case TOK_STRING:
        return "chaîne";
    }
    return "?";
}
