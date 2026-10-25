#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serveur.py — Interface web de démonstration pour mlang (VM + JIT)
================================================================
Lancement (depuis la racine du projet) :

    python3 gui/serveur.py

Puis ouvrir dans le navigateur (Windows) :  http://localhost:8765

Principe : cette page ne réimplémente RIEN du langage. Elle envoie le
code au vrai binaire ./build/mlang avec les options de la ligne de
commande (--jit=off / --jit=on / --osr-threshold / --stats) et affiche
sa sortie + ses statistiques. Aucun fichier C n'est modifié.
"""

import json
import os
import re
import subprocess
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

# ----------------------------------------------------------------------
# Chemins (relatifs à ce fichier : <projet>/gui/serveur.py)
# ----------------------------------------------------------------------
RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MLANG = os.path.join(RACINE, "build", "mlang")
PROG_TMP = os.path.join(RACINE, "gui", "prog_gui.mlang")
PORT = 8765
DELAI_MAX = 10  # secondes (protection contre les boucles infinies)

MODES = {
    "interp": ["--jit=off", "--stats"],
    "jit": ["--jit=on", "--osr-threshold=0", "--stats"],
    "osr": ["--jit=on", "--stats"],
}

PAGE = r"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<title>mlang — VM + JIT · démonstration</title>
<style>
  body { margin:0; font-family:Segoe UI, Arial, sans-serif; background:#1e1e1e; color:#d4d4d4; }
  header { background:#252526; padding:14px 22px; border-bottom:2px solid #0e639c; }
  header h1 { margin:0; font-size:22px; color:#fff; }
  header p { margin:4px 0 0; color:#9cdcfe; font-size:13px; }
  main { display:flex; gap:16px; padding:16px 22px; }
  .col { flex:1; min-width:0; }
  h2 { font-size:15px; color:#dcdcdc; margin:6px 0 8px; }
  select, textarea { width:100%; box-sizing:border-box; background:#111; color:#d4d4d4;
                     border:1px solid #3c3c3c; border-radius:6px; }
  select { padding:7px; margin-bottom:8px; font-size:14px; }
  textarea { height:300px; padding:10px; font-family:Consolas, monospace; font-size:14px;
             line-height:1.45; resize:vertical; }
  .modes { margin:10px 0; font-size:14px; }
  .modes label { display:block; margin:5px 0; cursor:pointer; }
  #btn { background:#0e639c; color:#fff; border:0; border-radius:6px; padding:11px 26px;
         font-size:16px; cursor:pointer; }
  #btn:hover:not(:disabled) { background:#1177bb; }
  #btn:disabled { opacity:.6; cursor:wait; }
  .badges { display:flex; gap:10px; margin-bottom:10px; }
  .carte { background:#252526; border:1px solid #3c3c3c; border-radius:8px;
           padding:8px 14px; text-align:center; min-width:110px; }
  .carte .titre { font-size:11px; color:#9cdcfe; text-transform:uppercase; letter-spacing:.5px; }
  .carte .valeur { font-size:22px; font-weight:bold; color:#fff; }
  .badge.ok { color:#4ec9b0 !important; } .badge.err { color:#f44747 !important; }
  pre { background:#111; border:1px solid #3c3c3c; border-radius:6px; padding:10px;
        min-height:80px; max-height:340px; overflow:auto; white-space:pre-wrap;
        font-family:Consolas, monospace; font-size:13px; }
  footer { padding:6px 22px 16px; color:#808080; font-size:12px; }
</style>
</head>
<body>
<header>
  <h1>mlang — machine virtuelle + compilation JIT</h1>
  <p>Interpréteur de bytecode, profiler (appels + boucles), JIT libgccjit, OSR — projet binôme : Samuel & Amine </p>
</header>
<main>
  <div class="col">
    <h2>Programme mlang</h2>
    <select id="exemple" onchange="chargerExemple()">
      <option value="boucle">🚀 Boucle chaude — somme(3 000 000) : l'OSR en action</option>
      <option value="fib">⚡ Fibonacci récursif — le JIT par appels</option>
      <option value="chaines">🧵 Chaînes de caractères — types dynamiques</option>
      <option value="divzero">⚠️ Division par zéro — erreur propre</option>
    </select>
    <textarea id="code" spellcheck="false"></textarea>
    <div class="modes">
      <label><input type="radio" name="mode" value="interp" checked> 🐢 Interprété seul <code>--jit=off</code></label>
      <label><input type="radio" name="mode" value="jit"> ⚡ JIT par appels (sans OSR) <code>--jit=on --osr-threshold=0</code></label>
      <label><input type="radio" name="mode" value="osr"> 🚀 JIT + OSR <code>--jit=on</code></label>
    </div>
    <button id="btn" onclick="executer()">▶ Exécuter</button>
  </div>
  <div class="col">
    <h2>Résultat</h2>
    <div class="badges">
      <div class="carte"><div class="titre">⏱ Temps total</div><div class="valeur" id="b_temps">—</div></div>
      <div class="carte"><div class="titre">⚙ Fonctions JIT</div><div class="valeur" id="b_jit">—</div></div>
      <div class="carte"><div class="titre">Code de sortie</div><div class="valeur badge" id="b_code">—</div></div>
    </div>
    <h2>Sortie du programme</h2>
    <pre id="sortie">(clique sur « Exécuter »)</pre>
    <h2>Messages + statistiques de la VM</h2>
    <pre id="stats"></pre>
  </div>
</main>
<footer>Compare les 3 modes sur l'exemple « Boucle chaude » : interprété ≈ 325 ms · JIT par appels ≈ idem (il ne voit pas la boucle !) · JIT + OSR ≈ 84 ms → gain ×3,9.</footer>
<script>
const EXEMPLES = {
  boucle: "// boucle chaude : somme de 1 a 3 000 000 (1 seul appel !)\nfn somme(n) {\n    let i = 0;\n    let s = 0;\n    while (i <= n) {\n        s = s + i;\n        i = i + 1;\n    }\n    return s;\n}\nprint(somme(3000000));",
  fib: "// recursion : des dizaines de milliers d'appels\nfn fib(n) {\n    if (n < 2) { return n; }\n    return fib(n - 1) + fib(n - 2);\n}\nprint(fib(25));",
  chaines: "// types dynamiques (v0.2) : concatenation de chaines\nlet a = \"Machine\";\nlet b = \"virtuelle\";\nprint(a + \" \" + b + \" + JIT\");",
  divzero: "// erreur d'execution propre (message + ligne + code 1)\nprint(1);\nlet zero = 0;\nprint(10 / zero);\nprint(2);"
};
function chargerExemple() {
  document.getElementById('code').value = EXEMPLES[document.getElementById('exemple').value];
}
async function executer() {
  const btn = document.getElementById('btn');
  btn.disabled = true; btn.textContent = '⏳ Exécution…';
  const code = document.getElementById('code').value;
  const mode = document.querySelector('input[name=mode]:checked').value;
  try {
    const r = await fetch('/run', { method:'POST', headers:{'Content-Type':'application/json'},
                                    body: JSON.stringify({ code: code, mode: mode }) });
    const d = await r.json();
    document.getElementById('sortie').textContent = d.stdout ? d.stdout : '(aucune sortie)';
    document.getElementById('stats').textContent = d.stderr || '';
    document.getElementById('b_temps').textContent = (d.temps_ms === null || d.temps_ms === undefined) ? '—' : d.temps_ms + ' ms';
    document.getElementById('b_jit').textContent = (d.fonctions_jit === null || d.fonctions_jit === undefined) ? '—' : d.fonctions_jit;
    const bc = document.getElementById('b_code');
    bc.textContent = d.code;
    bc.className = 'valeur badge ' + (d.code === 0 ? 'ok' : 'err');
  } catch (e) {
    document.getElementById('sortie').textContent = 'Erreur : serveur injoignable.\nRelance-le avec : python3 gui/serveur.py';
  }
  btn.disabled = false; btn.textContent = '▶ Exécuter';
}
chargerExemple();
</script>
</body>
</html>
"""


