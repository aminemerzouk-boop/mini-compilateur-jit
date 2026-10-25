/*
 * jit.c — Génération de code natif à partir du bytecode (libgccjit)
 * ------------------------------------------------------------------
 * TÂCHE TOI (cœur du projet)
 *
 * Principe :
 *   1. on crée un contexte libgccjit ;
 *   2. on déclare une fonction C : double jit_fn(VM *vm, double *args, int nargs)
 *   3. on traduit chaque opcode en instructions IR (une pile de valeurs
 *      "SSA" est maintenue à la compilation) ;
 *   4. libgccjit compile en code machine, on récupère le pointeur ;
 *   5. la VM remplace la version interprétée par ce pointeur.
 *
 * Les appels internes passent par vm_invoke_num() : le JIT reste ainsi
 * compatible avec les fonctions pas encore compilées.
 */
#include "jit.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libgccjit.h>

#define JIT_STACK_MAX 512

typedef struct {
    gcc_jit_context  *ctx;
    gcc_jit_function *fn;
    gcc_jit_type     *dbl;
    gcc_jit_type     *dbl_ptr;
    gcc_jit_type     *void_ptr;

    gcc_jit_rvalue   *vm_ptr;        /* &vm, gravé dans le code    */
    gcc_jit_rvalue   *scratch_ptr;   /* vm->scratch                */
    gcc_jit_rvalue   *globals_ptr;   /* vm->globals                */

    gcc_jit_lvalue   *locals[MLANG_MAX_LOCALS];

    gcc_jit_block    *blocks[JIT_STACK_MAX];
    char              blocks_created[JIT_STACK_MAX];

    gcc_jit_rvalue   *stack[JIT_STACK_MAX];
    int               sp;

    gcc_jit_block    *cur;
    int               cur_open;

    int               has_error;
    char              error_msg[256];
} JitCtx;

static void jit_error(JitCtx *j, const char *msg)
{
    if (!j->has_error) {
        j->has_error = 1;
        snprintf(j->error_msg, sizeof(j->error_msg), "%s", msg);
    }
}

static void push(JitCtx *j, gcc_jit_rvalue *v)
{
    if (j->sp >= JIT_STACK_MAX) { jit_error(j, "pile du JIT debordee"); return; }
    j->stack[j->sp++] = v;
}

static gcc_jit_rvalue *pop(JitCtx *j)
{
    if (j->sp <= 0) { jit_error(j, "pile du JIT vide"); return NULL; }
    return j->stack[--j->sp];
}

static gcc_jit_rvalue *pop2(JitCtx *j, gcc_jit_rvalue **first)
{
    gcc_jit_rvalue *b = pop(j);
    gcc_jit_rvalue *a = pop(j);
    *first = a;
    return b;
}

static gcc_jit_rvalue *num(JitCtx *j, double d)
{
    return gcc_jit_context_new_rvalue_from_double(j->ctx, j->dbl, d);
}

static gcc_jit_rvalue *int_rv(JitCtx *j, int i)
{
    return gcc_jit_context_new_rvalue_from_int(j->ctx, gcc_jit_context_get_type(j->ctx, GCC_JIT_TYPE_INT), i);
}

/* renvoie (ou crée) le bloc correspondant à l'adresse ip */
static gcc_jit_block *block_at(JitCtx *j, int ip)
{
    if (ip < 0 || ip >= JIT_STACK_MAX) { jit_error(j, "adresse de saut invalide"); return NULL; }
    if (!j->blocks_created[ip]) {
        char name[32];
        snprintf(name, sizeof(name), "b%d", ip);
        j->blocks[ip] = gcc_jit_function_new_block(j->fn, name);
        j->blocks_created[ip] = 1;
    }
    return j->blocks[ip];
}


/* le résultat d'une comparaison libgccjit est de type "bool" :
 * on le convertit en double en passant par int (bool -> int -> double). */
static gcc_jit_rvalue *bool_to_double(JitCtx *j, gcc_jit_rvalue *cmp)
{
    gcc_jit_type *it = gcc_jit_context_get_type(j->ctx, GCC_JIT_TYPE_INT);
    gcc_jit_rvalue *as_int = gcc_jit_context_new_cast(j->ctx, NULL, cmp, it);
    return gcc_jit_context_new_cast(j->ctx, NULL, as_int, j->dbl);
}


