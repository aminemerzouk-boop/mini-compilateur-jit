/*
 * ast.h — Arbre Syntaxique Abstrait (AST) + arène d'allocation
 * ------------------------------------------------------------
 * TÂCHE AMINE (front-end)
 *
 * Grammaire v0.1 :
 *   program    := decl*
 *   decl       := funcDef | stmt
 *   funcDef    := "fn" IDENT "(" params? ")" block
 *   stmt       := letStmt | printStmt | ifStmt | whileStmt | returnStmt
 *               | block | exprStmt
 *   letStmt    := "let" IDENT "=" expr ";"
 *   printStmt  := "print" "(" expr ")" ";"
 *   ifStmt     := "if" "(" expr ")" block ("else" (block | ifStmt))?
 *   whileStmt  := "while" "(" expr ")" block
 *   returnStmt := "return" expr? ";"
 *   block      := "{" stmt* "}"
 *   exprStmt   := expr ";"
 *   expr       := assign
 *   assign     := IDENT "=" assign | equality
 *   equality   := comparison (("==" | "!=") comparison)*
 *   comparison := additive (("<" | "<=" | ">" | ">=") additive)*
 *   additive   := multiplicative (("+" | "-") multiplicative)*
 *   multiplicative := unary (("*" | "/" | "%") unary)*
 *   unary      := ("-" | "!") unary | call
 *   call       := primary ("(" args? ")")*
 *   primary    := NUMBER | IDENT | "(" expr ")"
 */
#ifndef MLANG_AST_H
#define MLANG_AST_H

#include <stddef.h>
#include "lexer.h"
#include <stdbool.h>
#include "value.h"
/* ------------------------------------------------------------------ */
/* Arène : toutes les allocations de l'AST, libérées d'un coup          */
/* ------------------------------------------------------------------ */
typedef struct ArenaBlock ArenaBlock;
typedef struct
{
    ArenaBlock *head;
} Arena;

void *arena_alloc(Arena *a, size_t size);
void arena_free(Arena *a);

/* ------------------------------------------------------------------ */
/* Expressions                                                         */
/* ------------------------------------------------------------------ */
typedef enum
{
    EXPR_NUMBER,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_NIL,
    EXPR_VAR,
    EXPR_ASSIGN,
    EXPR_BINARY,
    EXPR_UNARY,
    EXPR_CALL
} ExprKind;

typedef struct Expr Expr;

struct Expr
{
    ExprKind kind;
    int line;
    union
    {
        double number;  /* EXPR_NUMBER            */
        ObjStr *string; /* EXPR_STRING            */
        bool boolean;   /* EXPR_BOOL              */
        struct
        {
            char *name;
            int len;
        } var; /* EXPR_VAR / EXPR_ASSIGN */
        struct
        {
            TokenType op;
            Expr *left;
            Expr *right;
        } binary;
        struct
        {
            TokenType op;
            Expr *operand;
        } unary;
        struct
        {
            char *name;
            int len;
            Expr **args;
            int nargs;
        } call;
    } as;
};

/* ------------------------------------------------------------------ */
/* Instructions                                                        */
/* ------------------------------------------------------------------ */
typedef enum
{
    STMT_LET,
    STMT_PRINT,
    STMT_IF,
    STMT_WHILE,
    STMT_RETURN,
    STMT_BLOCK,
    STMT_EXPR,
    STMT_FUNC /* définition de fonction (niveau global) */
} StmtKind;

typedef struct Stmt Stmt;

typedef struct
{
    char *name;
    int len;
    Expr *value; /* peut être NULL : let x; */
} LetStmt;

typedef struct
{
    Expr *condition;
    Stmt *then_branch;
    Stmt *else_branch; /* peut être NULL */
} IfStmt;

typedef struct
{
    Expr *condition;
    Stmt *body;
} WhileStmt;

typedef struct FuncDecl
{
    char *name;
    int len;
    char **params;
    int *param_lens;
    int nparams;
    Stmt *body;
} FuncDecl;

struct Stmt
{
    StmtKind kind;
    int line;
    union
    {
        LetStmt let;
        Expr *print_expr;
        IfStmt if_stmt;
        WhileStmt while_stmt;
        Expr *return_expr; /* NULL = return; */
        Stmt *block_first; /* liste chaînée via next */
        Expr *expr;
        FuncDecl func;
    } as;
    Stmt *next; /* instruction suivante dans un bloc / le programme */
};

#endif /* MLANG_AST_H */
