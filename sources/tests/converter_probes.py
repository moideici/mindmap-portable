"""Reproductions des limites, sans correction du convertisseur."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CONVERTER = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT.parents[1] / "transpiler"
sys.path.insert(0, str(CONVERTER))
from py2x import transpile, Unsupported

cases = {
    "json": 'import json\ndef decode(s: str) -> str:\n    return json.loads(s)["title"]\n',
    "files": 'def save(s: str) -> None:\n    with open("map.json", "w", encoding="utf-8") as f:\n        f.write(s)\n',
    "pathlib": 'from pathlib import Path\ndef location() -> str:\n    return str(Path(__file__).parent)\n',
    "tkinter": 'import tkinter as tk\ndef ui() -> None:\n    root = tk.Tk()\n    root.mainloop()\n',
    "deepcopy": 'import copy\ndef clone(xs: list[int]) -> list[int]:\n    return copy.deepcopy(xs)\n',
    "weakref": 'import weakref\nclass Node:\n    def __init__(self) -> None:\n        self.n = 1\ndef observe(n: Node) -> bool:\n    return weakref.ref(n)() is not None\n',
    "unicode_len": 'def main() -> None:\n    print(len("é🗺"))\nif __name__ == "__main__":\n    main()\n',
    "unicode_lower": 'def main() -> None:\n    print("ÉCOLE".lower())\nif __name__ == "__main__":\n    main()\n',
    "library_entry": 'def twice(n: int) -> int:\n    return n * 2\n',
    "cycles": 'from typing import Optional\nclass Link:\n    def __init__(self) -> None:\n        self.other: Optional[Link] = None\n    def connect(self, other: "Link") -> None:\n        self.other = other\n',
}
work = ROOT / "build-final/probes"
work.mkdir(parents=True, exist_ok=True)
results = {}
for name, source in cases.items():
    (work / (name + ".py")).write_text(source, encoding="utf-8")
    try:
        code = transpile(source, "cpp", strict=True)
    except Unsupported as e:
        results[name] = {"accepted": False, "error": str(e), "source": source}
        continue
    result = {"accepted": True, "source": source}
    if name == "library_entry":
        result["generates_main"] = "int main()" in code
    elif name == "cycles":
        code = '#define main generated_main\n' + code + '\n#undef main\nint main(){std::weak_ptr<Link> w;{auto x=std::make_shared<Link>();x->connect(x);w=x;}std::cout << (w.expired()?"released":"retained-cycle") << "\\n";if(auto retained=w.lock())retained->other.reset();std::cout << (w.expired()?"released-after-breaking":"still-alive") << "\\n";}\n'
    if name.startswith("unicode") or name == "cycles":
        path = work / (name + ".cpp")
        path.write_text(code, encoding="utf-8")
        executable = work / (name + ".exe")
        subprocess.run(["g++", "-std=c++20", "-O1", "-static", str(path), "-o", str(executable)], check=True, timeout=120)
        result["cpp_output"] = subprocess.run([str(executable)], capture_output=True, encoding="utf-8", check=True, timeout=20).stdout
        if name.startswith("unicode"):
            env = dict(os.environ, PYTHONIOENCODING="utf-8")
            result["python_output"] = subprocess.run([sys.executable, str(work / (name + ".py"))], env=env, capture_output=True, encoding="utf-8", check=True, timeout=20).stdout
            result["equal"] = result["cpp_output"] == result["python_output"]
    results[name] = result
provenance = json.loads((ROOT / "generated/provenance.json").read_text(encoding="utf-8"))
current = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((CONVERTER / "py2x").glob("*.py"))}
results["converter_unmodified"] = current == provenance["converter_sha256"]
(ROOT / "build-final/converter-probes.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps(results, ensure_ascii=False, indent=2))
