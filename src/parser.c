/*
 * parser.c — Analyse syntaxique (descente récursive + Pratt pour les expr.)
 * TÂCHE AMINE (front-end)
 */
#include "parser.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= Arène ======================= */
struct ArenaBlock {
    struct ArenaBlock *next;
    size_t used;
    size_t cap;
    char   data[];
};

void *arena_alloc(Arena *a, size_t size)
{
    size  = (size + 15) & ~(size_t)15;   /* alignement 16 octets */
    if (!a->head || a->head->used + size > a->head->cap) {
        size_t cap = size > 4096 ? size : 4096;
        ArenaBlock *b = malloc(sizeof(ArenaBlock) + cap);
        if (!b) { fprintf(stderr, "memoire epuisee\n"); exit(1); }
        b->next = a->head;
        b->used = 0;
        b->cap  = cap;
        a->head = b;
    }
    void *p = a->head->data + a->head->used;
    a->head->used += size;
    memset(p, 0, size);
    return p;
}

void arena_free(Arena *a)
{
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}

/* ======================= Helpers ======================= */
static void advance(Parser *p)
{
    p->previous = p->current;
    p->current  = lexer_next(&p->lexer);
}

static bool check(Parser *p, TokenType t) { return p->current.type == t; }

static bool match(Parser *p, TokenType t)
{
    if (!check(p, t)) return false;
    advance(p);
    return true;
}

static void set_error(Parser *p, const char *msg, int line)
{
    if (!p->had_error) {
        p->had_error  = true;
        p->error_line = line;
        snprintf(p->error_msg, sizeof(p->error_msg), "%s", msg);
    }
}

static bool expect(Parser *p, TokenType t, const char *what)
{
    if (match(p, t)) return true;
    char msg[256];
    snprintf(msg, sizeof(msg), "attendu : %s (trouve : %s)",
             what, token_type_name(p->current.type));
    set_error(p, msg, p->current.line);
    return false;
}

static char *copy_token(Parser *p, Arena *a, Token t)
{
    char *s = arena_alloc(a, (size_t)t.len + 1);
    memcpy(s, t.start, (size_t)t.len);
    s[t.len] = '\0';
    return s;
}
static ObjStr *copy_string_token(Arena *a, Token t)
{
    /* t.start / t.len incluent les deux guillemets */
    int len = t.len >= 2 ? t.len - 2 : 0;

    char *chars = arena_alloc(a, (size_t)len + 1);
    memcpy(chars, t.start + 1, (size_t)len);
    chars[len] = '\0';

    ObjStr *s = arena_alloc(a, sizeof(ObjStr));
    s->chars  = chars;
    s->length = len;
    return s;
}

