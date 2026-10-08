# mindmap-cli : créer et modifier des cartes en ligne de commande

`mindmap-cli.exe` (compilé avec l'application, dans `sources/build/`) lit et écrit les mêmes fichiers `.mindmap` que l'interface. Il est pensé pour qu'une IA (Claude Code, un script, un agent…) puisse **créer, relire et corriger des cartes sans ouvrir la fenêtre**. Aucune dépendance, aucun réseau.

Chaque commande lit le fichier, applique **une** modification, puis réécrit le fichier de façon atomique. Ajoutez `--json` pour obtenir une réponse lisible par un programme. Codes de sortie : `0` succès, `1` échec (fichier introuvable, idée absente, boucle…), `2` mauvaise utilisation.

## Boucle de travail conseillée pour une IA

1. **Lire** : `mindmap-cli show Histoire.mindmap --json` donne l'arbre complet (numéros d'idée `id`, notes, repères, sous-cartes, liens). Les numéros servent à toutes les commandes suivantes.
2. **Modifier** avec `add`, `add-outline`, `set`, `move`, `remove`, `link`…
3. **Vérifier** : `mindmap-cli validate Histoire.mindmap` contrôle la carte *et* toutes ses sous-cartes (code 1 s'il manque un fichier ou s'il y a une boucle).

Pour écrire beaucoup d'idées d'un coup, passez par un **plan Markdown** : `add-outline` (sous une idée existante) ou `new --outline` (nouvelle carte). Les titres `#`, les listes `-` ou indentées, les notes `> texte` et les repères (`★ `, `✓ `, `[?] `, `[!] `, `⚑ `) sont compris.

```bash
printf '# Révolution\n- Causes\n  - Crise financière\n  > Dette de l’État\n- Conséquences\n' | mindmap-cli new "Révolution.mindmap" --outline -
mindmap-cli show "Révolution.mindmap"
mindmap-cli add-outline "Révolution.mindmap" --parent 3 --outline plan.md
```

## Commandes

