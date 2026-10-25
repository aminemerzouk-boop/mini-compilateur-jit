/*
 * vm.c — Machine virtuelle à pile, interpréteur + profiler
 * TÂCHE TOI (runtime)
 */
#include "vm.h"
#include "jit.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void vm_init(VM *vm, BytecodeProgram *prog)
{
    memset(vm, 0, sizeof(*vm));
    vm->prog = prog;
    vm->jit_enabled = 1;
    vm->jit_threshold = 100; /* nb d'appels avant compilation */
    vm->osr_threshold = 1000;
    vm->jit_verbose = 1;
}

void vm_print_number(double v)
{
    print_value(value_num(v));
    printf("\n");
}

static void set_runtime_error(VM *vm, int line, const char *msg)
{
    if (vm->has_error)
        return; /* garder la premiere erreur */
    vm->has_error = 1;
    vm->error_line = line;
    snprintf(vm->error_msg, sizeof(vm->error_msg), "%s", msg);
}

/* ---------------------------------------------------------------- */
/*  Arene des chaines creees pendant l'execution (concatenation)      */
/* ---------------------------------------------------------------- */
struct VMArenaBlock
{
    VMArenaBlock *next;
    size_t used;
    size_t cap;
    char data[];
};

static void *vm_arena_alloc(VM *vm, size_t size)
{
    size = (size + 15) & ~(size_t)15; /* alignement 16 octets */
    if (!vm->string_arena || vm->string_arena->used + size > vm->string_arena->cap)
    {
        size_t cap = size > 4096 ? size : 4096;
        VMArenaBlock *b = malloc(sizeof(VMArenaBlock) + cap);
        if (!b)
            return NULL;
        b->next = vm->string_arena;
        b->used = 0;
        b->cap = cap;
        vm->string_arena = b;
    }
    void *p = vm->string_arena->data + vm->string_arena->used;
    vm->string_arena->used += size;
    memset(p, 0, size);
    return p;
}

static ObjStr *vm_concat_strings(VM *vm, ObjStr *a, ObjStr *b)
{
    if (!a || !b || !a->chars || !b->chars)
        return NULL;

    ObjStr *r = vm_arena_alloc(vm, sizeof(ObjStr));
    if (!r)
        return NULL;

    int len = a->length + b->length;
    char *chars = vm_arena_alloc(vm, (size_t)len + 1);
    if (!chars)
        return NULL;

    memcpy(chars, a->chars, (size_t)a->length);
    memcpy(chars + a->length, b->chars, (size_t)b->length);
    chars[len] = '\0';

    r->chars = chars;
    r->length = len;
    return r;
}

