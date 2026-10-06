"""Comparaison independante du modele Python et du modele C++ genere."""
from pathlib import Path
import subprocess
import sys
import random
import json

ROOT = Path(__file__).resolve().parents[1]
compiler = sys.argv[1] if len(sys.argv) > 1 else "g++"
converter = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else ROOT.parents[1] / "transpiler"
source = (ROOT / "python/mindmap_core.py").read_text(encoding="utf-8")
drivers = []
for seed in range(8):
    rng = random.Random(seed)
    lines = ["def differential_run() -> None:", f'    mind = MindMap("seed-{seed}")']
    for i in range(80):
        parent = rng.randint(1, i+1)
        lines.append(f'    mind.add({parent}, "idea-{seed}-{i}")')
    for i in range(100):
        node = rng.randint(2, 81)
        operation = rng.randrange(5)
        if operation == 0:
            lines.append(f"    mind.toggle({node})")
        elif operation == 1:
            lines.append(f"    mind.reveal({node})")
        elif operation == 2:
            lines.append(f"    mind.move_branch({node}, {float(rng.randint(-30,30))}, {float(rng.randint(-30,30))})")
        elif operation == 3:
            lines.append(f"    mind.set_color({node}, {rng.randint(0, 0xFFFFFF)}, True)")
        else:
            parent = rng.randint(1, 81)
            lines += ["    try:", f"        mind.reparent({node}, {parent})", "    except ValueError:", "        pass"]
    lines += ["    mind.layout()", "    print(mind.visible())", "    for node_id in mind.descendants(1):", "        node = mind.nodes[node_id]", "        print(node.id, node.parent, node.title, node.x, node.y, node.color, node.collapsed, node.children)", "differential_run()"]
    drivers.append("\n".join(lines))
work = ROOT / "build" / "differential"
work.mkdir(parents=True, exist_ok=True)
# Chaque driver a son nom propre: evitons toute redéfinition pour le convertisseur.
program = source.replace('if __name__ == "__main__":\n    core_demo()', "") + "\n"
for i, driver in enumerate(drivers):
    program += driver.replace("differential_run", f"differential_run_{i}") + "\n"
(work / "case.py").write_text(program, encoding="utf-8")
sys.path.insert(0, str(converter))
from py2x import transpile
(work / "case.cpp").write_text(transpile(program, "cpp", strict=True), encoding="utf-8")
subprocess.run([compiler, "-std=c++20", "-O1", "-static", str(work / "case.cpp"), "-o", str(work / "case.exe")], check=True, timeout=120)
expected = subprocess.run([sys.executable, str(work / "case.py")], capture_output=True, encoding="utf-8", check=True, timeout=30).stdout
actual = subprocess.run([str(work / "case.exe")], capture_output=True, encoding="utf-8", check=True, timeout=30).stdout
if actual != expected:
    (work / "python.txt").write_text(expected, encoding="utf-8")
    (work / "cpp.txt").write_text(actual, encoding="utf-8")
    raise SystemExit("Difference Python/C++: inspect build/differential")
result = {"seeds": 8, "adds": 640, "operations": 800, "lines": len(expected.splitlines()), "equal": True}
(ROOT / "build/differential-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(json.dumps(result))