/* Matérialise une valeur dans une variable temporaire du bloc courant.
 * INDISPENSABLE : libgccjit traite les rvalues comme des expressions
 * "pures", évaluées seulement au point d'utilisation. Sans ce passage
 * par une variable, deux appels qui partagent le tampon d'arguments
 * seraient évalués après que le 2e a écrasé le 1er (bug classique). */
static gcc_jit_rvalue *store_temp(JitCtx *j, int ip, gcc_jit_rvalue *v)
{
    char name[16];
    snprintf(name, sizeof(name), "t%d", ip);
    gcc_jit_lvalue *t = gcc_jit_function_new_local(j->fn, NULL, j->dbl, name);
    gcc_jit_block_add_assignment(j->cur, NULL, t, v);
    return gcc_jit_lvalue_as_rvalue(t);
}

/* =================== traduction d'une instruction =================== */
static void gen_instruction(JitCtx *j, const BytecodeProgram *prog,
                            const Function *fn, const Instr *in, int ip)
{
    switch (in->op) {

        case OP_CONST:
            push(j, store_temp(j, ip, num(j, as_num(prog->consts[in->a]))));
            break;

        case OP_NIL:
            push(j, store_temp(j, ip, num(j, 0.0)));
            break;

        case OP_GET_LOCAL:
            push(j, store_temp(j, ip, gcc_jit_lvalue_as_rvalue(j->locals[in->a])));
            break;

        case OP_SET_LOCAL: {
            gcc_jit_rvalue *v = j->stack[j->sp - 1];      /* peek */
            gcc_jit_block_add_assignment(j->cur, NULL, j->locals[in->a], v);
            break;
        }

        case OP_GET_GLOBAL: {
            gcc_jit_lvalue *slot = gcc_jit_context_new_array_access(
                j->ctx, NULL, j->globals_ptr, int_rv(j, in->a));
            push(j, store_temp(j, ip, gcc_jit_lvalue_as_rvalue(slot)));
            break;
        }

        case OP_SET_GLOBAL: {
            gcc_jit_rvalue *v = j->stack[j->sp - 1];
            gcc_jit_lvalue *slot = gcc_jit_context_new_array_access(
                j->ctx, NULL, j->globals_ptr, int_rv(j, in->a));
            gcc_jit_block_add_assignment(j->cur, NULL, slot, v);
            break;
        }

        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
            gcc_jit_rvalue *a;
            gcc_jit_rvalue *b = pop2(j, &a);
            enum gcc_jit_binary_op op =
                in->op == OP_ADD ? GCC_JIT_BINARY_OP_PLUS   :
                in->op == OP_SUB ? GCC_JIT_BINARY_OP_MINUS  :
                in->op == OP_MUL ? GCC_JIT_BINARY_OP_MULT   :
                in->op == OP_DIV ? GCC_JIT_BINARY_OP_DIVIDE :
                                   GCC_JIT_BINARY_OP_MODULO;
            push(j, store_temp(j, ip, gcc_jit_context_new_binary_op(j->ctx, NULL, op, j->dbl, a, b)));
            break;
        }

        case OP_NEG: {
            gcc_jit_rvalue *a = pop(j);
            push(j, store_temp(j, ip, gcc_jit_context_new_unary_op(j->ctx, NULL,
                     GCC_JIT_UNARY_OP_MINUS, j->dbl, a)));
            break;
        }

        case OP_NOT: {
            gcc_jit_rvalue *a = pop(j);
            gcc_jit_rvalue *cmp = gcc_jit_context_new_comparison(j->ctx, NULL,
                    GCC_JIT_COMPARISON_EQ, a, num(j, 0.0));
            push(j, store_temp(j, ip, bool_to_double(j, cmp)));
            break;
        }

        case OP_EQ: case OP_NEQ: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            gcc_jit_rvalue *a;
            gcc_jit_rvalue *b = pop2(j, &a);
            enum gcc_jit_comparison op =
                in->op == OP_EQ  ? GCC_JIT_COMPARISON_EQ :
                in->op == OP_NEQ ? GCC_JIT_COMPARISON_NE :
                in->op == OP_LT  ? GCC_JIT_COMPARISON_LT :
                in->op == OP_LE  ? GCC_JIT_COMPARISON_LE :
                in->op == OP_GT  ? GCC_JIT_COMPARISON_GT :
                                   GCC_JIT_COMPARISON_GE;
            gcc_jit_rvalue *cmp = gcc_jit_context_new_comparison(j->ctx, NULL, op, a, b);
            push(j, store_temp(j, ip, bool_to_double(j, cmp)));
            break;
        }

        case OP_JUMP: {
            gcc_jit_block *target = block_at(j, in->a);
            if (!target) { jit_error(j, "cible de saut invalide"); break; }
            gcc_jit_block_end_with_jump(j->cur, NULL, target);
            j->cur_open = 0;
            break;
        }

        case OP_JUMP_IF_FALSE: {
            gcc_jit_rvalue *cond = pop(j);
            gcc_jit_block *target = block_at(j, in->a);
            gcc_jit_block *fall   = block_at(j, ip + 1);
            if (!target || !fall) { jit_error(j, "cible de saut invalide"); break; }
            gcc_jit_rvalue *truthy = gcc_jit_context_new_comparison(j->ctx, NULL,
                    GCC_JIT_COMPARISON_NE, cond, num(j, 0.0));
            gcc_jit_block_end_with_conditional(j->cur, NULL, truthy, fall, target);
            j->cur_open = 0;
            break;
        }

        case OP_CALL: {
            int nargs = in->b;
            if (nargs > 16) { jit_error(j, "trop d'arguments (max 16)"); break; }

            gcc_jit_rvalue *argv[16];
            for (int k = nargs - 1; k >= 0; k--) argv[k] = pop(j);

            /* on pose les arguments dans vm->scratch */
            for (int k = 0; k < nargs; k++) {
                gcc_jit_lvalue *slot = gcc_jit_context_new_array_access(
                    j->ctx, NULL, j->scratch_ptr, int_rv(j, k));
                gcc_jit_block_add_assignment(j->cur, NULL, slot, argv[k]);
            }

            /* appel : double vm_invoke_num(VM*, int, double*, int) */
            gcc_jit_type *params[4] = { j->void_ptr, j->ctx ? gcc_jit_context_get_type(j->ctx, GCC_JIT_TYPE_INT) : NULL,
                                        j->dbl_ptr, gcc_jit_context_get_type(j->ctx, GCC_JIT_TYPE_INT) };
            gcc_jit_type *invoke_t = gcc_jit_context_new_function_ptr_type(
                j->ctx, NULL, j->dbl, 4, params, 0);

            gcc_jit_rvalue *invoke_ptr =
                gcc_jit_context_new_rvalue_from_ptr(j->ctx, invoke_t, (void *)vm_invoke_num);

            gcc_jit_rvalue *args4[4] = { j->vm_ptr, int_rv(j, in->a), j->scratch_ptr, int_rv(j, nargs) };

            push(j, store_temp(j, ip, gcc_jit_context_new_call_through_ptr(j->ctx, NULL,
                        invoke_ptr, 4, args4)));
            break;
        }

        case OP_PRINT: {
            gcc_jit_rvalue *v = pop(j);
            gcc_jit_type *params1[1] = { j->dbl };
            gcc_jit_type *print_t = gcc_jit_context_new_function_ptr_type(
                j->ctx, NULL, gcc_jit_context_get_type(j->ctx, GCC_JIT_TYPE_VOID),
                1, params1, 0);
            gcc_jit_rvalue *print_ptr =
                gcc_jit_context_new_rvalue_from_ptr(j->ctx, print_t, (void *)vm_print_number);
            gcc_jit_rvalue *args1[1] = { v };
            gcc_jit_block_add_eval(j->cur, NULL,
                gcc_jit_context_new_call_through_ptr(j->ctx, NULL, print_ptr, 1, args1));
            break;
        }

        case OP_RETURN: {
            gcc_jit_rvalue *v = pop(j);
            gcc_jit_block_end_with_return(j->cur, NULL, v ? v : num(j, 0.0));
            j->cur_open = 0;
            break;
        }

        case OP_POP:
            (void)pop(j);
            break;

        case OP_HALT:
            gcc_jit_block_end_with_return(j->cur, NULL, num(j, 0.0));
            j->cur_open = 0;
            break;

        default:
            (void)fn;
            jit_error(j, "opcode inconnu");
            break;
    }
}
/* =================== éligibilité JIT (stratégie A) =================== */
/* Le code natif actuel ne sait transporter que des double. On ne compile
 * donc que les fonctions prouvées "numériques seulement" ; les autres
 * restent interprétées. En cas de doute, on refuse. */

