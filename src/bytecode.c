/*
 * bytecode.c — Désassembleur (outil de débogage)
 * TÂCHE AMINE (front-end) / utile aux deux
 */
#include "bytecode.h"

#include <stdio.h>

const char *opcode_name(uint8_t op)
{
    switch (op)
    {
    case OP_CONST:
        return "CONST";
    case OP_NIL:
        return "NIL";
    case OP_GET_LOCAL:
        return "GET_LOCAL";
    case OP_SET_LOCAL:
        return "SET_LOCAL";
    case OP_GET_GLOBAL:
        return "GET_GLOBAL";
    case OP_SET_GLOBAL:
        return "SET_GLOBAL";
    case OP_ADD:
        return "ADD";
    case OP_SUB:
        return "SUB";
    case OP_MUL:
        return "MUL";
    case OP_DIV:
        return "DIV";
    case OP_MOD:
        return "MOD";
    case OP_NEG:
        return "NEG";
    case OP_NOT:
        return "NOT";
    case OP_EQ:
        return "EQ";
    case OP_NEQ:
        return "NEQ";
    case OP_LT:
        return "LT";
    case OP_LE:
        return "LE";
    case OP_GT:
        return "GT";
    case OP_GE:
        return "GE";
    case OP_JUMP:
        return "JUMP";
    case OP_JUMP_IF_FALSE:
        return "JUMP_IF_FALSE";
    case OP_CALL:
        return "CALL";
    case OP_PRINT:
        return "PRINT";
    case OP_RETURN:
        return "RETURN";
    case OP_POP:
        return "POP";
    case OP_HALT:
        return "HALT";
    }
    return "???";
}

static void print_value_inline(Value v)
{
    print_value(v);
}

void disassemble(const BytecodeProgram *prog)
{
    printf("============ BYTECODE (%d fonctions) ============\n", prog->nfuncs);
    for (int f = 0; f < prog->nfuncs; f++)
    {
        const Function *fn = &prog->funcs[f];
        printf("\n-- fonction [%d] %s (arity=%d, nlocals=%d) --\n",
               f, fn->name, fn->arity, fn->nlocals);
        for (int i = 0; i < fn->chunk.count; i++)
        {
            const Instr *in = &fn->chunk.code[i];
            printf("  %04d  %-14s", i, opcode_name(in->op));
            switch (in->op)
            {
            case OP_CONST:
                printf(" %d  (", in->a);
                print_value_inline(prog->consts[in->a]);
                printf(")");
                break;
            case OP_GET_LOCAL:
            case OP_SET_LOCAL:
            case OP_GET_GLOBAL:
            case OP_SET_GLOBAL:
            case OP_JUMP:
            case OP_JUMP_IF_FALSE:
                printf(" %d", in->a);
                break;
            case OP_CALL:
                printf(" fn=%d (%s) nargs=%d", in->a,
                       in->a >= 0 && in->a < prog->nfuncs ? prog->funcs[in->a].name : "?",
                       in->b);
                break;
            default:
                break;
            }
            printf("\n");
        }
    }
    printf("=================================================\n");
}
