#!/usr/bin/env bash
# ============================================================
#  run_tests.sh — Suite de tests de mlang
#  Vérifie pour chaque programme :
#    1. la sortie attendue (fichier .expected)
#    2. que le mode JIT donne EXACTEMENT le même résultat
#       que le mode interprété  (le test le plus important !)
#
#  Usage :  ./tests/run_tests.sh [chemin/vers/mlang]
# ============================================================

set -u
cd "$(dirname "$0")/.." || exit 1

MLANG="${1:-./build/mlang}"
GREEN="\033[0;32m"; RED="\033[0;31m"; BOLD="\033[1m"; NC="\033[0m"

if [ ! -x "$MLANG" ]; then
    echo "❌ binaire introuvable : $MLANG"
    echo "   compile d'abord : cmake -S . -B build && cmake --build build"
    exit 1
fi

pass=0; fail=0

for f in tests/*.mlang; do
    name="$(basename "$f" .mlang)"
    exp="tests/$name.expected"

    out_interp="$("$MLANG" --jit=off "$f" 2>/dev/null)"
    out_jit="$("$MLANG" --jit=on --threshold=1 "$f" 2>/dev/null)"

    ok=1
    if [ -f "$exp" ] && [ "$out_interp" != "$(cat "$exp")" ]; then
        echo -e "${RED}❌ $name : sortie interprétée != attendu${NC}"
        diff <(echo "$out_interp") "$exp" | head -6
        ok=0
    fi
    if [ "$out_interp" != "$out_jit" ]; then
        echo -e "${RED}❌ $name : JIT != interpréteur (résultats différents !)${NC}"
        diff <(echo "$out_interp") <(echo "$out_jit") | head -6
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo -e "${GREEN}✅ $name${NC}"
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done

echo
echo -e "${BOLD}$pass réussis, $fail échoués${NC}"
[ "$fail" -eq 0 ] && exit 0 || exit 1