static int compute_reachable(const Function *fn, char *reach)
{
    int n = fn->chunk.count;
    if (n <= 0 || n > JIT_STACK_MAX) return 0;

    memset(reach, 0, (size_t)JIT_STACK_MAX);

    int queue[JIT_STACK_MAX];
    int qh = 0, qt = 0;
    reach[0] = 1;
    queue[qt++] = 0;

    while (qh < qt) {
        int ip = queue[qh++];
        const Instr *in = &fn->chunk.code[ip];
        int succ[2];
        int nsucc = 0;

        switch (in->op) {
            case OP_JUMP:          succ[nsucc++] = in->a; break;
            case OP_JUMP_IF_FALSE: succ[nsucc++] = in->a; succ[nsucc++] = ip + 1; break;
            case OP_RETURN: case OP_HALT: break;
            default:               succ[nsucc++] = ip + 1; break;
        }

        for (int k = 0; k < nsucc; k++) {
            int t = succ[k];
            if (t >= 0 && t < n && !reach[t]) {
                reach[t] = 1;
                queue[qt++] = t;
            }
        }
    }
    return 1;
}

static int jit_function_is_numeric_local(const VM *vm, int fnidx)
{
    const BytecodeProgram *prog = vm->prog;
    const Function *fn = &prog->funcs[fnidx];
    char reach[JIT_STACK_MAX];

    if (!compute_reachable(fn, reach)) return 0;

    for (int ip = 0; ip < fn->chunk.count; ip++) {
        if (!reach[ip]) continue;          /* code mort ignoré */
        const Instr *in = &fn->chunk.code[ip];

        switch (in->op) {
            case OP_CONST:
                if (in->a < 0 || in->a >= prog->nconsts ||
                    !is_num(prog->consts[in->a]))
                    return 0;
                break;

            case OP_NIL:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_PRINT:
            case OP_DIV:                    /* pas encore de garde zéro en natif */
            case OP_MOD:
            case OP_NOT:                    /* produirait un booléen */
                return 0;

            case OP_EQ: case OP_NEQ:
            case OP_LT: case OP_LE:
            case OP_GT: case OP_GE:
                /* Booléen accepté uniquement s'il est immédiatement
                 * consommé comme condition d'un saut. */
                if (ip + 1 >= fn->chunk.count ||
                    fn->chunk.code[ip + 1].op != OP_JUMP_IF_FALSE)
                    return 0;
                break;

            case OP_CALL:
                if (in->a < 0 || in->a >= prog->nfuncs ||
                    in->b < 0 || in->b > 16)
                    return 0;
                break;

            case OP_GET_LOCAL: case OP_SET_LOCAL:
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_NEG:
            case OP_JUMP: case OP_JUMP_IF_FALSE:
            case OP_RETURN: case OP_POP:
                break;

            default:
                return 0;
        }
    }
    return 1;
}

