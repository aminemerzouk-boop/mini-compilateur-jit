/*
 * compiler.h — AST → bytecode
 * TÂCHE AMINE (front-end)
 */
#ifndef MLANG_COMPILER_H
#define MLANG_COMPILER_H

#include "ast.h"
#include "bytecode.h"

/* Retourne 0 si OK, -1 en cas d'erreur de compilation. */
int compile_program(Stmt *program, BytecodeProgram *out);

#endif /* MLANG_COMPILER_H */
