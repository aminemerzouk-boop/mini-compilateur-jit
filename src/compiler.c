/*
 * compiler.c — AST → bytecode
 * TÂCHE AMINE (front-end)
 *
 * v0.1 : pas de portée par bloc, les 'let' déclarent un slot par fonction.
 */
#include "compiler.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCAL_NAMES 256

typedef struct
{
    BytecodeProgram *prog;
    int fn; /* fonction en cours de compilation */
    int nlocals;
    int cur_line; /* ligne source en cours */
    char local_names[MAX_LOCAL_NAMES][64];
    int has_error;
    char error_msg[256];
    int error_line;
} Compiler;

static void emit(Compiler *c, uint8_t op, int32_t a, int32_t b)
{
    Chunk *ch = &c->prog->funcs[c->fn].chunk;
    if (ch->count == ch->cap)
    {
        int ncap = ch->cap ? ch->cap * 2 : 64;
        Instr *n = realloc(ch->code, sizeof(Instr) * (size_t)ncap);
        if (!n)
        {
            fprintf(stderr, "memoire epuisee\n");
            exit(1);
        }
        ch->code = n;
        ch->cap = ncap;
    }
    ch->code[ch->count].op = op;
    ch->code[ch->count].a = a;
    ch->code[ch->count].b = b;
    ch->code[ch->count].line = c->cur_line;
    ch->count++;
}

static int emit_jump(Compiler *c, uint8_t op)
{
    emit(c, op, -1, 0);
    return c->prog->funcs[c->fn].chunk.count - 1;
}

static void patch_jump(Compiler *c, int at)
{
    c->prog->funcs[c->fn].chunk.code[at].a = c->prog->funcs[c->fn].chunk.count;
}

static int here(Compiler *c) { return c->prog->funcs[c->fn].chunk.count; }

static void error_at(Compiler *c, int line, const char *msg)
{
    if (!c->has_error)
    {
        c->has_error = true;
        c->error_line = line;
        snprintf(c->error_msg, sizeof(c->error_msg), "%s", msg);
    }
}

static int add_const(Compiler *c, Value v)
{
    BytecodeProgram *p = c->prog;
    for (int i = 0; i < p->nconsts; i++)
        if (is_num(p->consts[i]) && is_num(v) &&
            as_num(p->consts[i]) == as_num(v))
            return i;
    if (p->nconsts >= MLANG_MAX_CONSTS)
    {
        error_at(c, 0, "trop de constantes");
        return 0;
    }
    p->consts[p->nconsts] = v;
    return p->nconsts++;
}

static int find_func(Compiler *c, const char *name)
{
    for (int i = 0; i < c->prog->nfuncs; i++)
        if (strcmp(c->prog->funcs[i].name, name) == 0)
            return i;
    return -1;
}

static int find_local(Compiler *c, const char *name)
{
    for (int i = c->nlocals - 1; i >= 0; i--)
        if (strcmp(c->local_names[i], name) == 0)
            return i;
    return -1;
}

/* variables globales : on les retrouve par leur nom dans le script */
static int global_count; /* noms globaux indexés      */
static char global_names[MLANG_MAX_GLOBALS][64];

static int find_global(Compiler *c, const char *name)
{
    for (int i = 0; i < global_count; i++)
        if (strcmp(global_names[i], name) == 0)
            return i;
    return -1;
}

static int add_global(Compiler *c, const char *name)
{
    if (global_count >= MLANG_MAX_GLOBALS)
    {
        error_at(c, 0, "trop de variables globales");
        return 0;
    }
    snprintf(global_names[global_count], 64, "%s", name);
    c->prog->nglobals = global_count + 1;
    return global_count++;
}

static void compile_expr(Compiler *c, Expr *e);
static void compile_stmt(Compiler *c, Stmt *s);
static void compile_block(Compiler *c, Stmt *first);