| Commande | Rôle |
|---|---|
| `show <carte> [--json]` | Arbre des idées avec leurs numéros |
| `outline <carte> [--deep]` | Plan Markdown ; `--deep` inclut les sous-cartes sous leur idée |
| `tree <carte> [--json]` | Carte, sous-cartes, sous-sous-cartes… avec leur état (`ok`, `introuvable`, `illisible`, `boucle`) |
| `validate <carte>` | Vérifie toute la hiérarchie |
| `export <carte> --to f.md` ou `f.svg` `[--deep]` | Exporte (PNG/PDF restent dans l'application) |
| `new <carte> [--title T] [--outline plan.md]` | Crée une carte (refuse d'écraser un fichier existant) ; `-` lit l'entrée standard |
| `add <carte> --title T [--parent ID] [--note N] [--note-file f] [--marker M] [--color #RRGGBB]` | Ajoute une idée (sous le sujet central par défaut) |
| `add-outline <carte> [--parent ID] [--outline plan.md]` | Insère un plan Markdown sous une idée (entrée standard par défaut) |
| `set <carte> --id ID [--title T] [--note N] [--note-file f] [--marker M] [--color #RRGGBB] [--only-node] [--collapsed] [--expanded]` | Modifie une idée |
| `move <carte> --id ID --parent ID` | Change le parent d'une branche |
| `remove <carte> --id ID` | Supprime l'idée et ses descendants (les fichiers de sous-cartes ne sont jamais effacés) |
| `link` / `unlink <carte> --from ID --to ID [--label L]` | Lien transversal |
| `batch <carte> [--from ops.json]` | Plusieurs modifications en une seule écriture, **tout ou rien** (JSON sur l'entrée standard, voir plus bas) |
| `orphans <carte>` | Fichiers de sous-cartes que plus aucune idée ne référence |
| `search <carte> <mots> [--limit N]` | Cherche dans les titres et notes de la carte **et de toutes ses sous-cartes** (accents et casse ignorés ; tous les mots doivent figurer dans la même idée) |
| `import-course <carte> [--from cours.md] [--split N] [--title T]` | Crée une carte depuis un cours Markdown ou texte ; `--split N` fait des **sous-cartes** des idées des N premiers niveaux qui ont des sous-idées |
| `nemo-config [--courses <dossier>]` | Dossier des cours que Nemo synchronise (affiché si aucune option) |
| `nemo-export <carte> [--to <dossier>]` | Écrit toute la hiérarchie en un cours Markdown « <titre> (carte mentale).md » |
| `submap-new <carte> --id ID [--title T] [--outline plan.md]` | Crée la sous-carte de l'idée |
| `submap-attach <carte> --id ID --file chemin.mindmap` | Relie une carte existante comme sous-carte |
| `submap-detach <carte> --id ID` | Détache la sous-carte (le fichier est conservé) |

Repères (`--marker`) : `0` aucun, `1` important, `2` acquis, `3` revoir, `4` attention, `5` repere (les noms français fonctionnent aussi). Dans `--note`, `\n` produit un retour à la ligne ; pour une note longue, utilisez `--note-file` (le fichier, ou `-` pour l'entrée standard). `--no-layout` conserve les positions telles quelles ; sinon la carte est réorganisée automatiquement.

## Batch : plusieurs modifications d'un coup

Une IA qui construit un cours entier a intérêt à envoyer **un seul batch** : la carte n'est lue et écrite qu'une fois, et si une opération échoue, **rien n'est écrit** (les fichiers de sous-cartes créés en cours de route sont supprimés). Chaque opération porte les mêmes paramètres que la commande du même nom ; `"as"` mémorise l'idée créée et `"$nom"` la désigne ensuite. Dans un batch, `note` et `outline` sont du texte direct (pas des chemins de fichier).

```json
{"ops": [
  {"op": "add", "title": "Thème 1 : l'Europe", "as": "t1", "marker": "important"},
  {"op": "add", "parent": "$t1", "title": "Chapitre 1", "note": "1789\nLa Révolution", "as": "c1"},
  {"op": "submap-new", "id": "$c1", "outline": "- Causes\n- Conséquences"},
  {"op": "link", "from": "$t1", "to": 1, "label": "voir aussi"}
]}
```

```bash
mindmap-cli batch Histoire.mindmap --json < ops.json
```

Opérations disponibles : `add`, `add-outline`, `set`, `move`, `remove`, `link`, `unlink`, `submap-new`, `submap-attach`, `submap-detach`.

## Sous-cartes : histoire → thème 1 → chapitre 1…

Une idée peut contenir **une autre carte**, qui peut elle-même contenir des sous-cartes (16 niveaux et 200 cartes au plus). Chaque carte reste un fichier `.mindmap` ordinaire ; l'idée garde seulement le **chemin relatif** du fichier de sa sous-carte, ce qui permet de déplacer tout le dossier.

```text
Histoire.mindmap                       ← l'idée « Thème 1 » a une sous-carte
Histoire/Thème 1 _ l'Europe.mindmap    ← l'idée « Chapitre 1 » a une sous-carte
Histoire/Thème 1 _ l'Europe/Chapitre 1 _ la Révolution.mindmap
```

```bash
mindmap-cli new Histoire.mindmap --title "Histoire"
mindmap-cli add Histoire.mindmap --title "Thème 1 : l'Europe"                    # → id 2
mindmap-cli submap-new Histoire.mindmap --id 2 --outline plan-theme1.md          # crée Histoire/Thème 1….mindmap
mindmap-cli show "Histoire/Thème 1 _ l'Europe.mindmap" --json                    # ids de la sous-carte
mindmap-cli submap-new "Histoire/Thème 1 _ l'Europe.mindmap" --id 2              # sous-carte du chapitre 1
mindmap-cli tree Histoire.mindmap
mindmap-cli outline Histoire.mindmap --deep                                      # tout le cours en un seul plan
```

Règles :

- Le sujet central d'une sous-carte reprend le titre de l'idée qui la contient. Le sujet central d'une carte ne peut pas lui-même avoir de sous-carte (choisir l'une de ses idées).
- Le chemin d'une sous-carte est **relatif**, sans `..` : le fichier doit se trouver dans le dossier de la carte parente ou dans un sous-dossier. Le format refuse tout autre chemin.
- Une carte ne peut pas se contenir elle-même, directement ou via d'autres sous-cartes (`submap-attach` refuse les boucles).
- Supprimer une idée qui a une sous-carte ne supprime **pas** le fichier de la sous-carte.
- Le titre de l'idée et le sujet central de sa sous-carte restent **identiques** dans l'application : renommer l'idée renomme la sous-carte, et renommer la sous-carte puis remonter renomme l'idée. La CLI ne synchronise pas ces titres (modifiez-les des deux côtés avec `set`).
- Si un fichier de sous-carte a été déplacé ou supprimé, l'application propose de le retrouver ou de détacher la sous-carte ; `validate` le signale (`introuvable`, code 1) et `orphans` liste les fichiers que plus aucune idée n'utilise.
- Renommer la carte parente ne renomme pas son dossier de sous-cartes : les liens restent valables (chemins relatifs) mais le rangement par défaut ne suit plus le nom.
- Dans l'application, ces idées portent un badge ▶ ; touche **O** ou clic sur le badge pour ouvrir, **Retour arrière** ou « ↑ Remonter » pour revenir (un fil d'Ariane indique le chemin). Sur une idée sans sous-carte, **O** propose de la créer.

## Cours et Nemo

Nemo lit les cours d'un dossier synchronisé (Word, PDF, PowerPoint, **Markdown**, texte). La connexion avec MindMap passe donc par des fichiers, sans réseau :

- **Carte → Nemo** : `nemo-export` écrit toute la hiérarchie (carte, sous-cartes, sous-sous-cartes) en un seul fichier Markdown « *<titre> (carte mentale).md* ». Chaque carte est un plan en liste, et chaque idée qui porte une sous-carte devient une section (`#` Histoire, `##` Thème 1, `###` Chapitre 1…). À la prochaine synchronisation, Nemo le voit dans `list_courses` / `read_course` et peut s'en servir pour ses séances. Le fichier est **régénéré** à chaque export : modifiez la carte, pas le fichier.
- **Nemo → carte** : une IA lit un cours avec `read_course` puis le transforme en cartes : `import-course Histoire.mindmap --split 2 --from -` (texte sur l'entrée standard). Les titres `#`, `##`, `###` deviennent les niveaux ; avec `--split 2`, les idées des deux premiers niveaux qui ont des sous-idées deviennent des sous-cartes (histoire → thème → chapitre), les niveaux suivants restent des idées.
- **Dossier des cours** : indiquez-le une fois avec `nemo-config --courses "C:\\...\\cours\\hist géo"` (il est mémorisé dans `.mindmap-data/nemo.json`, à côté de l'exécutable), ou passez `--to <dossier>` à chaque export. Dans l'application, **Fichier > Exporter pour Nemo…** demande l'emplacement la première fois, puis le retient. L'export ne touche jamais aux autres fichiers du dossier.
- Dans l'application, **Fichier > Exporter le cours complet…** produit le même Markdown où vous voulez.

## Précautions

- **Fermez la carte dans l'application** (ou rouvrez-la ensuite) quand une IA la modifie : l'application détecte qu'un autre programme a changé le fichier et garde vos changements dans un brouillon de secours au lieu de l'écraser.
- La CLI ne crée pas de versions de secours : travaillez sous Git ou faites une copie avant une grosse restructuration.
- Les arguments sont lus en Unicode : les accents des dossiers et des titres sont corrects sous Windows. Passez les textes longs par l'entrée standard (`-`) plutôt que par la ligne de commande.
- Limites du format : 2 000 idées par carte, titres de 1 024 octets, notes de 16 000 octets, 63 niveaux de profondeur, fichier de 8 Mio.

## Nemo : export groupé, publication directe, maîtrise

- `nemo-export --all <dossier de cartes>` exporte **toutes les cartes racines** d'un dossier (une sous-carte n'est jamais exportée comme cours à part). MindMap garde la liste de ce qu'il a généré (`.mindmap-data/nemo-exports.json`) : renommer une carte **remplace** l'ancien fichier « … (carte mentale).md », supprimer une carte retire le sien. Un fichier qui n'a pas ce nom, ou qui est hors du dossier des cours, n'est jamais supprimé.
- `nemo-config --auto-export on` (ou *Fichier > Publier vers Nemo à l'enregistrement* dans l'application) : un enregistrement **manuel** (Ctrl+S) réécrit aussi le cours dans le dossier de Nemo. Jamais à l'enregistrement automatique (cela inonderait l'historique de changements de Nemo).
- **Réseau, facultatif et désactivé par défaut** : `nemo-remote --url https://… --subject "hist géo" [--folder cartes] [--scope class]`, `echo JETON | nemo-login` (jeton dans le Gestionnaire d'identification Windows, jamais dans un fichier), `nemo-logout`, `nemo-push <carte>|--all <dossier>|--flush` (publie le fichier et son texte compagnon sous `<matière>/<dossier>/…`, file d'attente `.mindmap-data/nemo-queue/` si Nemo est injoignable, suppression de l'ancien cours d'une carte renommée). Délais courts, adresse en `https://` obligatoire (`http` seulement pour `127.0.0.1`/`localhost`), plafond de stockage (HTTP 413) et jeton refusé expliqués clairement.
- `nemo-mastery <carte> [--json]` interroge Nemo (`POST /api/mastery/lookup`) et affiche pour chaque idée ✓ acquis, ↻ à revoir, … en cours, · inconnu. **Lecture seule** : la carte et vos repères ne sont pas modifiés. L'appariement se fait par matière + titre de l'idée.
- **`.mindmap` directement comme cours** : le démon de synchronisation de Nemo accepte maintenant les fichiers `.mindmap` (carte + sous-cartes = un cours Markdown identique à `nemo-export`). Plus besoin d'exporter si le dossier de la matière est synchronisé ; n'utilisez pas les deux pour la même carte (Nemo lirait deux fois le même cours). Les sous-cartes d'une autre carte ne sont pas envoyées à part.
- **Renommer une idée** : MindMap garde ses anciens titres (`.mindmap-data/nemo-ideas.json`, mis à jour par `nemo-export`, `nemo-push`, la publication à l'enregistrement et `nemo-mastery`), et `nemo-mastery` retrouve l'historique de révision sous l'ancien titre (`renamedFrom` en JSON).
