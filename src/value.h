/*
 * value.h — Représentation des valeurs du langage
 * -------------------------------------------------
 * v0.2 (étape 3.1) : valeur étiquetée.
 *
 * Une Value = une étiquette (`type`) + un contenu (`as`).
 * L'étiquette dit comment interpréter le contenu :
 *   VAL_NIL  -> rien
 *   VAL_BOOL -> as.b
 *   VAL_NUM  -> as.n
 *   VAL_STR  -> as.s   (allocation des chaînes : étape 3.2)
 */
#ifndef MLANG_VALUE_H
#define MLANG_VALUE_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
/* Objet chaîne (créé/alloué à l'étape 3.2) */
typedef struct ObjStr {
    char *chars;
    int   length;
} ObjStr;

typedef enum {
    VAL_NIL,
    VAL_BOOL,
    VAL_NUM,
    VAL_STR
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool    b;
        double  n;
        ObjStr *s;
    } as;
} Value;

/* ---------- constructeurs ---------- */
static inline Value value_nil(void)
{
    Value v = {0};
    v.type = VAL_NIL;
    return v;
}

static inline Value value_bool(bool b)
{
    Value v  = {0};
    v.type   = VAL_BOOL;
    v.as.b   = b;
    return v;
}

static inline Value value_num(double n)
{
    Value v  = {0};
    v.type   = VAL_NUM;
    v.as.n   = n;
    return v;
}

static inline Value value_str(ObjStr *s)
{
    Value v  = {0};
    v.type   = VAL_STR;
    v.as.s   = s;
    return v;
}

#define VALUE_NIL   (value_nil())
#define VALUE_TRUE  (value_bool(true))
#define VALUE_FALSE (value_bool(false))

/* ---------- tests de type ---------- */
static inline bool is_nil (Value v) { return v.type == VAL_NIL;  }
static inline bool is_bool(Value v) { return v.type == VAL_BOOL; }
static inline bool is_num (Value v) { return v.type == VAL_NUM;  }
static inline bool is_str (Value v) { return v.type == VAL_STR;  }

/* ---------- extraction (vérifier le type AVANT) ---------- */
static inline bool    as_bool(Value v) { return v.as.b; }
static inline double  as_num (Value v) { return v.as.n; }
static inline ObjStr *as_str (Value v) { return v.as.s; }
static inline const char *value_type_name(Value v)


{
    switch (v.type) {
        case VAL_NIL:  return "nil";
        case VAL_BOOL: return "bool";
        case VAL_NUM:  return "nombre";
        case VAL_STR:  return "chaine";
    }
    return "?";
}

static inline bool values_equal(Value a, Value b)
{
    if (a.type != b.type) return false;

    switch (a.type) {
        case VAL_NIL:
            return true;
        case VAL_BOOL:
            return a.as.b == b.as.b;
        case VAL_NUM:
            return a.as.n == b.as.n;
        case VAL_STR:
            if (a.as.s == b.as.s) return true;
            if (!a.as.s || !b.as.s || !a.as.s->chars || !b.as.s->chars) return false;
            return a.as.s->length == b.as.s->length &&
                   memcmp(a.as.s->chars, b.as.s->chars, (size_t)a.as.s->length) == 0;
    }
    return false;
}

static inline bool is_truthy(Value v)
{
    switch (v.type) {
        case VAL_NIL:  return false;
        case VAL_BOOL: return v.as.b;
        case VAL_NUM:  return v.as.n != 0.0;
        case VAL_STR:  return true;
    }
    return false;
}

static inline void print_value(Value v)
{
    switch (v.type) {
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_BOOL:
            printf("%s", v.as.b ? "true" : "false");
            break;
        case VAL_NUM:
            /* affiche 3.0 comme "3" et 3.5 comme "3.5" */
            if (v.as.n == (long long)v.as.n) printf("%lld", (long long)v.as.n);
            else                             printf("%g", v.as.n);
            break;
        case VAL_STR:
            if (v.as.s && v.as.s->chars) printf("%.*s", v.as.s->length, v.as.s->chars);
            else                          printf("<string>");
            break;
    }
}

#endif /* MLANG_VALUE_H */