static void compile_expr(Compiler *c, Expr *e)
{
    c->cur_line = e->line;
    switch (e->kind)
    {
    case EXPR_NUMBER:
        emit(c, OP_CONST, add_const(c, value_num(e->as.number)), 0);
        break;
    case EXPR_STRING:
        emit(c, OP_CONST, add_const(c, value_str(e->as.string)), 0);
        break;

    case EXPR_BOOL:
        emit(c, OP_CONST, add_const(c, value_bool(e->as.boolean)), 0);
        break;

    case EXPR_NIL:
        emit(c, OP_NIL, 0, 0);
        break;
    case EXPR_VAR:
    {
        int slot = find_local(c, e->as.var.name);
        if (slot >= 0)
        {
            emit(c, OP_GET_LOCAL, slot, 0);
            break;
        }
        int g = find_global(c, e->as.var.name);
        if (g < 0)
        {
            char msg[160];
            snprintf(msg, sizeof(msg), "variable inconnue : '%s'", e->as.var.name);
            error_at(c, e->line, msg);
            emit(c, OP_NIL, 0, 0);
            break;
        }
        emit(c, OP_GET_GLOBAL, g, 0);
        break;
    }

    case EXPR_ASSIGN:
    {
        compile_expr(c, e->as.binary.left); /* la valeur */
        int slot = find_local(c, e->as.var.name);
        if (slot >= 0)
        {
            emit(c, OP_SET_LOCAL, slot, 0);
            break;
        }
        int g = find_global(c, e->as.var.name);
        if (g < 0)
        {
            char msg[160];
            snprintf(msg, sizeof(msg), "affectation a une variable inconnue : '%s'",
                     e->as.var.name);
            error_at(c, e->line, msg);
            break;
        }
        emit(c, OP_SET_GLOBAL, g, 0);
        break;
    }

    case EXPR_BINARY:
    {
        compile_expr(c, e->as.binary.left);
        compile_expr(c, e->as.binary.right);
        uint8_t op;
        switch (e->as.binary.op)
        {
        case TOK_PLUS:
            op = OP_ADD;
            break;
        case TOK_MINUS:
            op = OP_SUB;
            break;
        case TOK_STAR:
            op = OP_MUL;
            break;
        case TOK_SLASH:
            op = OP_DIV;
            break;
        case TOK_PERCENT:
            op = OP_MOD;
            break;
        case TOK_EQ:
            op = OP_EQ;
            break;
        case TOK_NEQ:
            op = OP_NEQ;
            break;
        case TOK_LT:
            op = OP_LT;
            break;
        case TOK_LE:
            op = OP_LE;
            break;
        case TOK_GT:
            op = OP_GT;
            break;
        case TOK_GE:
            op = OP_GE;
            break;
        default:
            op = OP_ADD;
            break;
        }
        c->cur_line = e->line; /* les enfants ont pu changer cur_line */
        emit(c, op, 0, 0);
        break;
    }

    case EXPR_UNARY:
        compile_expr(c, e->as.unary.operand);
        emit(c, e->as.unary.op == TOK_MINUS ? OP_NEG : OP_NOT, 0, 0);
        break;

    case EXPR_CALL:
    {
        int idx = find_func(c, e->as.call.name);
        if (idx < 0)
        {
            char msg[160];
            snprintf(msg, sizeof(msg), "fonction inconnue : '%s'", e->as.call.name);
            error_at(c, e->line, msg);
            emit(c, OP_NIL, 0, 0);
            break;
        }
        for (int i = 0; i < e->as.call.nargs; i++)
            compile_expr(c, e->as.call.args[i]);
        emit(c, OP_CALL, idx, e->as.call.nargs);
        break;
    }
    }
}