static int jit_function_is_numeric(const VM *vm, int fnidx)
{
    int nf = vm->prog->nfuncs;
    if (fnidx < 0 || fnidx >= nf) return 0;

    char eligible[MLANG_MAX_FUNCS];
    for (int i = 0; i < nf; i++)
        eligible[i] = (char)jit_function_is_numeric_local(vm, i);

    /* Une fonction qui appelle une fonction non éligible devient
     * elle-même non éligible. On itère jusqu'à stabilisation ;
     * la récursionsurvit car on part des fonctions localement OK. */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < nf; i++) {
            if (!eligible[i]) continue;
            const Function *fn = &vm->prog->funcs[i];
            for (int ip = 0; ip < fn->chunk.count; ip++) {
                const Instr *in = &fn->chunk.code[ip];
                if (in->op != OP_CALL) continue;
                if (in->a < 0 || in->a >= nf || !eligible[in->a]) {
                    eligible[i] = 0;
                    changed = 1;
                    break;
                }
            }
        }
    }
    return eligible[fnidx];
}
/* ============================ entrée ============================ */
int jit_compile_function(VM *vm, int fnidx)
{
    const Function *fn = &vm->prog->funcs[fnidx];
    struct timespec t0, t1;
    if (vm->native[fnidx]) return 0;

    if (!jit_function_is_numeric(vm, fnidx)) {
        if (vm->jit_verbose)
            fprintf(stderr, "[jit] %-8s non eligible (types dynamiques) : interpretee\n", fn->name);
        vm->jit_failed[fnidx] = 1;   /* on ne réessaiera pas */
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);

    JitCtx j;
    memset(&j, 0, sizeof(j));

    j.ctx = gcc_jit_context_acquire();
#ifdef GCC_JIT_OPTIMIZATION_LEVEL_2
    gcc_jit_context_set_optimization_level(j.ctx, GCC_JIT_OPTIMIZATION_LEVEL_2);
#endif

    j.dbl      = gcc_jit_context_get_type(j.ctx, GCC_JIT_TYPE_DOUBLE);
    j.dbl_ptr  = gcc_jit_type_get_pointer(j.dbl);
    j.void_ptr = gcc_jit_context_get_type(j.ctx, GCC_JIT_TYPE_VOID_PTR);

    gcc_jit_param *p_vm  = gcc_jit_context_new_param(j.ctx, NULL, j.void_ptr, "vm");
    gcc_jit_param *p_arg = gcc_jit_context_new_param(j.ctx, NULL, j.dbl_ptr, "args");
    gcc_jit_param *p_n   = gcc_jit_context_new_param(j.ctx, NULL,
                              gcc_jit_context_get_type(j.ctx, GCC_JIT_TYPE_INT), "nargs");
    gcc_jit_param *params[3] = { p_vm, p_arg, p_n };

    char fname[128];
    snprintf(fname, sizeof(fname), "jit_%s_%d", fn->name, fnidx);

    j.fn = gcc_jit_context_new_function(j.ctx, NULL, GCC_JIT_FUNCTION_EXPORTED,
                                        j.dbl, fname, 3, params, 0);

    j.vm_ptr      = gcc_jit_param_as_rvalue(p_vm);
    j.scratch_ptr = gcc_jit_context_new_rvalue_from_ptr(j.ctx, j.dbl_ptr, (void *)vm->scratch);
    j.globals_ptr = gcc_jit_context_new_rvalue_from_ptr(j.ctx, j.dbl_ptr, (void *)vm->globals);

    /* --- slots locaux --- */
    int nlocals = fn->nlocals > 0 ? fn->nlocals : 1;
    for (int i = 0; i < nlocals; i++) {
        char lname[16];
        snprintf(lname, sizeof(lname), "v%d", i);
        j.locals[i] = gcc_jit_function_new_local(j.fn, NULL, j.dbl, lname);
    }

    gcc_jit_block *entry = gcc_jit_function_new_block(j.fn, "entry");
    j.blocks[0]        = entry;
    j.blocks_created[0] = 1;
    j.cur      = entry;
    j.cur_open = 1;

    /* prologue : copier les arguments dans les slots, zéro ailleurs */
    for (int i = 0; i < nlocals; i++) {
        gcc_jit_rvalue *v;
        if (i < fn->arity) {
            gcc_jit_lvalue *arg_i = gcc_jit_context_new_array_access(
                j.ctx, NULL, gcc_jit_param_as_rvalue(p_arg), int_rv(&j, i));
            v = gcc_jit_lvalue_as_rvalue(arg_i);
        } else {
            v = num(&j, 0.0);
        }
        gcc_jit_block_add_assignment(entry, NULL, j.locals[i], v);
    }

    /* --- analyse du flot de contrôle : quelles instructions sont
           joignables, et où commencent les blocs (cibles de saut) ? --- */
    int n = fn->chunk.count;
    char reach[JIT_STACK_MAX];   memset(reach,  0, (size_t)n + 1);
    char starts[JIT_STACK_MAX];  memset(starts, 0, (size_t)n + 1);
    int queue[JIT_STACK_MAX];
    int qh = 0, qt = 0;

    if (n > 0) { reach[0] = 1; queue[qt++] = 0; }

    while (qh < qt) {
        int ip = queue[qh++];
        const Instr *in = &fn->chunk.code[ip];
        int succ[2]; int nsucc = 0;
        switch (in->op) {
            case OP_JUMP:          succ[nsucc++] = in->a; break;
            case OP_JUMP_IF_FALSE: succ[nsucc++] = in->a; succ[nsucc++] = ip + 1; break;
            case OP_RETURN: case OP_HALT: break;
            default:               succ[nsucc++] = ip + 1; break;
        }
        for (int k = 0; k < nsucc; k++) {
            int t = succ[k];
            if (t >= 0 && t < n && !reach[t] && qt < JIT_STACK_MAX) {
                reach[t] = 1;
                queue[qt++] = t;
            }
        }
    }

    starts[0] = 1;
    for (int ip = 0; ip < n; ip++) {
        if (!reach[ip]) continue;
        const Instr *in = &fn->chunk.code[ip];
        switch (in->op) {
            case OP_JUMP:
                if (in->a >= 0 && in->a < n) starts[in->a] = 1;
                break;
            case OP_JUMP_IF_FALSE:
                if (in->a >= 0 && in->a < n) starts[in->a] = 1;
                if (ip + 1 < n) starts[ip + 1] = 1;
                break;
            case OP_RETURN: case OP_HALT: break;
            default:
                if (ip + 1 < n) starts[ip + 1] = 1;
                break;
        }
    }

    /* --- traduction des instructions (un bloc par point d'entrée) --- */
    for (int ip = 0; ip < n && !j.has_error; ip++) {
        if (!reach[ip] || !starts[ip]) continue;      /* code mort : ignoré */

        if (ip == 0) {
            j.cur      = entry;
            j.cur_open = 1;
        } else {
            gcc_jit_block *target = block_at(&j, ip);
            if (!target) break;
            if (j.cur_open && j.cur)      /* chute dans une étiquette */
                gcc_jit_block_end_with_jump(j.cur, NULL, target);
            j.cur      = target;
            j.cur_open = 1;
        }
        gen_instruction(&j, vm->prog, fn, &fn->chunk.code[ip], ip);
    }

    /* bloc final éventuellement ouvert mais non terminé */
    if (j.cur && j.cur_open && !j.has_error)
        gcc_jit_block_end_with_return(j.cur, NULL, num(&j, 0.0));

    if (j.has_error) {
        fprintf(stderr, "[jit] erreur de generation pour '%s' : %s\n", fn->name, j.error_msg);
        gcc_jit_context_release(j.ctx);
        vm->jit_failed[fnidx] = 1;
        return -1;
    }

    if (getenv("MLANG_DUMP_JIT")) {
        char dump[256];
        snprintf(dump, sizeof(dump), "/tmp/jit_%s.c", fn->name);
        gcc_jit_context_dump_to_file(j.ctx, dump, 1);
        fprintf(stderr, "[jit] IR ecrit dans %s\n", dump);
    }

    gcc_jit_result *res = gcc_jit_context_compile(j.ctx);
    if (!res) {
        const char *err = gcc_jit_context_get_first_error(j.ctx);
        fprintf(stderr, "[jit] echec de compilation de '%s' : %s\n",
                fn->name, err ? err : "(inconnu)");
        gcc_jit_context_release(j.ctx);
        vm->jit_failed[fnidx] = 1;
        return -1;
    }

    void *code = gcc_jit_result_get_code(res, fname);
    gcc_jit_context_release(j.ctx);   /* le 'result' garde le code vivant */

    if (!code) {
        fprintf(stderr, "[jit] code introuvable pour '%s'\n", fn->name);
        gcc_jit_result_release(res);
        vm->jit_failed[fnidx] = 1;
        return -1;
    }

    vm->native[fnidx]      = code;
    vm->jit_results[fnidx] = res;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    vm->jit_compiled_count++;
    vm->jit_compile_time_ms += ms;

    if (vm->jit_verbose) {
        fprintf(stderr, "[jit] %-8s compilee en natif apres %d appels (%.2f ms)\n",
                fn->name, vm->jit_threshold, ms);
    }
    return 0;
}

void jit_release_all(VM *vm)
{
    for (int i = 0; i < vm->prog->nfuncs; i++) {
        if (vm->jit_results[i]) {
            gcc_jit_result_release((gcc_jit_result *)vm->jit_results[i]);
            vm->jit_results[i] = NULL;
            vm->native[i]      = NULL;
        }
    }
}
