# Guide pour un agent : MindMap + Nemo

Un agent qui dispose de **deux outils** — le serveur MCP Nemo et `mindmap-cli` — peut construire un cours sous forme de cartes, le faire réviser par Nemo, puis resserrer les cartes selon ce que l'élève maîtrise. Ce guide donne le jeu de commandes minimal et trois recettes. Détail des commandes : `docs/CLI.md`.

**Répartition des rôles** : MindMap sert à *construire* le cours (cartes, sous-cartes). Nemo sert à le *réviser* (séances, quiz, fiches, répétition espacée). MindMap n'a plus de mode révision.

## Jeu de commandes minimal

| Besoin | Commande |
|---|---|
| Lire une carte (numéros d'idée) | `mindmap-cli show <carte> --json` |
| Tout le cours, sous-cartes comprises | `mindmap-cli outline <carte> --deep` |
| Écrire beaucoup d'idées d'un coup | `mindmap-cli batch <carte> --json < ops.json` |
| Cours Markdown → cartes | `mindmap-cli import-course <carte> --split 2 --from -` |
| Marquer une idée « à revoir » | `mindmap-cli set <carte> --id N --marker revoir` |
| Publier le cours vers Nemo | `mindmap-cli nemo-export <carte>` (via le dossier synchronisé) ou `nemo-push <carte>` (direct) |
| Savoir ce que Nemo sait de chaque idée | `mindmap-cli nemo-mastery <carte> --json` (lecture seule) |
| Vérifier | `mindmap-cli validate <carte>` |

Fermez la carte dans l'application pendant qu'un agent la modifie, et travaillez sous Git ou sur une copie avant une grosse restructuration.

## Recette 1 — Cours Word → cartes

1. `read_course` (MCP Nemo) lit le cours, par exemple `histoire.docx`, en Markdown ou en texte.
2. `mindmap-cli import-course Histoire.mindmap --split 2 --from -` (le texte sur l'entrée standard) : les titres `#`, `##`, `###` deviennent les niveaux, et les idées des deux premiers niveaux qui ont des sous-idées deviennent des sous-cartes (histoire → thème → chapitre).
3. `mindmap-cli validate Histoire.mindmap`, puis relecture avec `outline --deep`.
4. Corriger avec `set` / `move` / `batch` ; **les titres d'idées sont des notions à retenir** (voir recette 3) : courts, précis, sans ponctuation inutile.

## Recette 2 — Resserrer selon la maîtrise

1. `mindmap-cli nemo-mastery Histoire.mindmap --json` donne pour chaque idée `status` : `acquis`, `en-cours`, `a-revoir` ou `inconnu` (avec la boîte Leitner et la prochaine révision).
2. `get_progression` / `suggest_focus` (MCP Nemo) donnent la vue d'ensemble par matière.
3. Pour chaque idée `a-revoir` : `mindmap-cli set Histoire.mindmap --id N --marker revoir`. Ne touchez **jamais** aux repères déjà posés à la main sur les idées `acquis` sans le dire à l'élève : `nemo-mastery` n'écrit rien, c'est l'agent qui décide.
4. Ajouter des exemples ou des sous-idées sous les idées faibles (`add`, `add-outline`).

## Recette 3 — Carte à jour → séance et fiche Nemo

1. Publier : `mindmap-cli nemo-export Histoire.mindmap` (le fichier « Histoire (carte mentale).md » apparaît à la prochaine synchronisation) **ou** `mindmap-cli nemo-push Histoire.mindmap` (immédiat ; demande `nemo-remote` et `nemo-login`, une fois).
2. Dans Nemo : `/nemo-prompt-fiche` (outil `prepare_fiche`) lit ce cours et en fait une fiche : hiérarchie → sections, ★ → emphase, `[!]` → piège, `[?]` et ⚑ → questions de quiz. `generate_flashcards` / `generate_mixed_session` fonctionnent aussi sur le cours à jour.
3. **Convention d'appariement** : le *recto* d'une carte ou d'une question de quiz doit être le **titre de l'idée**. Nemo retrouve alors la notion par matière + titre (casse, accents et espaces ignorés), et `nemo-mastery` affiche sa maîtrise sur la carte. Renommer une idée la sépare de son historique de révision.

## Réseau (facultatif, désactivé par défaut)

```bash
mindmap-cli nemo-remote --url https://nemo.moideici.tech --subject "hist géo" --folder cartes   # une fois
echo "VOTRE_JETON" | mindmap-cli nemo-login      # jeton dans le Gestionnaire d'identification Windows
mindmap-cli nemo-push Histoire.mindmap           # publie ; hors-ligne : mis en file d'attente
mindmap-cli nemo-push --flush                    # renvoie la file d'attente
```

Le dépôt de cours demande un profil **administrateur** de Nemo ; `--scope class` publie dans les cours de la classe (partagés). Le jeton n'est jamais écrit dans un fichier ni dans les journaux ; pour un script, la variable d'environnement `NEMO_TOKEN` le remplace.
