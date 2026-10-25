# Résultats des benchmarks mlang

Généré le 2026-10-18 à 21:09 par `./bench/run_bench.sh`
(1 run d'échauffement jeté + 5 exécutions par configuration, moyenne ± écart-type ;
le temps de compilation JIT est inclus).

| Benchmark | Interprété | JIT | Gain |
|---|---:|---:|---:|
| fib(30) — récursion (seuil JIT = 100 appels) | 178.2 ± 8.1 ms | 88.5 ± 3.0 ms | ×2.01 |
| somme(3 000 000) — boucle (seuil JIT = 1) | 256.9 ± 19.2 ms | 75.4 ± 1.3 ms | ×3.41 |
