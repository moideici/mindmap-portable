# MindMap portable

Carte mentale pour Windows, pensée pour réviser : **un seul fichier `MindMap.exe`**, sans installation, sans compte, sans connexion à Internet. Le dossier peut être copié sur une clé USB.

![Carte en mode clair, branches des deux côtés et aperçu des notes](docs/capture-claire.png)

Téléchargez l'exécutable dans les [Releases](../../releases) ou compilez-le (voir plus bas). La version 3 des sources ouvre un **accueil** avec vos cartes récentes, vos brouillons et vos modèles. Une carte enregistrée est sauvegardée automatiquement dans son propre fichier.

## Nouveautés de la version 3

- **Accueil** : créer une carte, ouvrir, importer, reprendre une carte récente ou un brouillon.
- **Modèles** : carte vide, chapitre, dissertation, chronologie, comparaison ; création de vos propres modèles depuis le menu Fichier.
- **Images et schémas** : PNG, JPEG ou BMP ajoutés à l'idée sélectionnée, depuis le panneau de droite ou par glisser-déposer. L'illustration est intégrée au fichier, redimensionnée à 640 pixels au plus, et suit les duplications de branches et l'annulation.
- **Versions de secours** : jusqu'à dix versions par carte, restaurables depuis Fichier. Les sauvegardes automatiques conservent une version au plus toutes les cinq minutes ; les enregistrements manuels créent aussi une version si le contenu change.
- **Import** : FreeMind `.mm`, XMind `.xmind` (formats XML et JSON), Markdown et texte. Les titres, branches et notes sont repris ; les liens sont repris depuis FreeMind et XMind JSON. Les styles, illustrations et pièces jointes des formats externes ne sont pas importés.
- **Export PDF** : carte visible sur une page A4 paysage, avec les images intégrées. Le PDF utilise une image du rendu ; son texte n'est pas sélectionnable. Les exports PNG et SVG incluent aussi les images. L'export Markdown reste un plan texte.

Les captures ci-dessous montrent l'interface de la version 2 ; l'accueil et les nouveaux panneaux restent à vérifier visuellement sous Windows.

## Nouveautés de la version 2

| | Amélioration | En bref |
|---|---|---|
| 1 | **Mémoire et fluidité** | Historique d'annulation compressé (~10× moins de mémoire, 500 étapes au lieu de 80), plus de sérialisation complète de la carte à chaque clic, polices et image de fond réutilisées entre deux affichages, champs de texte qui ne clignotent plus |
| 2 | **Branches tout autour de l'idée principale** | Disposition équilibrée gauche/droite (par défaut), radiale à 360° ou vers la droite, sans chevauchement |
| 3 | **Affichage des détails** | Idées dont la taille suit le texte (titres sur plusieurs lignes), aperçu des notes sur la carte (N), note complète au survol, idées trouvées surlignées pendant une recherche |
| 4 | **Relier deux idées** | Liens transversaux fléchés (L puis clic), avec libellés : cause de, conséquence, s'oppose à, exemple de… ou texte libre |
| 5 | **Repères** | ★ important, ✓ acquis, ? à revoir, ! attention, ⚑ repère clé (touches 1 à 5), plus une couleur personnalisée |
| 6 | **Mode révision** | Les idées sont masquées puis découvertes une à une (Espace) ; auto-évaluation acquis / à revoir ; second passage sur les seules idées à revoir |
| 7 | **Plans texte** | Importer un plan Markdown ou texte indenté, coller un plan sous une idée (Ctrl+Maj+V), copier une branche en texte (Ctrl+Maj+C) |
| 8 | **Navigation clavier** | Les flèches passent à l'idée voisine dans la direction choisie, quelle que soit la disposition ; Origine revient au sujet central |
| 9 | **Mini-carte** | Vue d'ensemble cliquable en bas à droite (M) |
| 10 | **Thème sombre** | Interface, carte et barre de titre Windows |

Les cartes des versions 1 et 2 restent lisibles. Les nouveaux fichiers utilisent le format `version 3`, qui ajoute les images intégrées.

| Thème sombre, disposition radiale | Mode révision |
|---|---|
| ![](docs/capture-sombre-radiale.png) | ![](docs/capture-revision.png) |

