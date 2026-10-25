# MLang — mini-langage dynamique avec VM + compilateur JIT

> **Projet 3** — Compilateur Just-In-Time pour un langage dynamique
> Pipeline complet : `source → lexer → parser → AST → bytecode → VM (interpréteur + profiler) → JIT (libgccjit) → code machine x86-64`

```
fn fib(n) {
    if (n < 2) { return n; }
    return fib(n - 1) + fib(n - 2);
}
print(fib(30));
```

```
$ ./build/mlang --jit=off bench/fib30.mlang      # interprété
832040                                   ~127 ms

$ ./build/mlang --jit=on  bench/fib30.mlang      # JIT
[jit] fib      compilee en natif apres 100 appels (21.4 ms)
832040                                    ~41 ms   (≈ 3× plus rapide)
```

---

## 1. Build

```bash
cmake -S . -B build
cmake --build build
```

Dépendance : **libgccjit** (voir le guide d'installation).
Ubuntu/WSL : `sudo apt install libgccjit-$(gcc -dumpversion | cut -d. -f1)-dev`

## 2. Utilisation

```bash
./build/mlang programme.mlang              # JIT activé (seuil 100)
./build/mlang --jit=off programme.mlang    # interpréteur seul
./build/mlang --threshold=50 programme.mlang
./build/mlang --dump-bytecode programme.mlang   # voir le bytecode
./build/mlang --stats programme.mlang           # temps, fonctions compilées
./build/mlang -e "print(2 * 21);"
./build/mlang --help
```

## 3. Tests

```bash
./tests/run_tests.sh
```

La suite vérifie pour chaque programme :
1. la sortie attendue (`.expected`) ;
2. **que le JIT produit exactement le même résultat que l'interpréteur** — le test le plus important du projet.

## 4. Le langage (v0.1)

```
let x = 10;                  // variable (globale ou locale)
fn carre(n) { return n*n; }  // fonction, récursion autorisée
if (cond) { ... } else { ... }
while (cond) { ... }
return expr;
print(expr);
```

Opérateurs : `+ - * / %`, comparaisons `< <= > >= == !=`, négation unaire.
Commentaires : `// ...`

> v0.1 = **tout est un nombre** ; l'extension vers des types dynamiques
> (bool, string, nil) est décrite dans `INTERFACES.md` §4.

## 5. Architecture

```
src/
├── lexer.h/.c      texte  → tokens
├── ast.h           nœuds de l'arbre + arène d'allocation
├── parser.h/.c     tokens → AST            (descente récursive)
├── bytecode.h      OPCODES + structures    ⚠️ LE CONTRAT
├── bytecode.c      désassembleur (debug)
├── compiler.h/.c   AST    → bytecode
├── vm.h/.c         VM à pile, interpréteur, profiler
├── jit.h/.c        bytecode → libgccjit → code machine
└── main.c          CLI
```

### Comment fonctionne le JIT (résumé)

1. La VM exécute du bytecode et compte les appels de chaque fonction.
2. Au-delà du seuil (`--threshold`), `jit_compile_function()` :
   - crée un contexte libgccjit ;
   - déclare `double jit_fib_1(void *vm, double *args, int nargs)` ;
   - traduit chaque opcode en IR : une **pile de compilation** donne
     naturellement une forme SSA (chaque valeur est stockée dans une
     variable temporaire) ;
   - construit le **graphe de blocs** (analyse de joignabilité : les
     instructions inatteignables sont ignorées) ;
   - compile et récupère le pointeur machine.
3. La VM remplace la version interprétée par le pointeur natif.
4. Les appels internes passent par `vm_invoke()` : le code compilé peut
   appeler des fonctions encore interprétées (et inversement).

### Deux pièges classiques (documentés pour le rapport)

- **Évaluation paresseuse des rvalues** : libgccjit construit des
  *expressions*, pas des instructions. Si deux appels partagent un tampon
  d'arguments, les deux stores sont exécutés avant les deux appels →
  bug. Solution : forcer chaque valeur dans une **variable temporaire**
  (`store_temp()` dans `jit.c`).
- **Blocs inatteignables interdits** : après un `return`/`jump`, le code
  suivant n'appartient à aucun bloc. Solution : analyse de joignabilité
  en deux passes avant l'émission.

## 6. Benchmarks (WSL2/Ubuntu, machine de Samuel)

| Programme | Interprété | JIT | Accélération |
|---|---:|---:|---:|
| `fib(30)` récursif (seuil JIT = 100 appels) | 178.2 ± 8.1 ms | 88.5 ± 3.0 ms | **×2.01** |
| `somme(3 000 000)` — boucle (seuil JIT = 1 appel) | 256.9 ± 19.2 ms | 75.4 ± 1.3 ms | **×3.41** |

*Méthodologie : 1 exécution d'échauffement écartée, puis 5 exécutions mesurées par configuration (moyenne ± écart-type). Le temps de compilation JIT est inclus. Le gain plus élevé de la boucle s'explique par sa compilation dès le premier appel et son exécution ensuite entièrement native ; pour `fib`, le début est interprété jusqu'au seuil et les appels récursifs passent par `vm_invoke`.*

Pour régénérer ces résultats : `./bench/run_bench.sh` (ils sont écrits dans `bench/RESULTS.md`).

> 💡 **Observation importante pour le rapport** : `somme()` n'est appelée
> qu'**une seule fois** — un profiler qui ne compte que les appels de
> fonctions ne la voit jamais devenir « chaude », même si elle boucle
> 3 millions de fois. C'est exactement le problème que résout l'**OSR**
> (*On-Stack Replacement*, v0.2) en comptant aussi les **retours de boucle**.

## 7. Répartition du travail

| Amine (front-end) | Toi (runtime) |
|---|---|
| lexer, parser, AST, compilateur, désassembleur, tests | VM, interpréteur, profiler, JIT, benchmarks, CLI |

Voir **`INTERFACES.md`** pour le contrat détaillé.

## 8. Pistes pour la v0.2 / v0.3

- [x] Types dynamiques (`nil`, `bool`, `string`) + `print` de chaînes
- [ ] Tableaux et indexation
- [ ] `!` logique (déjà présent dans l'AST, à câbler dans le lexer)
- [x] OSR : compiler une **boucle** déjà en cours d'exécution
- [ ] Inline caching / spécialisation des appels
- [ ] REPl interactif
- [ ] Messages d'erreur avec ligne + extrait du source

## 9. OSR (v0.3) — mesure sur bench/boucle.mlang

`somme(3000000)` n'est appelée qu'une fois : le JIT par appels ne la voit
jamais chaude. L'OSR compte les sauts arrière et compile la fonction
pendant l'exécution de la boucle.

| Mode | Temps total | Fonctions JIT | Sauts arrière |
|---|---|---|---|
| Interprété pur (`--jit=off`) | 324.6 ms | 0 | 3 000 000 |
| JIT par appels (`--osr-threshold=0`) | 335.4 ms | 0 | 3 000 000 |
| JIT + OSR | 84.1 ms | 1 | 1 000 |

Gain OSR vs interprété : x3,9 (temps de compilation libgccjit inclus).
Résultat identique dans les 3 modes : 4499998500000.

## 10. Robustesse mémoire (valgrind)

Commandes :
  valgrind --leak-check=full --error-exitcode=1 ./build/mlang --jit=off tests/fib.mlang
  valgrind --leak-check=full --error-exitcode=1 ./build/mlang --jit=on  tests/fib.mlang

Résultats :
- 0 accès mémoire invalide (« Invalid read/write ») dans les deux modes,
  et aucune valeur non initialisée dans notre code.
- Fuites connues et attribuées (piles d'allocation valgrind) :
  * 108 octets (5 blocs) dans le parser : tableaux d'AST non libérés
    (parser.c:160, parser.c:428-429). Sans effet pour un programme
    court ; correction à faire côté front-end.
  * ~2,6 Mo non libérés à l'intérieur de libgccjit en mode JIT
    (allocateurs par pools de gcc, jamais libérés bloc par bloc),
    dont 73 728 octets alloués dès le chargement de libgccjit.so
    (visibles même avec --jit=off).
La part de libgccjit a été isolée en comparant les runs --jit=off
et --jit=on. Piste : fichier de suppressions valgrind pour libgccjit.
