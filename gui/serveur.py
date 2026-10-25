#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
serveur.py — Interface web de démonstration pour mlang (VM + JIT) — v2.0 « pro »
================================================================================
Lancement (depuis la racine du projet) :

    python3 gui/serveur.py

Puis ouvrir dans le navigateur (Windows) :  http://localhost:8765

Principe : cette page ne réimplémente RIEN du langage. Elle envoie le
code au vrai binaire ./build/mlang avec les options officielles
(--jit=off/on, --threshold, --osr-threshold, --stats) et affiche sa
sortie + ses statistiques — y compris la COMPARAISON des 3 modes.
Aucun fichier C n'est modifié.
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
DELAI_MAX = 10          # secondes par exécution (anti boucle infinie)
TAILLE_MAX_CODE = 20000  # caractères

PAGE = r"""<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="utf-8">
<title>mlang — VM + JIT · démonstration</title>
<style>
  * { box-sizing:border-box; }
  body { margin:0; font-family:Segoe UI, Arial, sans-serif; background:#1e1e1e; color:#d4d4d4; }
  header { background:linear-gradient(90deg,#252526,#1f3a52); padding:14px 22px; border-bottom:2px solid #0e639c; }
  header h1 { margin:0; font-size:22px; color:#fff; }
  header h1 .v { font-size:12px; background:#0e639c; border-radius:10px; padding:2px 9px; vertical-align:middle; }
  header p { margin:4px 0 0; color:#9cdcfe; font-size:13px; }
  main { display:flex; gap:16px; padding:16px 22px; flex-wrap:wrap; }
  .col { flex:1 1 480px; min-width:0; }
  h2 { font-size:15px; color:#dcdcdc; margin:10px 0 8px; }
  select, input[type=number] { background:#111; color:#d4d4d4; border:1px solid #3c3c3c; border-radius:6px; }
  select { width:100%; padding:7px; margin-bottom:8px; font-size:14px; }
  /* --- éditeur de code (numéros de lignes + coloration) --- */
  .editor { display:flex; background:#111; border:1px solid #3c3c3c; border-radius:6px; overflow:hidden; }
  .gutter { padding:10px 8px 10px 10px; text-align:right; color:#6e7681; background:#161616;
            font:14px/1.5 Consolas, monospace; user-select:none; overflow:hidden; white-space:pre; }
  .codewrap { position:relative; flex:1; }
  #hl, #code { margin:0; padding:10px; font:14px/1.5 Consolas, monospace; white-space:pre; }
  #hl { position:absolute; inset:0; overflow:auto; pointer-events:none; z-index:1; }
  #code { position:relative; z-index:2; width:100%; height:320px; background:transparent;
          color:transparent; caret-color:#fff; border:0; outline:none; resize:vertical; overflow:auto; }
  .tk-kw { color:#c586c0; } .tk-num { color:#b5cea8; } .tk-str { color:#ce9178; } .tk-com { color:#6a9955; font-style:italic; }
  /* --- contrôles --- */
  .modes { margin:10px 0 4px; font-size:14px; }
  .modes label { display:block; margin:5px 0; cursor:pointer; }
  .opts { display:flex; gap:14px; align-items:center; margin:8px 0 12px; font-size:13px; color:#9cdcfe; flex-wrap:wrap; }
  .opts input { width:86px; padding:4px 6px; }
  .boutons { display:flex; gap:10px; flex-wrap:wrap; }
  button { border:0; border-radius:6px; padding:11px 22px; font-size:16px; cursor:pointer; color:#fff; }
  #btn { background:#0e639c; } #btn:hover:not(:disabled){ background:#1177bb; }
  #btn_cmp { background:#2e7d32; } #btn_cmp:hover:not(:disabled){ background:#388e3c; }
  button:disabled { opacity:.6; cursor:wait; }
  /* --- résultats --- */
  .badges { display:flex; gap:10px; margin-bottom:10px; flex-wrap:wrap; }
  .carte { background:#252526; border:1px solid #3c3c3c; border-radius:8px; padding:8px 14px; text-align:center; min-width:110px; }
  .carte .titre { font-size:11px; color:#9cdcfe; text-transform:uppercase; letter-spacing:.5px; }
  .carte .valeur { font-size:22px; font-weight:bold; color:#fff; }
  .badge.ok { color:#4ec9b0 !important; } .badge.err { color:#f44747 !important; }
  pre { background:#111; border:1px solid #3c3c3c; border-radius:6px; padding:10px; min-height:70px;
        max-height:320px; overflow:auto; white-space:pre-wrap; font-family:Consolas, monospace; font-size:13px; margin:0 0 6px; }
  /* --- comparaison --- */
  table { width:100%; border-collapse:collapse; font-size:14px; }
  td, th { padding:7px 8px; border-bottom:1px solid #333; text-align:left; vertical-align:middle; }
  th { color:#9cdcfe; font-size:12px; text-transform:uppercase; }
  .barreFond { background:#2a2a2a; border-radius:4px; height:16px; min-width:120px; }
  .barre { height:16px; border-radius:4px; transition:width .6s ease; }
  .b-interp { background:#808080; } .b-jit { background:#1177bb; } .b-osr { background:#4caf50; }
  .gain { font-weight:bold; color:#4ec9b0; }
  .verdict { font-size:13px; margin-top:6px; }
  /* --- historique --- */
  #hist { font-size:12.5px; color:#bdbdbd; }
  #hist div { padding:3px 0; border-bottom:1px dashed #333; font-family:Consolas, monospace; }
  footer { padding:6px 22px 18px; color:#808080; font-size:12px; }
  code { color:#ce9178; }
</style>
</head>
<body>
<header>
  <h1>mlang — machine virtuelle + compilation JIT <span class="v">GUI v2.0</span></h1>
  <p>Interpréteur de bytecode · profiler (appels + boucles) · JIT libgccjit · OSR — Samuel & Amine </p>
</header>
<main>
  <div class="col">
    <h2>Programme mlang</h2>
    <select id="exemple" onchange="chargerExemple()">
      <option value="boucle">🚀 Boucle chaude — somme(3 000 000) : l'OSR en action</option>
      <option value="appels">📞 Fonction appelée 300 fois : le JIT par appels</option>
      <option value="fib">⚡ Fibonacci récursif</option>
      <option value="chaines">🧵 Chaînes — types dynamiques</option>
      <option value="deopt">🧪 Dé-optimisation — fonction « chaînes » refusée par le JIT</option>
      <option value="divzero">⚠️ Division par zéro — erreur propre</option>
    </select>
    <div class="editor">
      <div class="gutter" id="gutter">1</div>
      <div class="codewrap">
        <pre id="hl" aria-hidden="true"></pre>
        <textarea id="code" spellcheck="false"></textarea>
      </div>
    </div>
    <div class="modes">
      <label><input type="radio" name="mode" value="interp" checked> 🐢 Interprété seul <code>--jit=off</code></label>
      <label><input type="radio" name="mode" value="jit"> ⚡ JIT par appels (sans OSR) <code>--jit=on --osr-threshold=0</code></label>
      <label><input type="radio" name="mode" value="osr"> 🚀 JIT + OSR <code>--jit=on</code></label>
    </div>
    <div class="opts">
      <label>Seuil appels <input type="number" id="s_app" value="100" min="1" max="1000000"></label>
      <label>Seuil OSR (itérations) <input type="number" id="s_osr" value="1000" min="1" max="1000000"></label>
    </div>
    <div class="boutons">
      <button id="btn" onclick="executer()">▶ Exécuter</button>
      <button id="btn_cmp" onclick="comparer()">📊 Comparer les 3 modes</button>
    </div>
    <h2>historique des exécutions</h2>
    <div id="hist"><i>(aucune pour le moment)</i></div>
  </div>
  <div class="col">
    <h2>Résultat</h2>
    <div class="badges">
      <div class="carte"><div class="titre">⏱ Temps total</div><div class="valeur" id="b_temps">—</div></div>
      <div class="carte"><div class="titre">⚙ Fonctions JIT</div><div class="valeur" id="b_jit">—</div></div>
      <div class="carte"><div class="titre">Code de sortie</div><div class="valeur badge" id="b_code">—</div></div>
    </div>
    <div id="zone_cmp" style="display:none">
      <h2>📊 Comparaison des 3 modes (même programme)</h2>
      <table><thead><tr><th>Mode</th><th>Temps</th><th>Fonctions JIT</th><th>Barre</th><th>Gain</th></tr></thead>
      <tbody id="corps_cmp"></tbody></table>
      <div class="verdict" id="verdict_cmp"></div>
    </div>
    <h2>Sortie du programme</h2>
    <pre id="sortie">(clique sur « Exécuter » ou « Comparer les 3 modes »)</pre>
    <h2>Messages + statistiques de la VM</h2>
    <pre id="stats"></pre>
  </div>
</main>
<footer>Astuce jury : « Comparer les 3 modes » sur la Boucle chaude → interprété ≈ JIT par appels (aveugle : 1 seul appel) mais JIT+OSR ≈ ×3,9. Sur « appelée 300 fois », c'est le JIT par appels qui gagne et l'OSR ne sert à rien : les deux profilers se complètent.</footer>
<script>
const EXEMPLES = {
  boucle: "// boucle chaude : somme de 1 a 3 000 000 (1 seul appel !)\n// -> le JIT par appels ne voit rien, l'OSR oui.\nfn somme(n) {\n    let i = 0;\n    let s = 0;\n    while (i <= n) {\n        s = s + i;\n        i = i + 1;\n    }\n    return s;\n}\nprint(somme(3000000));",
  appels: "// la meme fonction appelee 300 fois (boucles courtes)\n// -> le JIT par appels gagne, l'OSR ne sert a rien ici.\nfn somme(n) {\n    let i = 0;\n    let s = 0;\n    while (i <= n) {\n        s = s + i;\n        i = i + 1;\n    }\n    return s;\n}\nlet total = 0;\nlet k = 0;\nwhile (k < 300) {\n    total = somme(200);\n    k = k + 1;\n}\nprint(total);",
  fib: "// recursion : des dizaines de milliers d'appels\nfn fib(n) {\n    if (n < 2) { return n; }\n    return fib(n - 1) + fib(n - 2);\n}\nprint(fib(25));",
  chaines: "// types dynamiques (v0.2) : concatenation de chaines\nlet a = \"Machine\";\nlet b = \"virtuelle\";\nprint(a + \" \" + b + \" + JIT\");",
  deopt: "// de-optimisation : saluer() est appelee 151 fois,\n// mais elle manipule des chaines -> le scan du JIT la\n// refuse, elle reste interpretee (0 fonction JIT).\nfn saluer(nom) {\n    return \"Bonjour \" + nom;\n}\nlet i = 0;\nwhile (i < 150) {\n    saluer(\"mlang\");\n    i = i + 1;\n}\nprint(saluer(\"le jury\"));",
  divzero: "// erreur d'execution propre (message + ligne + code 1)\nprint(1);\nlet zero = 0;\nprint(10 / zero);\nprint(2);"
};
const codeEl = document.getElementById('code');
const hlEl = document.getElementById('hl');
const gutterEl = document.getElementById('gutter');
function echapper(s){ return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;'); }
function colorer(src){
  const RE = /(\/\/[^\n]*)|("[^"\n]*")|\b(fn|let|if|else|while|return|print|true|false|nil)\b|\b(\d+(?:\.\d+)?)\b/g;
  let out='', last=0, m;
  while((m=RE.exec(src))!==null){
    out += echapper(src.slice(last,m.index));
    if(m[1]) out += '<span class="tk-com">'+echapper(m[1])+'</span>';
    else if(m[2]) out += '<span class="tk-str">'+echapper(m[2])+'</span>';
    else if(m[3]) out += '<span class="tk-kw">'+m[3]+'</span>';
    else if(m[4]) out += '<span class="tk-num">'+m[4]+'</span>';
    last = m.index + m[0].length;
  }
  out += echapper(src.slice(last));
  return out + '\n';
}
function majEditeur(){
  hlEl.innerHTML = colorer(codeEl.value);
  const n = codeEl.value.split('\n').length;
  let g=''; for(let i=1;i<=n;i++) g += i + (i<n?'\n':'');
  gutterEl.textContent = g;
  syncScroll();
}
function syncScroll(){
  hlEl.scrollTop = codeEl.scrollTop; hlEl.scrollLeft = codeEl.scrollLeft;
  gutterEl.scrollTop = codeEl.scrollTop;
}
function chargerExemple(){ codeEl.value = EXEMPLES[document.getElementById('exemple').value]; majEditeur(); }
function seuils(){
  let a = parseInt(document.getElementById('s_app').value,10);
  let o = parseInt(document.getElementById('s_osr').value,10);
  return { seuil_appels: (a>0?a:100), seuil_osr: (o>0?o:1000) };
}
function heure(){ const d=new Date(); return d.toTimeString().slice(0,8); }
function historique(ligne){
  const h = document.getElementById('hist');
  if(h.textContent.indexOf('aucune')>=0) h.innerHTML='';
  const div = document.createElement('div');
  div.textContent = heure() + ' · ' + ligne;
  h.insertBefore(div, h.firstChild);
  while(h.children.length>8) h.removeChild(h.lastChild);
}
function afficher(d){
  document.getElementById('sortie').textContent = d.stdout ? d.stdout : '(aucune sortie)';
  document.getElementById('stats').textContent = d.stderr || '';
  document.getElementById('b_temps').textContent = (d.temps_ms===null||d.temps_ms===undefined) ? '—' : d.temps_ms + ' ms';
  document.getElementById('b_jit').textContent = (d.fonctions_jit===null||d.fonctions_jit===undefined) ? '—' : d.fonctions_jit;
  const bc = document.getElementById('b_code');
  bc.textContent = d.code; bc.className = 'valeur badge ' + (d.code===0 ? 'ok' : 'err');
}
const NOMS = { interp:'🐢 Interprété', jit:'⚡ JIT appels', osr:'🚀 JIT + OSR' };
async function poster(url, corps){
  const r = await fetch(url, { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(corps) });
  return await r.json();
}
async function executer(){
  const b1=document.getElementById('btn'), b2=document.getElementById('btn_cmp');
  b1.disabled=true; b2.disabled=true; b1.textContent='⏳ Exécution…';
  const mode = document.querySelector('input[name=mode]:checked').value;
  try {
    const d = await poster('/run', Object.assign({ code: codeEl.value, mode: mode }, seuils()));
    afficher(d);
    historique(NOMS[mode] + ' · ' + (d.temps_ms===null?'—':d.temps_ms+' ms') + ' · code ' + d.code);
  } catch(e){ afficher({ stdout:'Erreur : serveur injoignable.\nRelance : python3 gui/serveur.py', stderr:'', code:-1, temps_ms:null, fonctions_jit:null }); }
  b1.disabled=false; b2.disabled=false; b1.textContent='▶ Exécuter';
}
async function comparer(){
  const b1=document.getElementById('btn'), b2=document.getElementById('btn_cmp');
  b1.disabled=true; b2.disabled=true; b2.textContent='⏳ Comparaison…';
  try {
    const d = await poster('/compare', Object.assign({ code: codeEl.value }, seuils()));
    const res = d.resultats || [];
    const tmax = Math.max.apply(null, res.map(function(r){ return r.temps_ms||0; }).concat([0.001]));
    const t0 = (res[0] && res[0].temps_ms) ? res[0].temps_ms : null;
    let html='';
    res.forEach(function(r){
      const largeur = r.temps_ms ? Math.max(3, Math.round(100*r.temps_ms/tmax)) : 3;
      const gain = (t0 && r.temps_ms) ? ('×' + (t0/r.temps_ms).toFixed(2).replace('.',',')) : '—';
      html += '<tr><td>' + NOMS[r.mode] + '</td><td>' + (r.temps_ms===null?'—':r.temps_ms+' ms') + '</td>' +
              '<td>' + (r.fonctions_jit===null?'—':r.fonctions_jit) + '</td>' +
              '<td><div class="barreFond"><div class="barre b-' + r.mode + '" style="width:' + largeur + '%"></div></div></td>' +
              '<td class="gain">' + gain + '</td></tr>';
    });
    document.getElementById('corps_cmp').innerHTML = html;
    const same = res.length===3 && res[0].stdout===res[1].stdout && res[1].stdout===res[2].stdout;
    document.getElementById('verdict_cmp').textContent = same
      ? '✅ Sorties identiques dans les 3 modes (le JIT ne change jamais le résultat).'
      : '⚠️ Les sorties diffèrent selon le mode — voir ci-dessous.';
    document.getElementById('zone_cmp').style.display = 'block';
    if(res[2]) afficher(res[2]);
    historique('📊 Comparaison 3 modes · ' + res.map(function(r){ return (r.temps_ms===null?'—':r.temps_ms); }).join(' / ') + ' ms');
  } catch(e){ afficher({ stdout:'Erreur : serveur injoignable.\nRelance : python3 gui/serveur.py', stderr:'', code:-1, temps_ms:null, fonctions_jit:null }); }
  b1.disabled=false; b2.disabled=false; b2.textContent='📊 Comparer les 3 modes';
}
codeEl.addEventListener('input', majEditeur);
codeEl.addEventListener('scroll', syncScroll);
chargerExemple();
</script>
</body>
</html>
"""


