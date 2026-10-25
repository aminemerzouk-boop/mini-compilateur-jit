/*
 * jit.h — Compilation à la volée (bytecode → code machine natif)
 * -------------------------------------------------------------
 * TÂCHE TOI (cœur du projet)
 */
#ifndef MLANG_JIT_H
#define MLANG_JIT_H

#include "vm.h"

/* Compile la fonction fnidx avec libgccjit.
 * Retourne 0 si OK, -1 en cas d'échec (le programme continue alors
 * en mode interprété : le JIT n'est jamais bloquant). */
int  jit_compile_function(VM *vm, int fnidx);

/* Libère les résultats de compilation (mémoire des blocs machine). */
void jit_release_all(VM *vm);

#endif /* MLANG_JIT_H */
