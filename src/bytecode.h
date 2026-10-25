/*
 * bytecode.h — Format du bytecode (LE CONTRAT entre front-end et runtime)
 * ----------------------------------------------------------------------
 * ⚠️ CE FICHIER EST LE POINT DE RENCONTRE ENTRE VOS DEUX PARTIES.
 *    Une fois figé, Amine (AST → bytecode) et toi (VM + JIT) pouvez
 *    travailler en parallèle sans vous bloquer.
 *
 * Convention : pile d'opérandes + variables locales indexées par slot.
 */
#ifndef MLANG_BYTECODE_H
#define MLANG_BYTECODE_H

#include <stdint.h>
#include "value.h"

#define MLANG_MAX_FUNCS     256
#define MLANG_MAX_CONSTS    4096
#define MLANG_MAX_LOCALS    256
#define MLANG_MAX_GLOBALS   1024
#define MLANG_STACK_SIZE    8192

typedef enum {
    OP_CONST,        /* a = index constante          ; push consts[a]        */
    OP_NIL,          /* push 0.0                                             */
    OP_GET_LOCAL,    /* a = slot                     ; push frame[a]         */
    OP_SET_LOCAL,    /* a = slot                     ; frame[a] = pop()      */
    OP_GET_GLOBAL,   /* a = index global             ; push globals[a]       */
    OP_SET_GLOBAL,   /* a = index global             ; globals[a] = pop()    */

    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,   /* b = pop(), a = pop()      */
    OP_NEG,                                   /* a = -pop()                */
    OP_NOT,                                   /* a = (pop() == 0)          */

    OP_EQ, OP_NEQ, OP_LT, OP_LE, OP_GT, OP_GE,

    OP_JUMP,         /* a = adresse cible                                    */
    OP_JUMP_IF_FALSE,/* a = adresse cible ; pop condition                    */

    OP_CALL,         /* a = index fonction, b = nb arguments                 */
    OP_PRINT,        /* pop et affiche                                       */
    OP_RETURN,       /* retourne pop()                                       */
    OP_POP,          /* jette la valeur du sommet                            */
    OP_HALT          /* fin du script                                        */
} OpCode;

typedef struct {
    uint8_t op;
    int32_t a;
    int32_t b;
    int32_t line;
} Instr;

typedef struct {
    Instr *code;
    int    count;
    int    cap;
} Chunk;

typedef struct {
    char   name[64];
    int    arity;
    int    nlocals;       /* slots occupés (params inclus) */
    Chunk  chunk;
} Function;

typedef struct {
    Function funcs[MLANG_MAX_FUNCS];
    int      nfuncs;
    int      script_func;          /* index de la fonction <script> */
    Value    consts[MLANG_MAX_CONSTS];
    int      nconsts;
    int      nglobals;
} BytecodeProgram;

/* --- petit désassembleur (indispensable pour déboguer) --- */
const char *opcode_name(uint8_t op);
void        disassemble(const BytecodeProgram *prog);

#endif /* MLANG_BYTECODE_H */