def _seuil(valeur, defaut):
    try:
        return max(1, min(1000000, int(valeur)))
    except (TypeError, ValueError):
        return defaut


def drapeaux(mode, seuil_appels, seuil_osr):
    """Options officielles du binaire pour chaque mode."""
    if mode == "interp":
        return ["--jit=off", "--stats"]
    if mode == "jit":
        return ["--jit=on", "--threshold=%d" % seuil_appels,
                "--osr-threshold=0", "--stats"]
    return ["--jit=on", "--threshold=%d" % seuil_appels,
            "--osr-threshold=%d" % seuil_osr, "--stats"]


def executer(code, mode, seuil_appels, seuil_osr):
    """Écrit le programme, lance ./build/mlang, renvoie les résultats."""
    if len(code) > TAILLE_MAX_CODE:
        return {"mode": mode, "stdout": "",
                "stderr": "Programme trop long (max %d caractères)." % TAILLE_MAX_CODE,
                "code": -1, "temps_ms": None, "fonctions_jit": None}
    if not os.path.isfile(MLANG):
        return {"mode": mode, "stdout": "",
                "stderr": "Binaire introuvable : " + MLANG +
                          "\nCompile d'abord : cmake --build build",
                "code": -1, "temps_ms": None, "fonctions_jit": None}
    with open(PROG_TMP, "w", encoding="utf-8") as f:
        f.write(code)
    commande = [MLANG] + drapeaux(mode, seuil_appels, seuil_osr) + [PROG_TMP]
    debut = time.perf_counter()
    try:
        p = subprocess.run(commande, capture_output=True, text=True,
                           errors="replace", timeout=DELAI_MAX, cwd=RACINE)
    except subprocess.TimeoutExpired:
        return {"mode": mode, "stdout": "",
                "stderr": "(délai dépassé : %d s — boucle infinie ?)" % DELAI_MAX,
                "code": -1, "temps_ms": None, "fonctions_jit": None}
    duree = round((time.perf_counter() - debut) * 1000, 1)
    m = re.search(r"temps total\s*:\s*([0-9]+(?:[.,][0-9]+)?)\s*ms", p.stderr)
    temps = float(m.group(1).replace(",", ".")) if m else duree
    m2 = re.search(r"fonctions JIT\s*:\s*([0-9]+)", p.stderr)
    fjit = int(m2.group(1)) if m2 else None
    return {"mode": mode, "stdout": p.stdout, "stderr": p.stderr,
            "code": p.returncode, "temps_ms": temps, "fonctions_jit": fjit}


