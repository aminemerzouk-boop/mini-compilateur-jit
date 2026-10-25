/*
 * vm.h — Machine virtuelle à pile + profiler (détection des points chauds)
 * -----------------------------------------------------------------------
 * TÂCHE TOI (runtime)
 */
#ifndef MLANG_VM_H
#define MLANG_VM_H

#include <stdint.h>
#include "bytecode.h"

#define MLANG_MAX_FRAMES 1024
#define MLANG_SCRATCH    256
#define MLANG_MAX_LOOPS  128

typedef struct Frame
{
    int fn;
    int ip;
    int base;
} Frame;

typedef struct VMArenaBlock VMArenaBlock;

typedef struct {
    int fn;              /* fonction contenant la boucle */
    int target;          /* adresse bytecode de l'en-tete de boucle */
    uint64_t count;      /* nombre de sauts arriere vers target */
    int osr_done;        /* OSR deja tente pour cette boucle */
} LoopProfile;

typedef struct VM
{
    BytecodeProgram *prog;

    Value stack[MLANG_STACK_SIZE];
    int sp;

    Frame frames[MLANG_MAX_FRAMES];
    int nframes;

    Value globals[MLANG_MAX_GLOBALS];
    VMArenaBlock *string_arena; /* chaines creees pendant l'execution */

    /* --- profiler --- */
    uint64_t call_count[MLANG_MAX_FUNCS];
    uint64_t loop_iterations; /* sauts arriere (boucles) */

    LoopProfile loops[MLANG_MAX_LOOPS];
    int nloops;
    uint64_t osr_threshold;   /* iterations avant OSR (0 = desactive) */

    /* --- JIT --- */
    void *native[MLANG_MAX_FUNCS];      /* code machine compilé        */
    void *jit_results[MLANG_MAX_FUNCS]; /* gcc_jit_result à garder     */
    int jit_failed[MLANG_MAX_FUNCS];    /* échec : on ne réessaie pas  */
    double scratch[MLANG_SCRATCH];      /* zone d'arguments pour le JIT */
    int jit_enabled;
    int jit_threshold;
    int jit_verbose; /* affiche les messages [jit] */

    /* --- trace + erreurs d'exécution --- */
    int trace;
    int has_error;
    int error_line;
    char error_msg[128];

    int jit_compiled_count;
    double jit_compile_time_ms;
} VM;

void   vm_init(VM *vm, BytecodeProgram *prog);
Value  vm_invoke(VM *vm, int fnidx, Value *args, int nargs);      /* API runtime */
double vm_invoke_num(VM *vm, int fnidx, double *args, int nargs); /* pont JIT numérique */
void   vm_print_number(double v);                                  /* API runtime */
int    vm_run(VM *vm);
void   vm_free(VM *vm);

#endif /* MLANG_VM_H */
