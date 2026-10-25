#!/usr/bin/env bash
# ============================================================
#  run_bench.sh — benchmarks reproductibles pour le rapport
#  5 runs par configuration, moyenne + ecart-type,
#  sortie : tableau markdown dans bench/RESULTS.md
#  Usage : ./bench/run_bench.sh [chemin/vers/mlang]
# ============================================================
set -u
cd "$(dirname "$0")/.." || exit 1
M="${1:-./build/mlang}"
RUNS=5
OUT="bench/RESULTS.md"

if [ ! -x "$M" ]; then
    echo "binaire introuvable : $M (compile avec : cmake --build build)" >&2
    exit 1
fi

# --- lance la commande 1 fois, renvoie son "temps total" (ms) ---
temps_ms() {
    "$@" --stats 2>&1 >/dev/null | awk '/temps total/ {print $4}'
}

# --- $1 = label (affiché sur stderr), $2.. = commande ---
# --- renvoie sur stdout : "moyenne ecart_type" sur $RUNS runs ---
mesure() {
    local desc="$1"; shift
    echo "  mesure : $desc (1 warmup + $RUNS runs)" >&2
    temps_ms "$@" >/dev/null   # warmup : jete (caches OS, libgccjit deja chargee)
    local vals=()
    for _ in $(seq 1 "$RUNS"); do
        local ms
        ms=$(temps_ms "$@")
        [ -z "$ms" ] && ms=0
        vals+=("$ms")
    done
    printf '%s\n' "${vals[@]}" | awk '
        { a[NR] = $1; s += $1 }
        END {
            n = NR; moy = s / n
            s2 = 0; for (i = 1; i <= n; i++) s2 += (a[i] - moy)^2
            et = (n > 1) ? sqrt(s2 / (n - 1)) : 0
            printf "%.1f %.1f", moy, et
        }'
}

gain() { awk -v i="$1" -v j="$2" 'BEGIN { printf "%.2f", (j > 0) ? i / j : 0 }'; }

echo "Benchmarks mlang — $RUNS runs par configuration" >&2

fib_i=$(mesure "fib(30) interprete"  "$M" --jit=off bench/fib30.mlang)
fib_j=$(mesure "fib(30) JIT (100)"   "$M" --jit=on --threshold=100 bench/fib30.mlang)
bou_i=$(mesure "somme 3M interprete" "$M" --jit=off bench/boucle.mlang)
bou_j=$(mesure "somme 3M JIT (1)"    "$M" --jit=on --threshold=1 bench/boucle.mlang)

read -r fib_i_moy fib_i_et <<< "$fib_i"
read -r fib_j_moy fib_j_et <<< "$fib_j"
read -r bou_i_moy bou_i_et <<< "$bou_i"
read -r bou_j_moy bou_j_et <<< "$bou_j"

{
    echo "# Résultats des benchmarks mlang"
    echo
    echo "Généré le $(date '+%Y-%m-%d à %H:%M') par \`./bench/run_bench.sh\`"
    echo "(1 run d'échauffement jeté + $RUNS exécutions par configuration, moyenne ± écart-type ;"
    echo "le temps de compilation JIT est inclus)."
    echo
    echo "| Benchmark | Interprété | JIT | Gain |"
    echo "|---|---:|---:|---:|"
    echo "| fib(30) — récursion (seuil JIT = 100 appels) | $fib_i_moy ± $fib_i_et ms | $fib_j_moy ± $fib_j_et ms | ×$(gain "$fib_i_moy" "$fib_j_moy") |"
    echo "| somme(3 000 000) — boucle (seuil JIT = 1) | $bou_i_moy ± $bou_i_et ms | $bou_j_moy ± $bou_j_et ms | ×$(gain "$bou_i_moy" "$bou_j_moy") |"
} > "$OUT"

echo >&2
cat "$OUT"