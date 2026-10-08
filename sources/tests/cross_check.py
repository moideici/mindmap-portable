"""Tests croisés MindMap ↔ Nemo : les MÊMES cartes de référence (tests/fixtures/mindmap-cross, copiées telles quelles dans
nemo-https/test/fixtures/mindmap) doivent donner le MÊME cours Markdown (nemo-export ici, src/extract/mindmap.js là-bas)
et le MÊME arbre JSON (show --json ici, tool MCP read_mindmap là-bas). Les sorties attendues sont dans attendu/.
Usage : python cross_check.py <mindmap-cli.exe> [--regenerate]
  --regenerate réécrit attendu/ et SHA256SUMS depuis la CLI (à relire, puis recopier tout le dossier dans nemo-https)."""
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

CLI = str(Path(sys.argv[1]).resolve())
REGENERATE = "--regenerate" in sys.argv
FIXTURES = Path(__file__).resolve().parent / "fixtures" / "mindmap-cross"
ROOTS = ["Histoire.mindmap", "Manquante.mindmap", "Ancienne.mindmap", "boucle/A.mindmap"]  # cartes exportées comme cours
failures = []


def run(*args):
    done = subprocess.run([CLI, *map(str, args)], capture_output=True)
    if done.returncode != 0:
        failures.append(f"{args}: code {done.returncode}\n{done.stderr.decode(errors='replace')}")
    return done.stdout.decode()


def text(path):
    return path.read_bytes().decode("utf-8").replace("\r\n", "\n")  # git peut convertir les fins de ligne à l'extraction


def sums():
    """Empreinte de chaque fichier (fins de ligne normalisées) : même valeur dans les deux dépôts tant que les copies sont identiques."""
    lines = []
    # tri ordinal des chemins "/" (pas celui de WindowsPath, insensible à la casse) : même ordre que le test JS
    for f in sorted((p for p in FIXTURES.rglob("*") if p.is_file() and p.name != "SHA256SUMS"), key=lambda p: p.relative_to(FIXTURES).as_posix()):
        lines.append(f"{hashlib.sha256(text(f).encode('utf-8')).hexdigest()}  {f.relative_to(FIXTURES).as_posix()}")
    return "\n".join(lines) + "\n"


def compare(path, actual):
    if REGENERATE:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(actual, encoding="utf-8", newline="\n")
    elif not path.exists() or text(path) != actual:
        failures.append(f"{path.relative_to(FIXTURES)} : sortie différente\n--- obtenu ---\n{actual}")


before = {p: p.read_bytes() for p in FIXTURES.rglob("*.mindmap")}
with tempfile.TemporaryDirectory(prefix="mindmap croisé ") as tmp:
    for root in ROOTS:
        out = json.loads(run("nemo-export", FIXTURES / root, "--to", tmp, "--json"))
        compare(FIXTURES / "attendu" / (root + ".md.txt"), Path(out["file"]).read_bytes().decode("utf-8"))
for f in sorted(FIXTURES.rglob("*.mindmap")):
    tree = json.loads(run("show", f, "--json"))
    tree["file"] = f.relative_to(FIXTURES).as_posix()  # le chemin local n'a pas de sens ailleurs : chemin relatif au dossier des cartes
    compare(FIXTURES / "attendu" / (tree["file"] + ".json"), json.dumps(tree, ensure_ascii=False, indent=1) + "\n")
if any(p.read_bytes() != b for p, b in before.items()):
    failures.append("exporter ou lire une carte ne doit jamais la modifier (identifiants compris)")
compare(FIXTURES / "SHA256SUMS", sums())

if failures:
    print("ÉCHECS :\n" + "\n".join(failures))
    sys.exit(1)
print("cross_check : " + ("attendu/ régénéré" if REGENERATE else "OK"))