## Une première carte en une minute

1. Depuis l'**accueil**, choisissez Nouvelle carte ou un modèle, puis donnez un nom au sujet.
2. Sélectionnez le sujet et appuyez sur **Tab** pour créer une idée enfant ; **Entrée** ajoute une idée au même niveau.
3. Écrivez le titre et les notes ; **Ctrl+Entrée** applique, et tout est aussi appliqué après une courte pause.
4. **L** puis un clic sur une autre idée crée un lien ; clic droit sur le lien pour lui donner un libellé.
5. **R** lance la révision ; **Ctrl+S** choisit le nom et l'emplacement de la carte. Les changements suivants sont enregistrés automatiquement dans ce fichier.

`Exemple - révisions.mindmap` fournit un autre exemple : ouvrez-le avec **Ctrl+O** ou glissez-le sur la fenêtre. Un fichier `.md` ou `.txt` glissé sur la fenêtre est importé comme plan.

## Raccourcis

| Action | Geste |
|---|---|
| Idée enfant / même niveau | Tab / Entrée |
| Modifier un titre ou ses notes | Double-clic, ou F2 |
| Passer à l'idée voisine | Flèches ; Origine pour le sujet central |
| Relier deux idées | L (ou « Relier »), puis clic sur la seconde ; Échap annule |
| Libellé, sens ou suppression d'un lien | Clic droit sur le lien ; double-clic pour un libellé libre ; Suppr |
| Repères | 1 ★ · 2 ✓ · 3 ? · 4 ! · 5 ⚑ · 0 retire |
| Aperçu des notes / mini-carte | N / M |
| Réviser | R, puis Espace pour révéler, 2 / 3 pour s'évaluer, Échap pour terminer |
| Copier une branche en texte / coller un plan | Ctrl+Maj+C / Ctrl+Maj+V |
| Importer un plan | Ctrl+I |
| Zoomer / déplacer la vue | Molette / glisser le fond ou la mini-carte |
| Déplacer une branche / changer son parent | Glisser l'idée / Maj + glisser sur le futur parent |
| Replier une branche | Bouton + / − (il indique le nombre d'idées cachées) |
| Réorganiser / tout voir | Ctrl+L / F |
| Rechercher | Ctrl+F ; les idées trouvées sont surlignées sur la carte |
| Annuler / rétablir | Ctrl+Z / Ctrl+Y |
| Dupliquer / supprimer | Ctrl+D / Suppr |

**Affichage ▾** regroupe les dispositions, l'aperçu des notes, la mini-carte et le thème. **Aide** rappelle les raccourcis et indique la mémoire utilisée par l'historique.

## Exporter

**Fichier** exporte en **PDF**, **PNG**, **SVG** ou **Markdown**.

- Les images représentent les branches visibles, avec les liens, les repères et les notes affichées. Ouvrez les branches repliées avant d'exporter pour tout montrer.
- Le Markdown contient tout le plan, les notes, les repères et une section « Liens transversaux ». Il se réimporte tel quel.

## Sauvegarde et clé USB

Il n'y a plus de carte anonyme rechargée automatiquement depuis `session.mindmap`.

- **Carte enregistrée** : le nom et l'emplacement sont affichés ; les modifications sont enregistrées dans ce même fichier.
- **Nouvelle carte** : un brouillon est protégé dans `.mindmap-data/drafts/`. Il apparaît à l'accueil jusqu'à son premier enregistrement, puis le brouillon est retiré. Le menu Fichier permet aussi de supprimer un brouillon volontairement.
- **Ancienne session** : l'accueil propose « Récupérer l'ancienne session ». Son contenu devient un brouillon ; le fichier d'origine est archivé dans `.mindmap-data/legacy/` après récupération. Une session illisible n'est pas remplacée.
- **Versions** : `.mindmap-data/backups/` conserve jusqu'à dix états précédents par carte. Fichier → Restaurer une version précédente remet un état dans le fichier d'origine tout en conservant l'état remplacé.
- **Modèles et cartes récentes** : `.mindmap-data/templates/` et `.mindmap-data/workspace.json` conservent la bibliothèque et les emplacements. Les cartes situées à côté de l'application restent accessibles après déplacement de tout le dossier.

Pour déplacer l'application sur une clé USB, copiez **l'exécutable, vos cartes et `.mindmap-data/` ensemble**. Une carte peut aussi être enregistrée ailleurs : son emplacement reste affiché. Si le fichier est modifié par un autre programme ou disparaît, la sauvegarde automatique ne l'écrase pas ; les changements sont protégés dans un brouillon de secours et un message propose d'utiliser Enregistrer sous ou de rouvrir le fichier.

La sauvegarde écrit un fichier temporaire à côté de la destination, puis le remplace. Un message signale une clé pleine ou un dossier inaccessible. Fermez l'application et éjectez la clé avant de la retirer.

L'application ne crée pas de dossier AppData, n'écrit rien dans le registre et ne se connecte pas à Internet.

## Limites

2 000 idées, 1 000 liens, 63 niveaux sous le sujet, fichier de 8 Mio (images comprises), titres de 1 024 octets UTF-8, notes de 16 000 octets, libellés de liens de 200 octets. Une image PNG normalisée de 1 Mio maximum par idée. Jusqu'à 100 modèles personnels. Une seule fenêtre par dossier d'application. Les emojis s'affichent selon les polices Windows ; ils restent conservés dans les documents.

## Développement

```text
sources/
├── python/mindmap_core.py       modèle de l'arbre écrit en Python
├── generated/mindmap_core.cpp   sortie py2x non retouchée (empreinte dans provenance.json)
├── src/documents.hpp            JSON, format v1/v2, liens et repères
├── src/geometry.hpp             tailles des idées, dispositions, courbes (pur, testé)
├── src/exports.hpp              SVG, Markdown, import de plans, duplication avec liens
├── src/history.hpp              historique compressé (API de compression Windows)
├── src/workspace.hpp            brouillons, versions, cartes récentes et modèles
├── src/media.hpp                images PNG intégrées et encodage Base64
├── src/imports.hpp              FreeMind, XMind et lecture bornée des archives ZIP
├── src/pdf.hpp                  export PDF à partir du rendu de la carte
├── src/app.cpp                  fenêtre Win32, rendu GDI+, interactions
└── tests/core_tests.cpp         tests du modèle, du format, des dispositions et de l'historique
```

Les nouveautés de la version 2 sont écrites en C++ natif autour du cœur généré, qui reste identique octet pour octet : le convertisseur py2x n'est nécessaire que pour régénérer ce cœur.

Compilation avec MinGW-w64 (GCC) et Ninja, par exemple installés avec `scoop install mingw cmake ninja` :

```bash
cmake -S sources -B sources/build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build sources/build
```

```bash
ctest --test-dir sources/build --output-on-failure
```

`sources/build/MindMap.exe --smoke-test` exerce l'interface de bout en bout (édition, liens, repères, dispositions, révision, exports) et écrit un rapport et des captures dans `sources/build/`. MSVC ne convient pas : le C++ généré utilise des extensions GCC.

L'historique détaillé de la version 1 et les retours sur py2x sont dans **RAPPORT_COMPLET.md**.

### Tests sur Linux

Le modèle, le format de document, les dispositions et les exports se testent
avec GCC sans Windows. Avec CMake, les commandes ci-dessus ne construisent que
`core_tests` sur Linux. Sans CMake :

```bash
mkdir -p sources/build
g++ -std=c++20 -O2 -Wall -Wextra sources/tests/core_tests.cpp -o sources/build/core_tests
sources/build/core_tests
g++ -std=c++20 -O2 -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_ARCHIVE_APIS -x c++ -c sources/vendor/miniz/miniz_tinfl.c -o sources/build/miniz_tinfl.o
g++ -std=c++20 -O2 -Wall -Wextra sources/tests/workspace_tests.cpp sources/build/miniz_tinfl.o -o sources/build/workspace_tests
sources/build/workspace_tests sources/tests/fixtures/compressed.xmind
python3 sources/tests/wrap_performance.py
```

Le dernier script compare le découpage UTF-8 avec le commit de référence v2
(`98b56d2`, qui doit être présent dans le dépôt Git), puis mesure les notes
courtes et longues. L'interface et l'historique compressé restent à vérifier
sous Windows avec MinGW et `--smoke-test`.
