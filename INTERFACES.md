# INTERFACES.md — Le contrat entre vous deux

> ⚠️ **À lire et à figer AVANT de coder, chacun de son côté.**
> Ce fichier décrit les 3 points de contact entre le front-end (Amine)
> et le runtime (toi). Tant qu'ils ne changent pas, vous pouvez avancer
> **en parallèle sans vous bloquer**.

---

## 1. Qui fait quoi

| Amine — front-end | Toi — runtime |
|---|---|
| `src/lexer.h` / `src/lexer.c` | `src/vm.h` / `src/vm.c` |
| `src/ast.h` | `src/jit.h` / `src/jit.c` |
| `src/parser.h` / `src/parser.c` | `src/main.c` (CLI) |
| `src/bytecode.c` (désassembleur) | `tests/` + benchmarks |
| `src/compiler.h` / `src/compiler.c` | |
| `INTERFACES.md` (co-signé) | `INTERFACES.md` (co-signé) |

**Règle :** personne ne modifie les fichiers de l'autre sans une PR relue.

---

## 2. Le format du bytecode (`bytecode.h`)

Machine à pile. Une instruction = `{ uint8_t op; int32_t a; int32_t b; }`.

### Conventions

| Registre / mémoire | Contenu |
|---|---|
| Pile d'opérandes | les valeurs temporaires des expressions |
| `frame[slot]` | variables **locales** de la fonction courante (`let` + paramètres) |
| `globals[index]` | variables **globales** (les `let` du script) |
| `consts[index]` | constantes numériques (table globale) |

### Effet des opcodes sur la pile

| Opcode | Effet |
|---|---|
| `CONST a` | push `consts[a]` |
| `NIL` | push `0` |
| `GET_LOCAL a` | push `frame[a]` |
| `SET_LOCAL a` | `frame[a] = peek()` (**ne dépile pas** : la valeur reste pour l'expression) |
| `GET_GLOBAL a` / `SET_GLOBAL a` | idem sur `globals[a]` |
| `ADD/SUB/MUL/DIV/MOD` | `pop b; pop a; push a ⊕ b` |
| `NEG` / `NOT` | transforme le sommet |
| `EQ/NEQ/LT/LE/GT/GE` | `pop b; pop a; push (a ⊕ b) ? 1 : 0` |
| `JUMP a` | `ip = a` |
| `JUMP_IF_FALSE a` | `cond = pop(); si !cond alors ip = a` |
| `CALL a b` | `args = top b valeurs` ; `push vm_invoke(a, args, b)` |
| `PRINT` | `pop` + affiche + retour à la ligne |
| `RETURN` | `v = pop(); return v` |
| `POP` | jette le sommet (fin d'une expression utilisée comme instruction) |
| `HALT` | termine |

### Invariants garantis par le compilateur (Amine)

1. **La pile est vide** à chaque saut et à chaque début de bloc
   → le JIT peut compiler chaque bloc indépendamment.
2. Les **sauts sont des adresses absolues** vers des index valides.
3. Chaque fonction se termine par `RETURN` (ajouté automatiquement).
4. `nlocals` = nombre de slots occupés (paramètres inclus, en tête).

Ces 4 règles sont ce qui rend le JIT simple : **si tu les casses, préviens !**

---

## 3. L'API runtime (`vm.h`)

Le code généré par le JIT appelle ces fonctions :

```c
/* Point d'entrée unique : exécute fnidx de façon native si possible,
 * sinon interprétée. Utilisé par l'interpréteur ET par le code JIT. */
double vm_invoke(VM *vm, int fnidx, double *args, int nargs);

/* Affiche un nombre (utilisé par l'opcode PRINT compilé). */
void vm_print_number(double v);
```

**Signature d'une fonction compilée :**

```c
double jit_<nom>_<index>(void *vm, double *args, int nargs);
```

- `args` pointe vers un tampon valide pendant tout l'appel ;
- le prologue copie les arguments dans des variables locales → **aucune
  fonction ne dépend du tampon une fois entrée**.

---

## 4. Les valeurs (`value.h`)

**v0.1 : `typedef double Value;`** → tout est nombre.

C'est un choix assumé (et à expliquer dans le rapport) :
- le JIT reste trivial (une valeur = un registre SSE) ;
- on peut livrer un projet **complet et fonctionnel** ;
- l'extension se fait **ensuite**, sans tout casser.

**Extension v0.2 (si vous avez le temps) — même stratégie dans les 2 camps :**

```c
typedef struct {
    enum { VAL_NIL, VAL_BOOL, VAL_NUM, VAL_STR } type;
    union { bool b; double n; struct StrObj *s; } as;
} Value;
```

Conséquences :
- l'interpréteur gagne des vérifications de type ;
- le JIT doit soit **dé-optimiser** les fonctions non-numériques,
  soit ajouter des **gardes de type** (type checks) et un fallback ;
- l'arithmétique devient `float` + tags → plus lent mais plus « dynamique ».

👉 C'est exactement l'arbitrage que le jury attend dans la section
« difficultés / choix techniques » de votre rapport.

---

## 5. Ce qui est déjà fait et ce qui reste

| Brique | État |
|---|---|
| Lexer | ✅ v0.1 (nombres, identifiants, opérateurs, `//` commentaires) |
| Parser + AST | ✅ v0.1 (`let`, `fn`, `if/else`, `while`, `return`, `print`) |
| Compilateur AST → bytecode | ✅ v0.1 |
| Désassembleur | ✅ `--dump-bytecode` |
| VM + interpréteur | ✅ v0.1 |
| Profiler (points chauds) | ✅ seuil configurable `--threshold` |
| JIT libgccjit | ✅ v0.1 (arithmétique, conditions, boucles, appels) |
| Tests automatiques | ✅ 5 programmes, JIT vs interprété |
| **v0.2 : types dynamiques (bool/str/nil)** | ⬜ |
| **v0.2 : tableaux + indexation** | ⬜ |
| **v0.3 : OSR (compiler une boucle en cours)** | ⬜ |
| **v0.3 : inline caching des appels** | ⬜ |

---

## 6. Commandes utiles aux deux

```bash
cmake -S . -B build && cmake --build build     # compiler
./build/mlang tests/fib.mlang                  # exécuter
./build/mlang --dump-bytecode tests/fib.mlang  # voir le bytecode
./build/mlang --jit=off --stats tests/fib.mlang
./tests/run_tests.sh                           # suite de tests
MLANG_DUMP_JIT=1 ./build/mlang tests/fib.mlang # écrire l'IR dans /tmp/jit_*.c
```