def executer(code, mode):
    """Écrit le programme, lance ./build/mlang, renvoie les résultats."""
    if mode not in MODES:
        mode = "interp"
    if not os.path.isfile(MLANG):
        return {"stdout": "",
                "stderr": "Binaire introuvable : " + MLANG +
                          "\nCompile d'abord : cmake --build build",
                "code": -1, "temps_ms": None, "fonctions_jit": None}
    with open(PROG_TMP, "w", encoding="utf-8") as f:
        f.write(code)
    commande = [MLANG] + MODES[mode] + [PROG_TMP]
    try:
        p = subprocess.run(commande, capture_output=True, text=True,
                           errors="replace", timeout=DELAI_MAX, cwd=RACINE)
    except subprocess.TimeoutExpired:
        return {"stdout": "",
                "stderr": "(délai dépassé : %d s — boucle infinie ?)" % DELAI_MAX,
                "code": -1, "temps_ms": None, "fonctions_jit": None}
    m = re.search(r"temps total\s*:\s*([0-9]+(?:[.,][0-9]+)?)\s*ms", p.stderr)
    temps = float(m.group(1).replace(",", ".")) if m else None
    m2 = re.search(r"fonctions JIT\s*:\s*([0-9]+)", p.stderr)
    fjit = int(m2.group(1)) if m2 else None
    return {"stdout": p.stdout, "stderr": p.stderr, "code": p.returncode,
            "temps_ms": temps, "fonctions_jit": fjit}


class Gestionnaire(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path in ("/", "/index.html"):
            corps = PAGE.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(corps)))
            self.end_headers()
            self.wfile.write(corps)
        else:
            self.send_error(404)

    def do_POST(self):
        if self.path != "/run":
            self.send_error(404)
            return
        try:
            taille = int(self.headers.get("Content-Length", 0))
            donnees = json.loads(self.rfile.read(taille).decode("utf-8"))
            resultat = executer(str(donnees.get("code", "")),
                                str(donnees.get("mode", "interp")))
        except Exception as exc:  # ne jamais faire tomber le serveur
            resultat = {"stdout": "", "stderr": "Erreur serveur : %s" % exc,
                        "code": -1, "temps_ms": None, "fonctions_jit": None}
        corps = json.dumps(resultat).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(corps)))
        self.end_headers()
        self.wfile.write(corps)

    def log_message(self, *args):  # terminal propre
        pass


def main():
    os.makedirs(os.path.join(RACINE, "gui"), exist_ok=True)
    serveur = HTTPServer(("0.0.0.0", PORT), Gestionnaire)
    print("mlang GUI — serveur démarré")
    print("Ouvre dans ton navigateur :  http://localhost:%d" % PORT)
    print("(Ctrl+C pour arrêter)")
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        print("\nArrêt du serveur.")


if __name__ == "__main__":
    main()