static void compile_stmt(Compiler *c, Stmt *s)
{
    switch (s->kind)
    {
    case STMT_LET:
    {
        if (s->as.let.value)
            compile_expr(c, s->as.let.value);
        else
            emit(c, OP_NIL, 0, 0);

        if (c->fn == c->prog->script_func)
        { /* niveau global */
            int g = add_global(c, s->as.let.name);
            emit(c, OP_SET_GLOBAL, g, 0);
            emit(c, OP_POP, 0, 0);
        }
        else
        { /* local */
            if (c->nlocals >= MLANG_MAX_LOCALS)
            {
                error_at(c, s->line, "trop de variables locales");
                break;
            }
            snprintf(c->local_names[c->nlocals], 64, "%s", s->as.let.name);
            emit(c, OP_SET_LOCAL, c->nlocals, 0);
            emit(c, OP_POP, 0, 0);
            c->nlocals++;
            if (c->nlocals > c->prog->funcs[c->fn].nlocals)
                c->prog->funcs[c->fn].nlocals = c->nlocals;
        }
        break;
    }

    case STMT_PRINT:
        compile_expr(c, s->as.print_expr);
        emit(c, OP_PRINT, 0, 0);
        break;

    case STMT_IF:
    {
        compile_expr(c, s->as.if_stmt.condition);
        int jf = emit_jump(c, OP_JUMP_IF_FALSE);
        compile_block(c, s->as.if_stmt.then_branch->as.block_first);
        if (s->as.if_stmt.else_branch)
        {
            int jend = emit_jump(c, OP_JUMP);
            patch_jump(c, jf);
            compile_stmt(c, s->as.if_stmt.else_branch);
            patch_jump(c, jend);
        }
        else
        {
            patch_jump(c, jf);
        }
        break;
    }

    case STMT_WHILE:
    {
        int start = here(c);
        compile_expr(c, s->as.while_stmt.condition);
        int jf = emit_jump(c, OP_JUMP_IF_FALSE);
        compile_block(c, s->as.while_stmt.body->as.block_first);
        emit(c, OP_JUMP, start, 0);
        patch_jump(c, jf);
        break;
    }

    case STMT_RETURN:
        if (s->as.return_expr)
            compile_expr(c, s->as.return_expr);
        else
            emit(c, OP_NIL, 0, 0);
        emit(c, OP_RETURN, 0, 0);
        break;

    case STMT_BLOCK:
        compile_block(c, s->as.block_first);
        break;

    case STMT_EXPR:
        compile_expr(c, s->as.expr);
        emit(c, OP_POP, 0, 0);
        break;

    case STMT_FUNC:
        /* compilée séparément dans compile_program */
        break;
    }
}

static void compile_block(Compiler *c, Stmt *first)
{
    for (Stmt *s = first; s; s = s->next)
        compile_stmt(c, s);
}

/* ======================= Point d'entrée ======================= */
int compile_program(Stmt *program, BytecodeProgram *out)
{
    memset(out, 0, sizeof(*out));
    global_count = 0;

    Compiler c;
    memset(&c, 0, sizeof(c));
    c.prog = out;

    /* --- passe 1 : recenser les fonctions (permet les appels en avant) --- */
    snprintf(out->funcs[0].name, 64, "<script>");
    out->funcs[0].arity = 0;
    out->funcs[0].nlocals = 0;
    out->nfuncs = 1;
    out->script_func = 0;

    for (Stmt *s = program->as.block_first; s; s = s->next)
    {
        if (s->kind != STMT_FUNC)
            continue;
        if (out->nfuncs >= MLANG_MAX_FUNCS)
        {
            fprintf(stderr, "erreur : trop de fonctions\n");
            return -1;
        }
        int idx = out->nfuncs++;
        snprintf(out->funcs[idx].name, 64, "%s", s->as.func.name);
        out->funcs[idx].arity = s->as.func.nparams;
        out->funcs[idx].nlocals = s->as.func.nparams;
    }

    /* --- passe 2 : compiler le script (index 0) --- */
    c.fn = 0;
    c.nlocals = 0;
    for (Stmt *s = program->as.block_first; s; s = s->next)
        if (s->kind != STMT_FUNC)
            compile_stmt(&c, s);
    emit(&c, OP_NIL, 0, 0);
    emit(&c, OP_RETURN, 0, 0);

    if (c.has_error)
    {
        fprintf(stderr, "erreur de compilation (ligne %d) : %s\n", c.error_line, c.error_msg);
        return -1;
    }

    /* --- passe 3 : compiler chaque fonction déclarée --- */
    int idx = 1;
    for (Stmt *s = program->as.block_first; s; s = s->next)
    {
        if (s->kind != STMT_FUNC)
            continue;

        c.fn = idx;
        c.nlocals = 0;
        memset(c.local_names, 0, sizeof(c.local_names));

        for (int i = 0; i < s->as.func.nparams; i++)
        {
            snprintf(c.local_names[c.nlocals], 64, "%s", s->as.func.params[i]);
            c.nlocals++;
        }
        compile_block(&c, s->as.func.body->as.block_first);
        emit(&c, OP_NIL, 0, 0);
        emit(&c, OP_RETURN, 0, 0);

        if (c.has_error)
        {
            fprintf(stderr, "erreur de compilation dans '%s' (ligne %d) : %s\n",
                    s->as.func.name, c.error_line, c.error_msg);
            return -1;
        }
        idx++;
    }

    return 0;
}