class Gestionnaire(BaseHTTPRequestHandler):
    def _envoyer(self, code_http, type_contenu, corps):
        self.send_response(code_http)
        self.send_header("Content-Type", type_contenu + "; charset=utf-8")
        self.send_header("Content-Length", str(len(corps)))
        self.end_headers()
        self.wfile.write(corps)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            self._envoyer(200, "text/html", PAGE.encode("utf-8"))
        else:
            self.send_error(404)

    def do_POST(self):
        try:
            taille = int(self.headers.get("Content-Length", 0))
            d = json.loads(self.rfile.read(taille).decode("utf-8"))
            code = str(d.get("code", ""))
            s_app = _seuil(d.get("seuil_appels"), 100)
            s_osr = _seuil(d.get("seuil_osr"), 1000)
            if self.path == "/run":
                mode = str(d.get("mode", "interp"))
                if mode not in ("interp", "jit", "osr"):
                    mode = "interp"
                resultat = executer(code, mode, s_app, s_osr)
            elif self.path == "/compare":
                resultat = {"resultats": [
                    executer(code, m, s_app, s_osr)
                    for m in ("interp", "jit", "osr")]}
            else:
                self.send_error(404)
                return
        except Exception as exc:  # ne jamais faire tomber le serveur
            resultat = {"stdout": "", "stderr": "Erreur serveur : %s" % exc,
                        "code": -1, "temps_ms": None, "fonctions_jit": None,
                        "resultats": []}
        self._envoyer(200, "application/json",
                      json.dumps(resultat).encode("utf-8"))

    def log_message(self, *args):  # terminal propre
        pass


def main():
    os.makedirs(os.path.join(RACINE, "gui"), exist_ok=True)
    serveur = HTTPServer(("0.0.0.0", PORT), Gestionnaire)
    print("mlang GUI v2.0 — serveur démarré")
    print("Ouvre dans ton navigateur :  http://localhost:%d" % PORT)
    print("(Ctrl+C pour arrêter)")
    try:
        serveur.serve_forever()
    except KeyboardInterrupt:
        print("\nArrêt du serveur.")


if __name__ == "__main__":
    main()
