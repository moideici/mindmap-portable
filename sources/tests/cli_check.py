"""Bout en bout de mindmap-cli : création, édition, sous-cartes imbriquées, boucles et erreurs.
Usage : python cli_check.py <mindmap-cli.exe>  (dossier de test volontairement accentué)"""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

CLI = str(Path(sys.argv[1]).resolve())
failures = []


def run(*args, stdin=None, expect=0):
    done = subprocess.run([CLI, *map(str, args)], input=stdin.encode() if stdin else None, capture_output=True)
    if done.returncode != expect:
        failures.append(f"{args}: code {done.returncode} au lieu de {expect}\n{done.stderr.decode(errors='replace')}")
    return done.stdout.decode(), done.stderr.decode(errors="replace")


def check(condition, message):
    if not condition:
        failures.append(message)


NOTE = "ligne 1\nligne 2 avec \\n littéral"  # vrai retour à la ligne, puis antislash + n littéral

with tempfile.TemporaryDirectory(prefix="mindmap hist géo ") as tmp:
    folder = Path(tmp)
    main = folder / "Histoire.mindmap"
    run("new", main, "--title", "Histoire")
    out, _ = run("add", main, "--title", "Thème 1 : l'Europe", "--marker", "important", "--json")
    theme_id = json.loads(out)["id"]
    run("add", main, "--title", "Thème 2", "--note", "ligne 1\nligne 2", "--color", "#2e9e5b")
    run("add", main, "--title", "", expect=2)
    run("add", main, "--title", "Orpheline", "--parent", "999", expect=1)

    out, _ = run("submap-new", main, "--id", theme_id, "--outline", "-", "--json",
                 stdin="# x\n- Chapitre 1 : la Révolution\n  - 1789\n- Chapitre 2\n")
    theme = folder / json.loads(out)["relative"]
    check(theme.is_file() and theme.parent == folder / "Histoire", "sous-carte du thème absente ou mal placée")

    out, _ = run("show", theme, "--json")
    shown = json.loads(out)
    check(shown["root"]["title"] == "Thème 1 : l'Europe", "le sujet de la sous-carte doit reprendre le titre de l'idée")
    chapter_id = shown["root"]["children"][0]["id"]
    run("submap-new", theme, "--id", chapter_id, "--outline", "-", stdin="- Causes\n- Conséquences\n")
    run("submap-new", theme, "--id", chapter_id, expect=1)       # déjà une sous-carte
    run("submap-new", main, "--id", 1, expect=1)                  # le sujet central n'en a pas

    out, _ = run("tree", main, "--json")
    maps = json.loads(out)["maps"]
    check([m["depth"] for m in maps] == [0, 1, 2], f"hiérarchie inattendue : {maps}")
    check(all(m["status"] == "ok" for m in maps), "toutes les cartes doivent être valides")
    out, _ = run("outline", main, "--deep")
    check("  - Chapitre 1 : la Révolution\n    - Causes\n    - Conséquences\n" in out.replace("    - 1789\n", ""), f"plan complet : {out}")

    # Boucle : la carte du chapitre ne peut pas contenir l'une de ses ancêtres.
    chapter = next(folder.rglob("Chapitre 1*.mindmap"))
    run("submap-attach", chapter, "--id", 2, "--file", "../../Histoire.mindmap", expect=1)
    run("submap-attach", main, "--id", 3, "--file", "../hors.mindmap", expect=1)

    # Édition courante.
    run("set", main, "--id", theme_id, "--marker", "revoir", "--title", "Thème 1 modifié")
    run("link", main, "--from", 1, "--to", theme_id, "--label", "cause de")
    run("link", main, "--from", 1, "--to", theme_id, expect=1)
    run("unlink", main, "--from", 1, "--to", theme_id)
    run("move", main, "--id", theme_id, "--parent", theme_id, expect=1)
    run("set", main, "--id", theme_id, expect=2)                   # rien à modifier
    run("remove", main, "--id", 1, expect=1)
    out, _ = run("show", main)
    check("Thème 1 modifié" in out and "sous-carte" in out, f"show : {out}")

    # Batch : une seule écriture, références $nom, tout ou rien (y compris les fichiers de sous-cartes créés).
    batch_map = folder / "Batch.mindmap"
    run("new", batch_map, "--title", "Batch")
    ops = {"ops": [
        {"op": "add", "title": "Thème A", "as": "a", "note": NOTE},
        {"op": "add", "parent": "$a", "title": "Sous-idée", "marker": "revoir"},
        {"op": "submap-new", "id": "$a", "outline": "- Chapitre A1\n  - Détail"},
        {"op": "link", "from": "$a", "to": 1, "label": "voir aussi"},
        {"op": "set", "id": "$a", "collapsed": True},
    ]}
    out, _ = run("batch", batch_map, "--json", stdin=json.dumps(ops))
    check(json.loads(out)["count"] == 5, f"batch : {out}")
    out, _ = run("show", batch_map, "--json")
    node = json.loads(out)["root"]["children"][0]
    check(node["note"] == NOTE and node.get("collapsed") and "submap" in node, f"batch note/état : {node}")
    check((folder / "Batch" / "Thème A.mindmap").is_file(), "sous-carte créée par le batch")
    before = batch_map.read_bytes()
    bad = {"ops": [
        {"op": "add", "title": "Fantôme", "as": "f"},
        {"op": "submap-new", "id": "$f", "outline": "- X"},
        {"op": "set", "id": 999, "title": "n'existe pas"},
    ]}
    run("batch", batch_map, stdin=json.dumps(bad), expect=1)
    check(batch_map.read_bytes() == before, "un batch en échec ne doit rien écrire")
    check(not (folder / "Batch" / "Fantôme.mindmap").exists(), "un batch en échec doit supprimer les sous-cartes créées")
    run("batch", batch_map, stdin=json.dumps({"ops": [{"op": "add", "title": "x", "parent": "$inconnu"}]}), expect=2)
    run("batch", batch_map, stdin=json.dumps({"ops": [{"op": "danse"}]}), expect=2)

    # Fichiers orphelins : la carte est détachée mais son fichier reste.
    out, _ = run("show", batch_map, "--json")
    run("submap-detach", batch_map, "--id", json.loads(out)["root"]["children"][0]["id"])
    out, _ = run("orphans", batch_map, "--json")
    check(len(json.loads(out)["orphans"]) == 1, f"orphelin attendu : {out}")
    out, _ = run("validate", batch_map, "--json")
    check(json.loads(out)["ok"] and len(json.loads(out)["warnings"]) == 1, f"validate doit avertir : {out}")

    # Cours Markdown -> carte + 2 niveaux de sous-cartes ; recherche globale ; export pour Nemo.
    course = folder / "Cours.mindmap"
    plan = ("# Histoire\n## Thème 1 : l'Europe\n### Chapitre 1 : la Révolution\n- 1789\n- Causes\n  > Crise financière\n"
            "### Chapitre 2\n- Napoléon\n## Thème 2\n- Guerre froide\n")
    out, _ = run("import-course", course, "--split", 2, "--json", stdin=plan)
    check(json.loads(out)["maps"] == 5, f"import-course : {out}")
    run("import-course", course, stdin=plan, expect=1)            # n'écrase jamais un fichier existant
    out, _ = run("tree", course, "--json")
    check([m["depth"] for m in json.loads(out)["maps"]] == [0, 1, 2, 2, 1], f"hiérarchie importée : {out}")
    run("validate", course)

    out, _ = run("search", course, "NAPOLEON", "--json")
    hits = json.loads(out)["hits"]
    check(len(hits) == 1 and hits[0]["path"] == "Histoire › Thème 1 : l'Europe › Chapitre 2", f"recherche : {out}")
    out, _ = run("search", course, "revolution", "--json")                     # sans accents ni casse
    check(len(json.loads(out)["hits"]) >= 1, "la recherche ignore accents et casse")
    out, _ = run("search", course, "crise", "--json")                           # texte d'une note
    check(json.loads(out)["hits"][0].get("excerpt", "").startswith("Crise"), f"recherche dans une note : {out}")
    run("search", course, expect=2)

    nemo_courses = folder / "cours Nemo"
    nemo_courses.mkdir()
    run("nemo-export", course, "--workspace", folder, expect=2)                                        # dossier pas encore configuré
    run("nemo-config", "--workspace", folder, "--courses", nemo_courses)
    run("nemo-config", "--workspace", folder, "--courses", folder / "absent", expect=1)
    out, _ = run("nemo-export", course, "--workspace", folder, "--json")
    exported = Path(json.loads(out)["file"])
    markdown = exported.read_text(encoding="utf-8")
    check(exported.parent == nemo_courses and exported.name == "Histoire (carte mentale).md", f"fichier exporté : {exported}")
    check("\n# Histoire\n" in markdown and "\n## Thème 1 : l'Europe\n" in markdown and "\n### Chapitre 1 : la Révolution\n" in markdown and "- Napoléon" in markdown,
          f"sections du cours exporté :\n{markdown}")

    # Fichier manquant => problème signalé, code 1.
    chapter.unlink()
    run("validate", main, expect=1)
    run("submap-detach", theme, "--id", chapter_id)
    run("validate", main)
    run("show", folder / "absent.mindmap", expect=1)
    run("nimporte-quoi", expect=2)

if failures:
    print("\n".join(failures))
    sys.exit(1)
print("mindmap-cli : PASS")