/* ======================= Expressions ======================= */
static Expr *new_expr(Parser *p, Arena *a, ExprKind kind, int line)
{
    Expr *e = arena_alloc(a, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

static Expr *parse_expression(Parser *p);
static Expr *parse_assignment(Parser *p);

static Expr *parse_primary(Parser *p)
{
    int line = p->current.line;

    if (check(p, TOK_NUMBER)) {
        double n = p->current.number;
        advance(p);
        Expr *e = new_expr(p, p->arena, EXPR_NUMBER, line);
        e->as.number = n;
        return e;
    }
        if (check(p, TOK_STRING)) {
        Token t = p->current;
        advance(p);
        Expr *e = new_expr(p, p->arena, EXPR_STRING, line);
        e->as.string = copy_string_token(p->arena, t);
        return e;
    }

    if (check(p, TOK_TRUE) || check(p, TOK_FALSE)) {
        bool b = check(p, TOK_TRUE);
        advance(p);
        Expr *e = new_expr(p, p->arena, EXPR_BOOL, line);
        e->as.boolean = b;
        return e;
    }

    if (match(p, TOK_NIL)) {
        return new_expr(p, p->arena, EXPR_NIL, line);
    }

    if (check(p, TOK_IDENT)) {
        Token t = p->current;
        advance(p);
        if (check(p, TOK_LPAREN)) {
            advance(p);
            Expr **args = NULL;
            int nargs = 0, cap = 0;
            if (!check(p, TOK_RPAREN)) {
                do {
                    if (nargs == cap) {
                        cap = cap ? cap * 2 : 4;
                        Expr **tmp = realloc(args, sizeof(Expr *) * (size_t)cap);
                        if (!tmp) { free(args); set_error(p, "memoire epuisee", line); return NULL; }
                        args = tmp;
                    }
                    Expr *arg = parse_expression(p);
                    if (!arg) { free(args); return NULL; }
                    args[nargs++] = arg;
                } while (match(p, TOK_COMMA));
            }
            if (!expect(p, TOK_RPAREN, "')'")) { free(args); return NULL; }
            Expr *e = new_expr(p, p->arena, EXPR_CALL, line);
            e->as.call.name  = copy_token(p, p->arena, t);
            e->as.call.len   = t.len;
            e->as.call.args  = args;
            e->as.call.nargs = nargs;
            return e;
        }
        Expr *e = new_expr(p, p->arena, EXPR_VAR, line);
        e->as.var.name = copy_token(p, p->arena, t);
        e->as.var.len  = t.len;
        return e;
    }

    if (match(p, TOK_LPAREN)) {
        Expr *e = parse_expression(p);
        if (!expect(p, TOK_RPAREN, "')'")) return NULL;
        return e;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "expression attendue (trouve : %s)",
             token_type_name(p->current.type));
    set_error(p, msg, line);
    return NULL;
}

static Expr *parse_call(Parser *p) { return parse_primary(p); }

static Expr *parse_unary(Parser *p)
{
    if (match(p, TOK_MINUS)) {
        int line = p->previous.line;
        Expr *operand = parse_unary(p);
        if (!operand) return NULL;
        Expr *e = new_expr(p, p->arena, EXPR_UNARY, line);
        e->as.unary.op = TOK_MINUS;
        e->as.unary.operand = operand;
        return e;
    }
    return parse_call(p);
}

static Expr *parse_binary(Parser *p, Expr *(*next)(Parser *), const TokenType *ops, int nops)
{
    Expr *left = next(p);
    if (!left) return NULL;
    for (;;) {
        int found = -1;
        for (int i = 0; i < nops; i++)
            if (check(p, ops[i])) { found = i; break; }
        if (found < 0) return left;

        TokenType op = ops[found];
        int line = p->current.line;
        advance(p);

        Expr *right = next(p);
        if (!right) return NULL;

        Expr *e = new_expr(p, p->arena, EXPR_BINARY, line);
        e->as.binary.op    = op;
        e->as.binary.left  = left;
        e->as.binary.right = right;
        left = e;
    }
}

static Expr *parse_factor(Parser *p)
{
    static const TokenType ops[] = { TOK_STAR, TOK_SLASH, TOK_PERCENT };
    return parse_binary(p, parse_unary, ops, 3);
}

static Expr *parse_term(Parser *p)
{
    static const TokenType ops[] = { TOK_PLUS, TOK_MINUS };
    return parse_binary(p, parse_factor, ops, 2);
}

static Expr *parse_comparison(Parser *p)
{
    static const TokenType ops[] = { TOK_LT, TOK_LE, TOK_GT, TOK_GE };
    return parse_binary(p, parse_term, ops, 4);
}

static Expr *parse_equality(Parser *p)
{
    static const TokenType ops[] = { TOK_EQ, TOK_NEQ };
    return parse_binary(p, parse_comparison, ops, 2);
}

static Expr *parse_assignment(Parser *p)
{
    /* IDENT '=' expr  → EXPR_ASSIGN */
    if (check(p, TOK_IDENT)) {
        Token t = p->current;
        /* lookahead simple : on avance puis on revient si ce n'est pas '=' */
        const char *save_cur = p->lexer.cur;
        int save_line = p->lexer.line;
        Token save_tok = p->current;
        advance(p);
        if (check(p, TOK_ASSIGN)) {
            advance(p);
            Expr *value = parse_assignment(p);
            if (!value) return NULL;
            Expr *e = new_expr(p, p->arena, EXPR_ASSIGN, t.line);
            e->as.var.name = copy_token(p, p->arena, t);
            e->as.var.len  = t.len;
            e->as.binary.left = value;   /* on réutilise le champ */
            (void)save_cur; (void)save_line; (void)save_tok;
            return e;
        }
        /* pas une affectation : on rembobine et on repart sur l'égalité */
        p->lexer.cur  = save_cur;
        p->lexer.line = save_line;
        p->current    = save_tok;
        p->previous   = save_tok;
    }
    return parse_equality(p);
}

static Expr *parse_expression(Parser *p) { return parse_assignment(p); }

/* ======================= Instructions ======================= */
static Stmt *new_stmt(Parser *p, Arena *a, StmtKind kind, int line)
{
    Stmt *s = arena_alloc(a, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}

static Stmt *parse_statement(Parser *p);
static Stmt *parse_block(Parser *p);

static Stmt *parse_let(Parser *p)
{
    int line = p->previous.line;
    if (!check(p, TOK_IDENT)) {
        set_error(p, "nom de variable attendu apres 'let'", p->current.line);
        return NULL;
    }
    Token name = p->current;
    advance(p);

    Expr *value = NULL;
    if (match(p, TOK_ASSIGN)) {
        value = parse_expression(p);
        if (!value) return NULL;
    }
    if (!expect(p, TOK_SEMI, "';'")) return NULL;

    Stmt *s = new_stmt(p, p->arena, STMT_LET, line);
    s->as.let.name  = copy_token(p, p->arena, name);
    s->as.let.len   = name.len;
    s->as.let.value = value;
    return s;
}

static Stmt *parse_print(Parser *p)
{
    int line = p->previous.line;
    if (!expect(p, TOK_LPAREN, "'('")) return NULL;
    Expr *e = parse_expression(p);
    if (!e) return NULL;
    if (!expect(p, TOK_RPAREN, "')'")) return NULL;
    if (!expect(p, TOK_SEMI, "';'")) return NULL;

    Stmt *s = new_stmt(p, p->arena, STMT_PRINT, line);
    s->as.print_expr = e;
    return s;
}

static Stmt *parse_if(Parser *p)
{
    int line = p->previous.line;
    if (!expect(p, TOK_LPAREN, "'('")) return NULL;
    Expr *cond = parse_expression(p);
    if (!cond) return NULL;
    if (!expect(p, TOK_RPAREN, "')'")) return NULL;

    Stmt *then_branch = parse_block(p);
    if (!then_branch) return NULL;

    Stmt *else_branch = NULL;
    if (match(p, TOK_ELSE)) {
        if (check(p, TOK_IF)) {
            advance(p);
            else_branch = parse_if(p);
        } else {
            else_branch = parse_block(p);
        }
        if (!else_branch) return NULL;
    }

    Stmt *s = new_stmt(p, p->arena, STMT_IF, line);
    s->as.if_stmt.condition   = cond;
    s->as.if_stmt.then_branch = then_branch;
    s->as.if_stmt.else_branch = else_branch;
    return s;
}

static Stmt *parse_while(Parser *p)
{
    int line = p->previous.line;
    if (!expect(p, TOK_LPAREN, "'('")) return NULL;
    Expr *cond = parse_expression(p);
    if (!cond) return NULL;
    if (!expect(p, TOK_RPAREN, "')'")) return NULL;

    Stmt *body = parse_block(p);
    if (!body) return NULL;

    Stmt *s = new_stmt(p, p->arena, STMT_WHILE, line);
    s->as.while_stmt.condition = cond;
    s->as.while_stmt.body      = body;
    return s;
}

static Stmt *parse_return(Parser *p)
{
    int line = p->previous.line;
    Expr *value = NULL;
    if (!check(p, TOK_SEMI)) {
        value = parse_expression(p);
        if (!value) return NULL;
    }
    if (!expect(p, TOK_SEMI, "';'")) return NULL;

    Stmt *s = new_stmt(p, p->arena, STMT_RETURN, line);
    s->as.return_expr = value;
    return s;
}

static Stmt *parse_func(Parser *p, Arena *a)
{
    int line = p->previous.line;
    if (!check(p, TOK_IDENT)) {
        set_error(p, "nom de fonction attendu apres 'fn'", p->current.line);
        return NULL;
    }
    Token name = p->current;
    advance(p);

    if (!expect(p, TOK_LPAREN, "'('")) return NULL;

    char **params = NULL;
    int   *plens  = NULL;
    int    nparams = 0;

    if (!check(p, TOK_RPAREN)) {
        do {
            if (!check(p, TOK_IDENT)) {
                set_error(p, "nom de parametre attendu", p->current.line);
                return NULL;
            }
            Token pt = p->current;
            advance(p);
            char **np = realloc(params, sizeof(char *) * (size_t)(nparams + 1));
            int   *nl = realloc(plens,  sizeof(int)    * (size_t)(nparams + 1));
            if (!np || !nl) { free(np); free(nl); set_error(p, "memoire epuisee", line); return NULL; }
            params = np;
            plens  = nl;
            params[nparams] = copy_token(p, a, pt);
            plens[nparams]  = pt.len;
            nparams++;
        } while (match(p, TOK_COMMA));
    }
    if (!expect(p, TOK_RPAREN, "')'")) return NULL;

    Stmt *body = parse_block(p);
    if (!body) return NULL;

    Stmt *s = new_stmt(p, a, STMT_FUNC, line);
    s->as.func.name       = copy_token(p, a, name);
    s->as.func.len        = name.len;
    s->as.func.params     = params;
    s->as.func.param_lens = plens;
    s->as.func.nparams    = nparams;
    s->as.func.body       = body;
    return s;
}

static Stmt *parse_block(Parser *p)
{
    int line = p->current.line;
    if (!expect(p, TOK_LBRACE, "'{'")) return NULL;

    Stmt *block = new_stmt(p, p->arena, STMT_BLOCK, line);
    Stmt *tail  = NULL;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Stmt *s = parse_statement(p);
        if (!s) { arena_free(p->arena); return NULL; }
        if (!block->as.block_first) block->as.block_first = s;
        else                        tail->next = s;
        tail = s;
    }
    if (!expect(p, TOK_RBRACE, "'}'")) return NULL;
    return block;
}

static Stmt *parse_statement(Parser *p)
{
    if (match(p, TOK_LET))    return parse_let(p);
    if (match(p, TOK_PRINT))  return parse_print(p);
    if (match(p, TOK_IF))     return parse_if(p);
    if (match(p, TOK_WHILE))  return parse_while(p);
    if (match(p, TOK_RETURN)) return parse_return(p);
    if (check(p, TOK_LBRACE)) return parse_block(p);

    int line = p->current.line;
    Expr *e = parse_expression(p);
    if (!e) return NULL;
    if (!expect(p, TOK_SEMI, "';'")) return NULL;

    Stmt *s = new_stmt(p, p->arena, STMT_EXPR, line);
    s->as.expr = e;
    return s;
}

/* ======================= Point d'entrée ======================= */
Stmt *parse_program(Parser *p, Arena *arena, const char *source)
{
    memset(p, 0, sizeof(*p));
    p->arena = arena;
    lexer_init(&p->lexer, source);
    advance(p);

    Stmt *program = arena_alloc(arena, sizeof(Stmt));
    program->kind = STMT_BLOCK;
    program->line = 1;
    program->as.block_first = NULL;

    Stmt *tail = NULL;
    while (!check(p, TOK_EOF)) {
        Stmt *s;
        if (check(p, TOK_FN)) {
            advance(p);
            s = parse_func(p, arena);
        } else {
            s = parse_statement(p);
        }
        if (!s) return NULL;

        if (!program->as.block_first) program->as.block_first = s;
        else                          tail->next = s;
        tail = s;
    }
    return program;
}
