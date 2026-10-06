"""Prepare une livraison portable; ne modifie ni le convertisseur ni les sources."""
from pathlib import Path
import hashlib
import json
import runpy
import shutil

ROOT = Path(__file__).resolve().parents[1]
DELIVERY = ROOT / "delivery"
DELIVERY.mkdir(exist_ok=True)
shutil.copy2(ROOT / "build-final/MindMap.exe", DELIVERY / "MindMap.exe")
for name in ("README.md", "RAPPORT_COMPLET.md"):
    shutil.copy2(ROOT / name, DELIVERY / name)
shutil.copytree(ROOT, DELIVERY / "sources", dirs_exist_ok=True,
    ignore=shutil.ignore_patterns("build*", "delivery", "__pycache__", "*.pyc", ".git", "README.md", "RAPPORT_COMPLET.md"))
proof = DELIVERY / "sources/tests/preuves"
proof.mkdir(parents=True, exist_ok=True)
shutil.copy2(ROOT / "build/differential-result.json", proof / "differential-result.json")
for name in ("converter-probes.json", "portable-result.json"):
    shutil.copy2(ROOT / "build-final" / name, proof / name)
portable = ROOT / "build-final/test clé USB/cartes"
for name in ("test-interface.png", "test-carte.png", "test-carte.svg", "test-carte.mindmap", "test-plan.md", "smoke-test-report.json"):
    shutil.copy2(portable / name, proof / name)
shutil.copy2(ROOT / "build-final/Testing/Temporary/LastTest.log", proof / "ctest.txt")
module = runpy.run_path(str(ROOT / "python/mindmap_core.py"))
mind = module["MindMap"]("Organiser mes révisions")
first = mind.add(1, "Comprendre le chapitre")
second = mind.add(1, "Mémoriser les repères")
third = mind.add(1, "S’entraîner")
for parent, title, note in [
    (first, "Définitions", "Reformuler les notions du cours avec ses propres mots."),
    (first, "Causes et conséquences", "Relier les événements et expliquer leurs effets."),
    (second, "Dates et lieux", "Ajouter les repères utiles du chapitre et les situer sur une frise ou une carte."),
    (second, "Acteurs", "Noter le rôle des personnes, groupes ou institutions."),
    (third, "Questions du cours", "Transformer les titres en questions, puis répondre sans regarder les notes."),
    (third, "Un exemple par idée", "Trouver un exemple concret pour illustrer chaque argument."),
]:
    node_id = mind.add(parent, title)
    mind.rename(node_id, title, note)
mind.nodes[1].note = "Carte d’exemple modifiable. Double-cliquez une idée pour la renommer. Tab ajoute un enfant."
mind.layout()
nodes = [dict(id=n.id, parent=n.parent, title=n.title, note=n.note, x=n.x, y=n.y,
              color=n.color, collapsed=n.collapsed) for n in (mind.nodes[i] for i in mind.descendants(1))]
example = dict(format="mindmap-portable", version=1, next_id=mind.next_id, selected=1,
               view=dict(zoom=.65, x=140, y=270), nodes=nodes)
(DELIVERY / "Exemple - révisions.mindmap").write_text(json.dumps(example, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
baseline = json.loads((ROOT / "generated/provenance.json").read_text(encoding="utf-8"))
current = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT.parents[1] / "transpiler/py2x").glob("*.py"))}
assert current == baseline["converter_sha256"], "Converter changed since project start"
assert hashlib.sha256((ROOT / "generated/mindmap_core.cpp").read_bytes()).hexdigest() == baseline["generated_sha256"]
manifest = {str(p.relative_to(DELIVERY)).replace("\\", "/"): hashlib.sha256(p.read_bytes()).hexdigest()
    for p in DELIVERY.rglob("*") if p.is_file() and p.name != "delivery-sha256.json"}
(proof / "delivery-sha256.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
print("Portable delivery:", DELIVERY)
print("Executable:", (DELIVERY / "MindMap.exe").stat().st_size, "bytes")
print("Converter source hashes unchanged")
