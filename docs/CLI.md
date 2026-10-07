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
| `submap-new <carte> --id ID [--title T] [--outline plan.md]` | Crée la sous-carte de l'idée |
| `submap-attach <carte> --id ID --file chemin.mindmap` | Relie une carte existante comme sous-carte |
| `submap-detach <carte> --id ID` | Détache la sous-carte (le fichier est conservé) |

Repères (`--marker`) : `0` aucun, `1` important, `2` acquis, `3` revoir, `4` attention, `5` repere (les noms français fonctionnent aussi). Dans `--note`, `\n` produit un retour à la ligne ; pour une note longue, utilisez `--note-file` (le fichier, ou `-` pour l'entrée standard). `--no-layout` conserve les positions telles quelles ; sinon la carte est réorganisée automatiquement.

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
- Dans l'application, ces idées portent un badge ▶ ; touche **O** ou clic sur le badge pour ouvrir, **Retour arrière** ou « ↑ Remonter » pour revenir (un fil d'Ariane indique le chemin). Sur une idée sans sous-carte, **O** propose de la créer.

## Précautions

- **Fermez la carte dans l'application** (ou rouvrez-la ensuite) quand une IA la modifie : l'application détecte qu'un autre programme a changé le fichier et garde vos changements dans un brouillon de secours au lieu de l'écraser.
- La CLI ne crée pas de versions de secours : travaillez sous Git ou faites une copie avant une grosse restructuration.
- Les arguments sont lus en Unicode : les accents des dossiers et des titres sont corrects sous Windows. Passez les textes longs par l'entrée standard (`-`) plutôt que par la ligne de commande.
- Limites du format : 2 000 idées par carte, titres de 1 024 octets, notes de 16 000 octets, 63 niveaux de profondeur, fichier de 8 Mio.