static void vm_free_string_arena(VM *vm)
{
    VMArenaBlock *b = vm->string_arena;
    while (b)
    {
        VMArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    vm->string_arena = NULL;
}

/* ---------------------------------------------------------------- */
/*  Verifications de types pour les operateurs numeriques             */
/* ---------------------------------------------------------------- */
static bool require_numbers(VM *vm, int line, const char *op, Value a, Value b)
{
    if (is_num(a) && is_num(b))
        return true;

    char msg[128];
    snprintf(msg, sizeof(msg), "types incompatibles pour %s : %s et %s",
             op, value_type_name(a), value_type_name(b));
    set_runtime_error(vm, line, msg);
    return false;
}

static bool require_number(VM *vm, int line, const char *op, Value a)
{
    if (is_num(a))
        return true;

    char msg[128];
    snprintf(msg, sizeof(msg), "type incompatible pour %s : %s",
             op, value_type_name(a));
    set_runtime_error(vm, line, msg);
    return false;
}

/* ---------------------------------------------------------------- */
/*  Profiler de boucles : un compteur par (fonction, cible)           */
/* ---------------------------------------------------------------- */
static LoopProfile *loop_profile_for(VM *vm, int fnidx, int target)
{
    for (int i = 0; i < vm->nloops; i++)
        if (vm->loops[i].fn == fnidx && vm->loops[i].target == target)
            return &vm->loops[i];

    if (vm->nloops >= MLANG_MAX_LOOPS)
        return NULL;

    LoopProfile *lp = &vm->loops[vm->nloops++];
    lp->fn = fnidx;
    lp->target = target;
    lp->count = 0;
    lp->osr_done = 0;
    return lp;
}

static LoopProfile *count_backedge(VM *vm, int fnidx, int target)
{
    vm->loop_iterations++;
    LoopProfile *lp = loop_profile_for(vm, fnidx, target);
    if (lp)
        lp->count++;
    return lp;
}

static int osr_try_transfer(VM *vm, int fnidx, LoopProfile *lp,
                            Value *args, int nargs, int base, Value *out)
{
    if (!lp || lp->osr_done || !vm->jit_enabled || vm->osr_threshold == 0 ||
        lp->count < vm->osr_threshold || fnidx == vm->prog->script_func ||
        vm->jit_failed[fnidx])
        return 0;

    for (int i = 0; i < nargs; i++)
        if (!args || !is_num(args[i]))
            return 0;

    lp->osr_done = 1;

    if (!vm->native[fnidx]) {
        int old_verbose = vm->jit_verbose;
        vm->jit_verbose = 0;
        int rc = jit_compile_function(vm, fnidx);
        vm->jit_verbose = old_verbose;

        if (rc != 0 || !vm->native[fnidx])
            return 0;

        if (old_verbose) {
            fprintf(stderr, "[jit] %-8s compilee (OSR) apres %llu iterations\n",
                    vm->prog->funcs[fnidx].name,
                    (unsigned long long)lp->count);
        }
    }

    /* OSR simplifie : on relance la fonction entiere en natif
     * avec ses arguments d'origine, puis on abandonne le cadre interprete. */
    vm->sp = base + vm->prog->funcs[fnidx].nlocals;
    *out = vm_invoke(vm, fnidx, args, nargs);
    return 1;
}

/* ---------------------------------------------------------------- */
/*  Interprétation d'une fonction (pile d'appels, slots locaux)       */
/* ---------------------------------------------------------------- */
static Value interpret(VM *vm, int fnidx, Value *args, int nargs)
{
    Function *fn = &vm->prog->funcs[fnidx];
    int base = vm->sp;

    if (base + fn->nlocals + 16 >= MLANG_STACK_SIZE)
    {
        fprintf(stderr, "erreur : debordement de pile (stack overflow)\n");
        return value_nil();
    }

    for (int i = 0; i < fn->nlocals; i++)
        vm->stack[base + i] = (i < nargs) ? args[i] : value_nil();
    vm->sp = base + fn->nlocals;

    int ip = 0;
    Chunk *ch = &fn->chunk;

    for (;;)
    {
        if (ip < 0 || ip >= ch->count)
        {
            fprintf(stderr, "erreur : ip hors limites dans %s\n", fn->name);
            vm->sp = base;
            return value_nil();
        }

        Instr in = ch->code[ip++];

        if (vm->trace)
        {
            int cur_ip = ip - 1; /* ip a deja ete incremente par le fetch */
            fprintf(stderr, "[trace] %-10s %04d %-14s a=%d b=%d sp=%d",
                    fn->name, cur_ip, opcode_name(in.op), in.a, in.b, vm->sp);
            if (vm->sp > 0)
                fprintf(stderr, " top=%g",
                        is_num(vm->stack[vm->sp - 1]) ? as_num(vm->stack[vm->sp - 1]) : 0.0);
            fprintf(stderr, "\n");
        }

        switch (in.op)
        {
        case OP_CONST:
            vm->stack[vm->sp++] = vm->prog->consts[in.a];
            break;
        case OP_NIL:
            vm->stack[vm->sp++] = value_nil();
            break;
        case OP_GET_LOCAL:
            vm->stack[vm->sp++] = vm->stack[base + in.a];
            break;
        case OP_SET_LOCAL:
            vm->stack[base + in.a] = vm->stack[vm->sp - 1];
            break;
        case OP_GET_GLOBAL:
            vm->stack[vm->sp++] = vm->globals[in.a];
            break;
        case OP_SET_GLOBAL:
            vm->globals[in.a] = vm->stack[vm->sp - 1];
            break;

        case OP_ADD:
        {
            Value b = vm->stack[--vm->sp];
            Value a = vm->stack[--vm->sp];

            if (is_num(a) && is_num(b))
            {
                vm->stack[vm->sp++] = value_num(as_num(a) + as_num(b));
            }
            else if (is_str(a) && is_str(b))
            {
                ObjStr *r = vm_concat_strings(vm, as_str(a), as_str(b));
                if (!r)
                {
                    set_runtime_error(vm, in.line, "memoire epuisee");
                    vm->sp = base;
                    return value_nil();
                }
                vm->stack[vm->sp++] = value_str(r);
            }
            else
            {
                char msg[128];
                snprintf(msg, sizeof(msg), "types incompatibles pour + : %s et %s",
                         value_type_name(a), value_type_name(b));
                set_runtime_error(vm, in.line, msg);
                vm->sp = base;
                return value_nil();
            }
            break;
        }

        case OP_SUB:
        {
            Value b = vm->stack[--vm->sp];
            Value a = vm->stack[--vm->sp];
            if (!require_numbers(vm, in.line, "-", a, b))
            {
                vm->sp = base;
                return value_nil();
            }
            vm->stack[vm->sp++] = value_num(as_num(a) - as_num(b));
            break;
        }

        case OP_MUL:
        {
            Value b = vm->stack[--vm->sp];
            Value a = vm->stack[--vm->sp];
            if (!require_numbers(vm, in.line, "*", a, b))
            {
                vm->sp = base;
                return value_nil();
            }
            vm->stack[vm->sp++] = value_num(as_num(a) * as_num(b));
            break;
        }

        case OP_DIV:
        {
            Value b = vm->stack[--vm->sp];
            Value a = vm->stack[--vm->sp];
            if (!require_numbers(vm, in.line, "/", a, b))
            {
                vm->sp = base;
                return value_nil();
            }
            if (as_num(b) == 0.0)
            {
                set_runtime_error(vm, in.line, "division par zero");
                vm->sp = base;
                return value_nil();
            }
            vm->stack[vm->sp++] = value_num(as_num(a) / as_num(b));
            break;
        }

        case OP_MOD:
        {
            Value b = vm->stack[--vm->sp];
            Value a = vm->stack[--vm->sp];
            if (!require_numbers(vm, in.line, "%", a, b))
            {
                vm->sp = base;
                return value_nil();
            }
            long long ib = (long long)as_num(b);
            if (ib == 0)
            {
                set_runtime_error(vm, in.line, "modulo par zero");
                vm->sp = base;
                return value_nil();
            }
            vm->stack[vm->sp++] = value_num((double)((long long)as_num(a) % ib));
            break;
        }

        case OP_NEG:
        {
            Value v = vm->stack[vm->sp - 1];
            if (!require_number(vm, in.line, "- unaire", v))
            {
                vm->sp = base;
                return value_nil();
            }
            vm->stack[vm->sp - 1] = value_num(-as_num(v));
            break;
        }

        case OP_NOT:
            vm->stack[vm->sp - 1] = value_bool(!is_truthy(vm->stack[vm->sp - 1]));
            break;

        case OP_EQ:
        case OP_NEQ:
        case OP_LT:
        case OP_LE:
        case OP_GT:
        case OP_GE:
        {
            Value b = vm->stack[--vm->sp];
            Value a = vm->stack[--vm->sp];
            int r = 0;

            if (in.op == OP_EQ || in.op == OP_NEQ)
            {
                r = values_equal(a, b);
                if (in.op == OP_NEQ)
                    r = !r;
            }
            else
            {
                if (!is_num(a) || !is_num(b))
                {
                    char msg[128];
                    snprintf(msg, sizeof(msg), "comparaison ordonnee impossible : %s et %s",
                             value_type_name(a), value_type_name(b));
                    set_runtime_error(vm, in.line, msg);
                    vm->sp = base;
                    return value_nil();
                }
                switch (in.op)
                {
                case OP_LT:
                    r = (as_num(a) < as_num(b));
                    break;
                case OP_LE:
                    r = (as_num(a) <= as_num(b));
                    break;
                case OP_GT:
                    r = (as_num(a) > as_num(b));
                    break;
                case OP_GE:
                    r = (as_num(a) >= as_num(b));
                    break;
                default:
                    break;
                }
            }

            vm->stack[vm->sp++] = value_bool(r);
            break;
        }

        case OP_JUMP:
            if (in.a < ip - 1) {
                LoopProfile *lp = count_backedge(vm, fnidx, in.a);
                Value out;
                if (osr_try_transfer(vm, fnidx, lp, args, nargs, base, &out)) {
                    vm->sp = base;
                    return out;
                }
            }
            ip = in.a;
            break;

        case OP_JUMP_IF_FALSE:
        {
            Value cond = vm->stack[--vm->sp];
            if (!is_truthy(cond))
            {
                if (in.a < ip - 1) {
                    LoopProfile *lp = count_backedge(vm, fnidx, in.a);
                    Value out;
                    if (osr_try_transfer(vm, fnidx, lp, args, nargs, base, &out)) {
                        vm->sp = base;
                        return out;
                    }
                }
                ip = in.a;
            }
            break;
        }

        case OP_CALL:
        {
            Value result = vm_invoke(vm, in.a, &vm->stack[vm->sp - in.b], in.b);
            if (vm->has_error)
            {
                vm->sp = base;
                return value_nil();
            }
            vm->sp -= in.b;
            vm->stack[vm->sp++] = result;
            break;
        }

        case OP_PRINT:
        {
            Value v = vm->stack[--vm->sp];
            print_value(v);
            printf("\n");
            break;
        }

        case OP_RETURN:
        {
            Value v = vm->stack[--vm->sp];
            vm->sp = base;
            return v;
        }

        case OP_POP:
            vm->sp--;
            break;

        case OP_HALT:
            vm->sp = base;
            return value_nil();
        }
    }
}

/* ---------------------------------------------------------------- */
/*  Point d'entrée unique : natif si compilé, sinon interprété        */
/*  C'est AUSSI la fonction que le code JIT appelle via le pont       */
/* ---------------------------------------------------------------- */
Value vm_invoke(VM *vm, int fnidx, Value *args, int nargs)
{
    if (fnidx < 0 || fnidx >= vm->prog->nfuncs)
        return value_nil();
    if (vm->has_error)
        return value_nil();

    /* --- profiler : la fonction est-elle « chaude » ? --- */
    if (vm->jit_enabled && fnidx != vm->prog->script_func &&
        !vm->native[fnidx] && !vm->jit_failed[fnidx])
    {
        vm->call_count[fnidx]++;
        if (vm->call_count[fnidx] >= (uint64_t)vm->jit_threshold)
        {
            if (jit_compile_function(vm, fnidx) == 0)
                vm->call_count[fnidx] = 0; /* évite de recompiler */
        }
    }

    /* --- version native ? (pont Value <-> double) --- */
    if (vm->native[fnidx] && nargs >= 0 && nargs <= MLANG_MAX_LOCALS)
    {
        double buf[MLANG_MAX_LOCALS];
        int all_num = 1;

        for (int i = 0; i < nargs; i++)
        {
            if (!args || !is_num(args[i]))
            {
                all_num = 0;
                break;
            }
            buf[i] = as_num(args[i]);
        }

        if (all_num)
        {
            double (*fn_ptr)(VM *, double *, int) =
                (double (*)(VM *, double *, int))vm->native[fnidx];
            double r = fn_ptr(vm, buf, nargs);
            if (vm->has_error)
                return value_nil();
            return value_num(r);
        }
        /* Au moins un argument non numérique :
         * dé-optimisation de cet appel vers l'interpréteur. */
    }

    return interpret(vm, fnidx, args, nargs);
}

/* Pont pour l'ancien ABI numérique du JIT :
 * double[] (natif) -> Value[] (VM actuelle) -> double (natif). */
double vm_invoke_num(VM *vm, int fnidx, double *args, int nargs)
{
    if (!vm)
        return 0.0;
    if (nargs < 0 || nargs > 16 || (nargs > 0 && !args))
    {
        set_runtime_error(vm, 0, "trop d'arguments pour le pont JIT");
        return 0.0;
    }

    Value buf[16];
    for (int i = 0; i < nargs; i++)
        buf[i] = value_num(args[i]);

    Value r = vm_invoke(vm, fnidx, buf, nargs);

    if (vm->has_error)
        return 0.0;
    if (!is_num(r))
    {
        set_runtime_error(vm, 0, "retour non numerique vers le code JIT");
        return 0.0;
    }
    return as_num(r);
}

int vm_run(VM *vm)
{
    Value r = vm_invoke(vm, vm->prog->script_func, NULL, 0);
    return vm->has_error ? 1 : (is_num(r) ? (int)as_num(r) : 0);
}

void vm_free(VM *vm)
{
    vm_free_string_arena(vm);
    jit_release_all(vm);
    if (vm->prog)
    {
        for (int i = 0; i < vm->prog->nfuncs; i++)
        {
            free(vm->prog->funcs[i].chunk.code);
            vm->prog->funcs[i].chunk.code = NULL;
        }
    }
}
