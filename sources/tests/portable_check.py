"""Executable seul dans un chemin accentue et CWD different: aucune DLL fournie."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
target = ROOT / "build-final" / "test clé USB" / "cartes"
target.mkdir(parents=True, exist_ok=True)
shutil.copy2(ROOT / "build-final/MindMap.exe", target / "MindMap.exe")
result = subprocess.run([str(target / "MindMap.exe"), "--smoke-test"], cwd=ROOT / "tests", timeout=40)
report = json.loads((target / "smoke-test-report.json").read_text(encoding="utf-8"))
assert result.returncode == 0 and report["ok"], report
data = json.loads((target / "test-carte.mindmap").read_text(encoding="utf-8"))
assert any(n["note"] == "1789\nUne note avec des accents." for n in data["nodes"])
assert not list(target.glob("*.dll"))
assert not list(target.glob("*.tmp"))
pe = subprocess.run([str(Path.home() / "scoop/apps/mingw/current/bin/objdump.exe"), "-p", str(target / "MindMap.exe")], capture_output=True, text=True, check=True)
imports = [line.split("DLL Name:",1)[1].strip() for line in pe.stdout.splitlines() if "DLL Name:" in line]
assert not any("libstdc++" in name or "libgcc" in name or "libwinpthread" in name for name in imports)
report.update({"exe_bytes": (target / "MindMap.exe").stat().st_size, "imports": imports,
    "relocated_unicode_path": True, "cwd_is_different": True, "bundled_dlls": 0,
    "exe_sha256": hashlib.sha256((target / "MindMap.exe").read_bytes()).hexdigest(),
    "physical_usb_tested": False})
(ROOT / "build-final/portable-result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
