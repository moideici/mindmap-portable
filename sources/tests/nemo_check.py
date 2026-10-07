"""Pont réseau Nemo de mindmap-cli, contre un FAUX serveur Nemo local : export --all, remplacement après renommage,
publication (fichier + .txt), file d'attente hors-ligne, suppression de l'ancien cours, lecture de la maîtrise.
Usage : python nemo_check.py <mindmap-cli.exe>"""
import json
import os
import subprocess
import sys
import tempfile
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

CLI = str(Path(sys.argv[1]).resolve())
failures = []
seen = []  # (méthode, chemin décodé, jeton, corps)
MASTERY = {"Causes": "acquis", "Conséquences": "a-revoir"}


class Fake(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def handle_any(self):
        body = self.rfile.read(int(self.headers.get("content-length") or 0)).decode()
        path = urllib.parse.unquote(self.path)
        seen.append((self.command, path, self.headers.get("authorization"), body))
        if self.headers.get("authorization") != "Bearer jeton-test":
            self.send_response(401); self.end_headers(); return
        payload = b"{}"
        if path == "/api/mastery/lookup":
            titles = json.loads(body)["titles"]
            payload = json.dumps({"items": [{"title": t, "itemId": "n1", "status": MASTERY.get(t, "inconnu"), **({"box": 3, "mastery": 1} if t in MASTERY else {})} for t in titles]}).encode()
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    do_PUT = do_POST = do_DELETE = handle_any


server = HTTPServer(("127.0.0.1", 0), Fake)
port = server.server_address[1]
threading.Thread(target=server.serve_forever, daemon=True).start()


def run(*args, stdin=None, expect=0, token="jeton-test"):
    env = {**os.environ, "NEMO_TOKEN": token}
    done = subprocess.run([CLI, *map(str, args)], input=stdin.encode() if stdin else None, capture_output=True, env=env)
    if done.returncode != expect:
        failures.append(f"{args}: code {done.returncode} au lieu de {expect}\n{done.stderr.decode(errors='replace')}")
    return done.stdout.decode(errors="replace"), done.stderr.decode(errors="replace")


def check(condition, message):
    if not condition:
        failures.append(message)


with tempfile.TemporaryDirectory(prefix="mindmap hist géo ") as tmp:
    work = Path(tmp)
    cartes = work / "cartes"
    cartes.mkdir()
    cours = work / "cours Nemo"
    cours.mkdir()
    # deux cartes racines, dont une avec une sous-carte
    revo = cartes / "Révolution.mindmap"
    run("new", revo, "--title", "Révolution")
    out, _ = run("add", revo, "--title", "1789", "--json")
    run("submap-new", revo, "--id", json.loads(out)["id"], "--outline", "-", stdin="- Causes\n- Conséquences\n")
    run("new", cartes / "Empire.mindmap", "--title", "Empire")
    run("nemo-config", "--workspace", work, "--courses", cours)

    # --- export --all : les sous-cartes ne sont pas des cours à part
    out, _ = run("nemo-export", "--all", cartes, "--workspace", work, "--json")
    files = sorted(Path(p).name for p in json.loads(out)["files"])
    check(files == ["Empire (carte mentale).md", "Révolution (carte mentale).md"], f"cours exportés : {files}")
    check(len(list(cours.glob("*.md"))) == 2, "deux cours attendus dans le dossier Nemo")
    other = cours / "Mon cours Word.md"
    other.write_text("à moi", encoding="utf-8")

    # --- renommage de la carte : l'ancien cours généré disparaît, un fichier étranger reste
    run("set", revo, "--id", 1, "--title", "Révolution française")
    run("nemo-export", revo, "--workspace", work)
    names = sorted(p.name for p in cours.glob("*.md"))
    check("Révolution (carte mentale).md" not in names and "Révolution française (carte mentale).md" in names, f"après renommage : {names}")
    check(other.exists(), "un fichier qui n'est pas généré par MindMap ne doit jamais être supprimé")
    run("nemo-export", "--all", cartes, "--workspace", work)
    (cartes / "Empire.mindmap").unlink()
    run("nemo-export", revo, "--workspace", work)
    check(not (cours / "Empire (carte mentale).md").exists(), "la carte supprimée doit retirer son cours généré")

    # --- export automatique : réglage mémorisé
    out, _ = run("nemo-config", "--workspace", work, "--auto-export", "on", "--json")
    check(json.loads(out)["autoExport"] is True, "autoExport doit être activé")
    run("nemo-config", "--workspace", work, "--auto-export", "peut-être", expect=2)

    # --- réseau : sans configuration, refus clair
    run("nemo-push", revo, "--workspace", work, expect=1)
    run("nemo-remote", "--workspace", work, "--url", "http://exemple.org", "--subject", "hist géo", expect=1)  # http refusé hors localhost
    run("nemo-remote", "--workspace", work, "--url", f"http://127.0.0.1:{port}", "--subject", "hist géo", "--scope", "class")

    # --- publication : le fichier ET son .txt compagnon, sous <matière>/cartes/
    out, _ = run("nemo-push", revo, "--workspace", work, "--json")
    check(json.loads(out)["sent"] == 1, f"publication : {out}")
    puts = [s for s in seen if s[0] == "PUT"]
    keys = [s[1] for s in puts]
    base = "/api/admin/classe/courses/hist géo/cartes/Révolution française (carte mentale).md"
    check(keys == [base, base + ".txt"], f"clés publiées : {keys}")
    check(all(s[2] == "Bearer jeton-test" for s in puts), "le jeton doit accompagner chaque requête")
    check("Causes" in puts[0][3] and "Conséquences" in puts[1][3], "le cours complet (sous-cartes comprises) doit être envoyé")

    # --- jeton refusé
    seen.clear()
    _, err = run("nemo-push", revo, "--workspace", work, expect=1, token="mauvais")
    check("refuse le jeton" in err, f"message de refus : {err}")

    # --- renommage APRES publication : l'ancien cours est supprimé sur Nemo
    seen.clear()
    run("set", revo, "--id", 1, "--title", "La Révolution")
    run("nemo-push", revo, "--workspace", work)
    deletes = [s[1] for s in seen if s[0] == "DELETE"]
    check(deletes == [base, base + ".txt"], f"suppression de l'ancien cours : {deletes}")

    # --- hors-ligne : mise en file d'attente, puis envoi au retour du réseau
    run("nemo-remote", "--workspace", work, "--url", "http://127.0.0.1:9", "--subject", "hist géo")
    out, _ = run("nemo-push", revo, "--workspace", work, "--json")
    check(json.loads(out)["queued"] == 1, f"file d'attente : {out}")
    check(len(list((work / ".mindmap-data" / "nemo-queue").glob("*"))) == 1, "le cours doit attendre dans nemo-queue")
    run("nemo-remote", "--workspace", work, "--url", f"http://127.0.0.1:{port}", "--subject", "hist géo")
    seen.clear()
    out, _ = run("nemo-push", "--flush", "--workspace", work, "--json")
    check(json.loads(out)["flushed"] == 1, f"vidage de la file : {out}")
    check(not list((work / ".mindmap-data" / "nemo-queue").glob("*")), "la file doit être vide")

    # --- maîtrise : lecture seule, par titre d'idée
    seen.clear()
    out, _ = run("nemo-mastery", revo, "--workspace", work, "--json")
    ideas = {i["title"]: i["status"] for i in json.loads(out)["ideas"]}
    check(ideas == {"1789": "inconnu", "Causes": "acquis", "Conséquences": "a-revoir"}, f"maîtrise : {ideas}")
    sent = json.loads([s for s in seen if s[1] == "/api/mastery/lookup"][0][3])
    check(sent["subject"] == "hist géo" and sorted(sent["titles"]) == ["1789", "Causes", "Conséquences"], f"requête de maîtrise : {sent}")
    before = revo.read_bytes()
    text, _ = run("nemo-mastery", revo, "--workspace", work)
    check("✓" in text and "↻" in text and revo.read_bytes() == before, "lecture seule : la carte ne doit pas changer")

server.shutdown()
if failures:
    print("ÉCHECS :\n" + "\n".join(failures))
    sys.exit(1)
print("nemo_check : OK")
