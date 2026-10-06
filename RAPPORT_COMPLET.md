# MindMap portable — rapport d'implémentation, validation et retours pour py2x

Date : **4 octobre 2026**. Ce document est destiné à l'utilisateur et aux futures conversations qui travailleront sur le convertisseur ou l'application.

## 1. Résultat livré et consignes respectées

La livraison est une application graphique Windows x64 de carte mentale, exécutée par **un seul fichier `MindMap.exe`**. Le modèle métier est écrit en Python typé, traduit en C++ avec le convertisseur existant **py2x**, puis intégré à une interface Windows écrite manuellement.

Emplacement demandé :

```text
C:\Users\gabor\OneDrive\cours\hist géo\outil\mindmap
```

Le dossier comprend un exécutable prêt à lancer, une carte d'exemple, un guide, ce rapport et un sous-dossier de sources. Pour une copie d'utilisation sur clé USB, seul l'exécutable est nécessaire au premier lancement. Le fichier `session.mindmap` est ensuite créé dans le même dossier.

**Aucun fichier source du convertisseur n'a été modifié pendant ce projet.** Les SHA-256 de tous ses fichiers `.py` ont été enregistrés lors de la première génération et comparés à la fin des tests. Le résultat est `converter_unmodified: true`. Les limites ont été reproduites, puis contournées dans le projet applicatif uniquement.

Le précédent petit projet TaskDesk est indépendant de cette livraison. Il n'est pas utilisé par l'application. Aucun modèle d'IA, Ollama, entraînement ou téléchargement n'est nécessaire.

Aucun sous-agent n'a été utilisé.

### Ce que signifie « utilise le convertisseur » ici

Il ne s'agit pas de prétendre que l'ensemble de Windows ou de la GUI a été traduit depuis Python. Les opérations sur l'arbre, les règles métier et le placement automatique proviennent réellement de `python/mindmap_core.py`. Le C++ correspondant est conservé sans retouche.

La fenêtre, les événements, le dessin, les boîtes de dialogue, l'Unicode au niveau de l'interface, les fichiers et les exports sont des compléments C++ natifs. Cette séparation est explicite dans les fichiers et dans les sections suivantes.

## 2. Fonctionnalités disponibles

| Domaine | Fonctionnement |
|---|---|
| Création | Nouveau sujet central ; ajout d'une idée enfant ou au même niveau |
| Édition | Titres, notes multilignes, couleurs appliquées à toute une branche |
| Organisation | Réorganisation automatique ; déplacement libre d'une branche |
| Structure | Maj + glisser pour changer de parent ; protection contre les cycles |
| Duplication | Copie d'une branche et de ses descendants, notes, couleurs et plis |
| Suppression | Suppression d'une branche ; confirmation pour plusieurs idées ; sujet central protégé |
| Navigation | Zoom autour du pointeur, déplacement du fond, centrage de la carte |
| Lisibilité | Branches repliables ; plan latéral sélectionnable et défilant |
| Recherche | Titres et notes ; recherche Unicode insensible à la casse ; branches cachées incluses |
| Historique | Annuler/rétablir les changements métier ; historique borné en mémoire |
| Documents | Ouvrir, enregistrer, enregistrer sous ; fichiers `.mindmap` JSON UTF-8 |
| Portabilité | Session à côté de l'exécutable ; chemin indépendant du dossier courant |
| Récupération | Sauvegarde automatique ; copie conservée d'une session illisible, lorsque possible |
| Import utilisateur | Glisser-déposer d'un fichier `.mindmap` ; ouverture avec un chemin en argument |
| Exports | PNG, SVG et Markdown |
| Aide | Aide intégrée, commandes au clic droit, raccourcis clavier |
| Affichage | Fenêtre redimensionnable, gestion du DPI et icône embarquée |

### Raccourcis et focus

Les raccourcis applicatifs Tab, Entrée, Suppr, F et Ctrl+D sont utilisés lorsque le canvas a le focus. Les champs de saisie conservent leurs usages habituels. Ctrl+Z dans un champ correspond à son édition locale ; les boutons Annuler/Rétablir agissent sur le document. Échap replace le focus sur la carte après validation.

Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Maj+S, Ctrl+F et Ctrl+L sont accessibles depuis les champs. Ctrl+Entrée applique explicitement le titre et la note.

## 3. Organisation de la livraison

```text
mindmap/
├── MindMap.exe                      application portable
├── Exemple - révisions.mindmap       document de démonstration
├── README.md                        guide utilisateur
├── RAPPORT_COMPLET.md               ce rapport
└── sources/
    ├── CMakeLists.txt
    ├── python/mindmap_core.py        modèle écrit en Python
    ├── generated/mindmap_core.cpp    sortie py2x non retouchée
    ├── generated/provenance.json     empreintes et options de conversion
    ├── src/core_bridge.hpp           adaptation du point d'entrée de démonstration
    ├── src/documents.hpp             JSON, schéma, export SVG/Markdown
    ├── src/app.cpp                   fenêtre, rendu, contrôles, disque, PNG
    ├── assets/app.rc                 ressources Windows
    ├── assets/app.manifest           DPI, privilèges normaux, contrôles Windows
    ├── assets/icon.ico               icône incorporée à l'exécutable
    ├── tools/generate.py             génération et provenance
    ├── tools/build.ps1               construction et tests CMake
    └── tests/
        ├── core_tests.cpp            modèle, documents, erreurs, durées de vie
        ├── differential.py          comparaison du modèle Python/C++
        ├── converter_probes.py       reproductions des limites de py2x
        ├── portable_check.py         exécutable déplacé et dossier courant différent
        └── preuves/                 résultats et captures conservés
```

Les fichiers `session.mindmap` et les cartes créées par l'utilisateur apparaissent au cours de l'utilisation. Les fichiers `.tmp` sont temporaires. Les sources, ce rapport et le guide ne sont pas nécessaires au lancement.

## 4. Architecture et frontière Python/C++

### 4.1 Modèle traduit

`MapNode` est une dataclass comportant :

| Champ | Type Python | Rôle |
|---|---|---|
| `id` | int | identifiant stable |
| `parent` | int | identifiant du parent ; -1 uniquement pour le sujet central |
| `title` / `note` | str | contenu utilisateur |
| `x` / `y` | float | coordonnées dans l'espace de la carte |
| `color` | int | RGB sur 24 bits |
| `collapsed` | bool | état de repli |
| `children` | list[int] | ordre des enfants |

`MindMap` possède `nodes: dict[int, MapNode]` et un compteur `next_id`.

Ses méthodes traduites comprennent : `depth`, `add`, `descendants`, `remove`, `reparent`, `visible`, `branch_span`, `place`, `layout`, `move_branch`, `rename`, `set_color`, `toggle`, `reveal`, `duplicate`.

Les parcours `descendants` et `visible` utilisent une pile explicite. Le placement automatique utilise une récursion bornée par la profondeur maximale. Les enfants sont placés de gauche à droite, à 260 unités de leur parent ; la hauteur d'une feuille est de 100 unités.

La disposition est volontairement un arbre orienté vers la droite. Les positions restent librement modifiables. La réorganisation replace l'arbre ; elle n'essaye pas de préserver les déplacements manuels.

### 4.2 Sortie générée

La génération utilise :

```python
transpile(source, "cpp", strict=True)
```

Le mode strict évite la conversion implicite d'un int vers un paramètre annoté float. Les coordonnées et déplacements sont écrits avec des flottants explicites dans les tests et dans le modèle.

`generated/mindmap_core.cpp` inclut le runtime actuel de py2x et les classes/méthodes traduites. Le script enregistre les empreintes du Python, du C++ produit et des sources du convertisseur.

La source Python contient une démonstration de console sous `if __name__ == "__main__"`. Pour l'intégrer dans l'application graphique, `core_bridge.hpp` renomme seulement ce `main` au moment de l'inclusion. Cela ne retouche ni le fichier généré ni l'émetteur. Une sonde séparée montre qu'un module ne contenant qu'une fonction, sans entrée, ne génère pas systématiquement de `main` : il ne faut donc pas attribuer ce comportement à tous les modules.

Le runtime est émis de façon monolithique. Cela facilite l'intégration actuelle, mais un découpage bibliothèque/en-tête constituerait une amélioration utile. L'exécutable reste petit grâce à l'édition de liens optimisée.

### 4.3 Compléments manuels

| Besoin | Implémentation manuelle | Pourquoi |
|---|---|---|
| Fenêtre et boucle d'événements | Win32 dans `app.cpp` | py2x n'offre pas de binding graphique |
| Canvas, connexions, textes | GDI+ | rendu natif sans Qt/Python/WebView |
| Saisie | contrôles Windows EDIT Unicode | édition, sélection et saisie natives |
| Recherche accentuée | conversion UTF-8/UTF-16 et `LCMapStringEx` | méthodes de chaîne py2x limitées à l'ASCII |
| Documents | parseur/encodeur JSON typé | `json` Python non pris en charge |
| Disque | `CreateFileW`, `ReadFile`, `WriteFile`, `FlushFileBuffers`, `MoveFileExW` | `open`, `with` et `pathlib` non pris en charge |
| Historique | snapshots sérialisés en chaînes | `copy.deepcopy` non pris en charge |
| Exports SVG/Markdown | sérialisation C++ | fichiers et Unicode gérés côté natif |
| Export PNG | encodeur GDI+ fourni par Windows | API d'image absente du sous-ensemble |
| Mesure des ressources graphiques | `GetGuiResources` | API de diagnostic spécifique à Windows |

Le contrôle de l'arbre n'a pas été recodé une seconde fois dans la GUI. Les boutons appellent les méthodes produites par py2x. La couche de chargement valide et reconstruit les structures pour éviter d'importer un arbre incohérent.

## 5. Portabilité : ce qui est garanti et ce qui ne l'est pas

### 5.1 Données de l'application

Le dossier de base est obtenu depuis le chemin réel de l'exécutable avec `GetModuleFileNameW`. L'application ne s'appuie pas sur `GetCurrentDirectory` pour trouver ses données.

La session est écrite dans `dossier_exe/session.mindmap`. Les boîtes de sauvegarde commencent dans ce dossier. Un utilisateur peut volontairement choisir un autre chemin pour une carte ou un export ; aucune redirection automatique vers AppData n'est effectuée.

Les options des boîtes de dialogue incluent `OFN_NOCHANGEDIR` et `OFN_DONTADDTORECENT`. Le programme ne crée pas d'association de fichier, de raccourci, de service, de tâche planifiée ou de clé de registre. Aucun réseau n'est utilisé par le code de l'application.

Un mutex de session, dont le nom dépend du dossier de l'exécutable, empêche deux fenêtres de ce même dossier de réécrire la session simultanément. Il s'agit d'un objet Windows en mémoire, pas d'un fichier ni d'une installation. Deux copies dans deux dossiers différents peuvent fonctionner indépendamment.

### 5.2 Dépendances exécutables

Les bibliothèques du compilateur sont liées statiquement : aucune `libstdc++`, `libgcc` ou `libwinpthread` externe n'est nécessaire. Les imports PE ont été inspectés. Ils correspondent aux DLL graphiques et système de Windows et à l'UCRT de Windows moderne.

L'application ne contient pas Python, Qt, un navigateur ou une JVM. Elle utilise Segoe UI et les composants de Windows déjà présents sur la machine.

### 5.3 Ce que portable ne signifie pas

Windows peut garder ses traces normales de lancement, de caches, de boîtes de dialogue ou de diagnostic. Le programme ne désactive pas ces mécanismes. Il n'est donc pas affirmé que le système d'exploitation n'écrira jamais rien ailleurs.

Le chemin demandé appartient à OneDrive : OneDrive peut synchroniser les fichiers de ce dossier. Cette synchronisation ne vient pas de MindMap. Une copie sur une clé USB ordinaire fonctionne sans OneDrive.

Une vraie clé USB, son système de fichiers, l'éjection brutale, la panne d'alimentation et un autre PC n'ont pas été testés physiquement. Le test réalisé déplace effectivement le seul exécutable dans un autre dossier comportant accents et espaces, puis le lance depuis un dossier courant différent.

### 5.4 Écriture et échec

Les documents et la session sont écrits dans un `.tmp` voisin, vidés via `FlushFileBuffers`, puis remplacés avec `MoveFileExW`. Une erreur est signalée à l'utilisateur. La sauvegarde automatique réessaie après 30 secondes, sans afficher des fenêtres d'erreur en boucle.

Une session illisible est copiée vers un nom `session-invalide-<compteur>.mindmap` avant reprise normale, si le dossier le permet. Si cette copie est impossible, les tentatives automatiques sont bloquées pour ne pas écraser immédiatement la session problématique ; un enregistrement explicite réactive les essais.

Les opérations reposent sur les garanties disponibles de Windows et du système de fichiers. Elles ne constituent pas une garantie contre une déconnexion matérielle pendant l'écriture. Un `.tmp` peut rester après certains échecs ou une interruption du processus ; cela n'affecte pas le fichier précédent déjà finalisé.

## 6. Format `.mindmap` et validation

Le format est du JSON UTF-8, versionné et indépendant d'un chemin absolu.

```json
{
  "format": "mindmap-portable",
  "version": 1,
  "next_id": 3,
  "selected": 2,
  "view": {"zoom": 0.8, "x": 170, "y": 300},
  "nodes": [
    {"id": 1, "parent": -1, "title": "Sujet", "note": "", "x": 0, "y": 0,
     "color": 6452456, "collapsed": false},
    {"id": 2, "parent": 1, "title": "Une idée", "note": "Explication", "x": 260, "y": 0,
     "color": 6452456, "collapsed": false}
  ]
}
```

L'ordre des enregistrements est un parcours en profondeur. Il conserve l'ordre des frères, y compris après changement de parent. Les listes d'enfants sont reconstruites au chargement à partir de cet ordre et des identifiants de parents.

Le parseur reconnaît les échappements JSON, les paires de surrogates et le BOM UTF-8 facultatif. Il refuse les encodages UTF-8 invalides, les clés JSON dupliquées, les nombres non finis, les virgules terminales et les données supplémentaires après le document.

### Limites effectives

| Élément | Limite |
|---|---|
| Fichier | 8 Mio à la lecture et à l'encodage |
| Nombre d'idées | 2 000 |
| Profondeur métier | 63 sous le sujet central |
| Imbrication du parseur JSON | 70 |
| Taille d'un tableau JSON | 10 000 éléments, puis validation métier à 2 000 idées |
| Titre | 1 024 octets UTF-8 ; saisie UI limitée à 256 unités UTF-16 |
| Note | 16 000 octets UTF-8 ; saisie UI limitée à 8 000 unités UTF-16 |
| Couleur | 0 à 0xFFFFFF |
| Coordonnées des idées | ±1 000 000 |
| Vue | zoom 0,2 à 3 ; translation ±10 000 000 |
| PNG | côté maximal 8 192 pixels et environ 16 millions de pixels |
| Historique | 80 états et budget d'environ 32 Mio pour la pile Annuler |

La pile Rétablir provient des états annulés ; une nouvelle modification l'efface. Ce budget concerne les chaînes des snapshots et ne mesure pas l'intégralité de la mémoire du processus.

Les parents absents, identifiants dupliqués, boucles, branches orphelines, mauvaises racines et profondeurs excessives sont rejetés. Le compteur `next_id` doit être supérieur aux identifiants présents. La couche UI ajoute une garde proche de la borne de la plage d'identifiants du document.

Les positions, couleurs et plis sont conservés. L'historique n'est pas sauvegardé : il appartient uniquement à la session en cours du processus.

## 7. Gestion mémoire et ressources

### 7.1 Objets traduits

Le runtime C++ actuel utilise `shared_ptr` pour les classes et les poignées de listes/dictionnaires. Cela conserve les alias Python : une copie d'une liste ne signifie pas une copie des objets qu'elle contient.

Dans cette application, le dictionnaire possède les objets `MapNode`. Les champs parent/enfants stockent des **identifiants**, pas des `shared_ptr` vers d'autres objets. Le modèle de propriété est donc acyclique.

Les variables temporaires peuvent garder un nœud vivant après retrait du dictionnaire. C'est attendu. L'objet disparaît après destruction de la dernière référence propriétaire. Les tests vérifient ce cas avec `weak_ptr`.

Il n'y a pas d'allocateur pool ou arena imposé aux nœuds traduits. Remplacer arbitrairement les poignées par des pointeurs de pool modifierait leurs durées de vie et pourrait créer des références pendantes. Les ressources sont gérées par RAII ; c'est le choix adapté au modèle actuel.

### 7.2 Ressources natives

Les pinceaux, chemins, fontes et bitmaps GDI+ temporaires ont une durée de vie de portée. Le fond est dessiné dans un bitmap pour éviter le scintillement. La fonte et le pinceau des champs Windows sont libérés par le destructeur de l'application.

Les HANDLE de fichiers et du mutex sont encapsulés par un objet RAII. Les menus contextuels sont détruits après utilisation. Les tableaux JSON, codecs et snapshots utilisent les conteneurs standard.

Le test natif répète les dessins et compare le nombre d'objets GDI du processus. Pour la construction finale testée, ce nombre reste **27 avant / 27 après 40 redessins**.

### 7.3 Ce que ces tests ne prouvent pas

L'expiration de références faibles ne mesure pas tous les octets alloués. La stabilité du nombre d'objets GDI ne prouve pas l'absence de toute fuite native. ASan/UBSan, un profileur de tas et un long test de charge sur un autre environnement compléteraient ces vérifications.

L'ASan de MinGW n'a pas été utilisé ici. Il ne faut pas présenter les tests actuels comme une certification d'absence de fuite pour tous les cas.

## 8. Tests exécutés et preuves

### 8.1 Comparaison différentielle Python/C++

`tests/differential.py` construit des programmes indépendants à partir du modèle et d'opérations pseudoaléatoires reproductibles. Il compare directement la sortie Python à celle d'un binaire C++ compilé depuis py2x.

Résultat : **8 graines, 640 ajouts, 800 opérations, 656 lignes de sortie identiques**.

Les opérations couvrent le repli, la révélation des parents, le déplacement de branche, la propagation de couleurs, les changements de parent autorisés/refusés et la réorganisation. Les identifiants, parents, titres, coordonnées, couleurs, plis et enfants sont comparés.

Ce résultat concerne ces scénarios et ce modèle. Il n'est pas une mesure du taux de réussite général du convertisseur.

### 8.2 Tests C++ du modèle et des documents

`core_tests.cpp`, enregistré dans CTest sous `core_and_documents`, passe.

Cas vérifiés : ajout, profondeur, visibilité, révélation, duplication, changement de parent, refus de cycle, propagation de couleur, suppression protégée de la racine, titre vide, aller-retour de document, accents/emojis, échappements XML, exports textuels, documents mal formés, surrogates JSON, UTF-8 invalide, taille de fichier et profondeur.

Le test répète **100 cycles de 100 idées** avec sérialisation/désérialisation et vérification de destruction via références faibles. Ce n'est pas un test de 2 000 idées avec une interface interactive.

### 8.3 Test de l'application graphique

Le mode de test interne `--smoke-test` initialise réellement une fenêtre et des champs natifs, effectue des actions applicatives, capture un rendu et écrit des exports.

Il vérifie la synchronisation des notes, la duplication, Annuler/Rétablir, les plis, la disposition, l'aller-retour JSON, le PNG, le SVG, le Markdown et les ressources GDI.

Résultat final : `ok: true`, **11 idées**, **27 / 27 objets GDI**.

Le test se ferme automatiquement. Il produit ses fichiers à côté de la copie de l'exécutable utilisée pour ce test. Il n'est pas prévu comme mode d'utilisation ordinaire.

### 8.4 Essai via l'interface utilisateur

La fenêtre a été lancée et inspectée. Les essais ont inclus :

- création d'une idée avec Tab ;
- renommage en « Révision de géographie » ;
- validation par Ctrl+Entrée ;
- édition du champ de notes ;
- recherche « GÉOGRAPHIE » retrouvant le titre accentué ;
- dialogue Enregistrer sous ;
- enregistrement effectif de `Test-interface.mindmap` ;
- fermeture normale.

Cet essai a révélé un défaut de synchronisation des notes lorsqu'une édition était effectuée programmatiquement sans notification `EN_CHANGE`. Il a été corrigé dans **l'application C++**, en relisant toujours les contrôles avant validation/sauvegarde. Le test natif de la construction finale force précisément cette situation et vérifie la note sérialisée.

Les interactions de glisser-déposer/changement de parent par la souris ne sont pas toutes couvertes par cet essai manuel. La règle métier correspondante est couverte par les tests différentiels et C++.

### 8.5 Exécutable réellement déplacé

`portable_check.py` copie uniquement `MindMap.exe` dans un dossier nommé `test clé USB/cartes`, puis le lance depuis un autre dossier courant. Aucune DLL additionnelle n'est présente.

Le test vérifie le rapport graphique, la note UTF-8 sérialisée, l'absence de `.tmp` restant après une exécution réussie et l'absence de dépendances dynamiques au compilateur.

La taille et l'empreinte de la construction finale sont conservées dans `tests/preuves/portable-result.json`. L'exécutable de la livraison est comparé à cette construction lors de la copie.

### 8.6 Preuves conservées

| Fichier dans `sources/tests/preuves` | Contenu |
|---|---|
| `differential-result.json` | compte des graines/opérations/lignes et résultat d'égalité |
| `converter-probes.json` | sources minimales, refus et divergences du convertisseur |
| `portable-result.json` | taille, SHA-256, imports DLL, déplacement, GDI |
| `test-interface.png` | capture produite par le test natif final |
| `test-carte.png` | export image natif |
| `test-carte.svg` | export SVG natif |
| `test-plan.md` | export du plan |
| `test-carte.mindmap` | document de test natif |
| `ctest.txt` | résultat CTest de la construction finale |

## 9. Limites du convertisseur réellement reproduites

Les sondes sont exécutables par `tests/converter_probes.py`. Aucun patch de py2x n'accompagne cette livraison.

### 9.1 Unicode : divergence silencieuse de longueur

```python
def main() -> None:
    print(len("é🗺"))

if __name__ == "__main__":
    main()
```

Python affiche **2**. Le C++ produit affiche **6**, soit le nombre d'octets UTF-8. La conversion est acceptée. C'est une divergence démontrée, pas seulement une supposition.

Conséquence pour MindMap : ne pas utiliser le `len` traduit pour limiter ou découper les caractères utilisateur. La couche native valide UTF-8, convertit en UTF-16 pour la GUI et impose des limites explicitement en octets.

Piste de patch : choisir et documenter une représentation Unicode. Au minimum, refuser explicitement les opérations non ASCII non supportées au lieu de les accepter avec une promesse implicite de conformité Python. Pour une vraie implémentation, gérer les points de code et leurs frontières UTF-8, puis définir séparément les graphèmes.

Tests d'acceptation recommandés : `len`, indexation, slices, `find`, `count`, `lower`, `upper`, `strip`, ord/chr, chaîne vide, caractères hors BMP, caractères combinants et entrées UTF-8 invalides. Ne pas assimiler point de code et graphème visuel.

### 9.2 Unicode : casse non ASCII

```python
def main() -> None:
    print("ÉCOLE".lower())

if __name__ == "__main__":
    main()
```

Python : **école**. C++ traduit : **École**. La lettre accentuée n'est pas transformée.

MindMap utilise donc `LCMapStringEx` côté Windows pour sa recherche. Cela convient aux textes testés, sans prétendre reproduire toutes les nuances du `casefold` Python ou supprimer les accents.

Une recherche « ecole » sans accent n'est pas actuellement équivalente à « école ». Une normalisation sans accents serait une fonctionnalité applicative supplémentaire.

### 9.3 Références circulaires : destruction différée indéfiniment

```python
from typing import Optional

class Link:
    def __init__(self) -> None:
        self.other: Optional[Link] = None

    def connect(self, other: "Link") -> None:
        self.other = other
```

La conversion est acceptée. Un harness natif crée `x`, appelle `x.connect(x)`, conserve une `weak_ptr`, puis détruit la référence externe. Le résultat observé est :

```text
retained-cycle
released-after-breaking
```

L'objet reste vivant à cause de son propre `shared_ptr`. Après rupture manuelle du cycle, il est détruit. Python possède une collecte des cycles ; le runtime C++ actuel n'en propose pas l'équivalent.

MindMap évite ce problème par des parent/enfants entiers. Aucun patch automatique transformant tous les liens parents en `weak_ptr` n'a été appliqué : une telle transformation changerait la sémantique pour d'autres programmes.

Piste de patch : contrat explicite sur les graphes de propriété, diagnostic sur certains cycles évidents, support de références faibles typées ou collecteur de cycles adapté au runtime. Les solutions doivent être évaluées sur les alias et les graphes arbitraires, pas uniquement sur les arbres.

### 9.4 JSON

Sonde : `import json` puis `json.loads`.

Refus exact observé :

```text
ligne 1 : module json non supporté (supportés : math, string, sys)
```

Le chargement JSON retourne habituellement des structures dynamiques ; un simple mapping vers une bibliothèque C++ ne suffit pas à préserver le typage de l'IR.

MindMap utilise un parseur C++ et un schéma validé. Piste : commencer par un format JSON typé avec dataclasses/schémas, erreurs de parse, Unicode et bornes, avant de promettre le chargement de tout JSON arbitraire.

### 9.5 Fichiers et `with`

```python
def save(s: str) -> None:
    with open("map.json", "w", encoding="utf-8") as f:
        f.write(s)
```

Refus exact : `ligne 2 : instruction non supportée : With`.

Ce refus ne constitue pas un test isolé de `open`, puisque `with` est rencontré d'abord. Le README de py2x indique également que les fichiers ne font pas partie du sous-ensemble ; un prochain travail doit les tester séparément.

MindMap fait l'I/O manuellement. Piste : abstraction typée de fichier texte, RAII, modes et exceptions précis, puis support des gestionnaires de contexte reconnus. Les chemins Unicode Windows sont nécessaires pour une application française portable.

### 9.6 Chemins : `pathlib`

Sonde : `from pathlib import Path` et `Path(__file__).parent`.

Refus : module `pathlib` non supporté, dès la ligne 1. MindMap obtient le dossier de l'exécutable par Win32. `__file__` Python n'est pas une définition portable de l'emplacement d'un binaire natif.

Piste : décider explicitement ce que signifient `__file__`, le dossier de ressources et le dossier de données dans un programme compilé. Un modèle configurable de déploiement serait préférable à une approximation silencieuse.

### 9.7 Interface graphique : `tkinter`

L'import `tkinter` est refusé dès la ligne 1. Le convertisseur ne transforme pas un toolkit Python en toolkit natif.

Le choix ici est Win32/GDI+, écrit à la main. Piste : une frontière FFI documentée pour exposer les classes et fonctions traduites à une application native. Un support générique de `tkinter` complet serait un chantier distinct.

### 9.8 Copie profonde et historique

`import copy` et `copy.deepcopy` sont refusés dès la ligne 1.

MindMap sérialise puis reconstruit des snapshots. Ceux-ci sont indépendants : ils ne partagent pas les objets du document précédent.

Piste : support d'une copie profonde avec table de mémoïsation, préservant les alias et évitant la récursion infinie. Copier simplement chaque `shared_ptr` serait une copie superficielle, incorrecte pour cette fonction.

### 9.9 Références faibles

`import weakref` est refusé dès la ligne 1. Les tests utilisent `weak_ptr` dans un harness natif, pas dans le Python traduit.

Piste : type explicite de référence faible, construction, verrouillage et tests de disparition. Cela aiderait les applis et les diagnostics mémoire, sans régler automatiquement la collecte de cycles.

### 9.10 Diagnostic des imports

Les messages d'import simple citent parfois seulement `math, string, sys`, alors que le convertisseur accepte davantage de modules dans d'autres chemins et dans son README. Un `from ... import ...` peut afficher une liste différente.

Il s'agit d'une incohérence de diagnostic observée dans les sondes, pas d'une preuve que tous les modules annoncés sont indisponibles. Piste : une liste des capacités partagée par tous les chemins d'import et les messages d'erreur.

## 10. Priorités proposées pour une future conversation

| Priorité proposée | Travail | Critère de succès |
|---|---|---|
| Haute | Contrat Unicode, refus explicites ou implémentation correcte | sondes de longueur et casse conformes ou refusées clairement |
| Haute | Contrat de mémoire et cycles | cas auto-référence documenté, détecté ou collecté sans altérer les alias |
| Haute | Mode bibliothèque / frontière d'intégration | code traduit inclus/lié sans collision d'entrée et avec ABI documentée |
| Moyenne | Fichiers texte et chemins Unicode | tests de lecture/écriture, erreurs et fermetures sur exception |
| Moyenne | JSON typé | validation, surrogates, erreurs, aller-retour déterministe |
| Moyenne | Copies profondes | snapshots indépendants avec préservation des alias internes |
| Moyenne | `weakref` typé | observation correcte de destruction sans garder l'objet vivant |
| Basse | Diagnostics d'imports | listes de capacités cohérentes et exactes |

La GUI native complète n'est pas proposée comme première priorité de py2x. Une bonne intégration de la logique à une GUI existante apporte déjà de la valeur et limite la complexité.

### Règles à conserver lors d'un patch futur

1. Ne pas retoucher manuellement les fichiers C++ générés pour masquer une erreur.
2. Conserver des tests différentiels contre Python et des reproductions minimales.
3. Distinguer refus explicite, divergence silencieuse et API native simplement hors du périmètre.
4. Préserver les alias des listes, dictionnaires et objets.
5. Tester les erreurs, pas seulement les sorties nominales.
6. Vérifier que les changements passent aussi Kotlin lorsqu'ils touchent le frontend ou l'IR.
7. Régénérer MindMap et refaire les essais du modèle, des documents et de la GUI.

## 11. Défauts applicatifs rencontrés et corrigés ici

Ces corrections concernent uniquement le nouveau projet :

| Problème observé | Correction |
|---|---|
| En-têtes GDI+ incomplets avec MinGW | inclusion explicite d'`objidl.h` |
| Constante du champ de recherche absente | inclusion de `commctrl.h` |
| Appel incorrect de `SolidBrush::GetColor` | utilisation directe de la couleur |
| Fenêtre initiale trop petite en unités logiques à DPI élevé | taille initiale calculée depuis le DPI et l'espace de travail |
| Recherche peu visible | bordure du contrôle et Common Controls v6 intégrés au manifeste |
| Dernière ligne du plan dépassant le panneau | borne des lignes visibles et du défilement corrigée |
| Notes modifiées sans notification non synchronisées | relecture des contrôles avant validation/sauvegarde ; test de régression |
| Risque de cartes écrites dépassant la limite de lecture | même borne de 8 Mio à l'encodage |
| Réessais de sauvegarde trop fréquents en erreur | délai de reprise automatique de 30 secondes |
| Texte long pouvant sortir des nœuds SVG | découpe UTF-8 en lignes et zone de clipping |

Le texte SVG est découpé en points de code approximatifs, pas en graphèmes ni en lignes mesurées par une police. Le titre complet reste dans le document et dans la balise `title` de l'export. Cette limitation est applicative et ne modifie pas py2x.

## 12. Limites de cette version de MindMap

- Arbre hiérarchique uniquement : pas de relations transversales entre deux branches.
- Pas d'import XMind/FreeMind, ni d'images/fichiers attachés aux nœuds.
- Pas de collaboration, cloud applicatif, compte ou synchronisation propre à MindMap.
- Une fenêtre par dossier ; les onglets multiples ne sont pas implémentés.
- L'historique repart à zéro après fermeture.
- Les exports PNG/SVG montrent les branches visibles ; le Markdown inclut tout.
- Les emojis sont stockés correctement, mais leur rendu GDI+ dépend des polices et peut afficher un symbole de remplacement.
- Les contrôles de saisie exposent des éléments d'accessibilité ; les boutons et le canvas sont dessinés et n'ont pas de provider UI Automation complet.
- Aucune validation sur un PC distinct, une vraie clé USB, un disque plein ou un retrait brutal.
- Aucune mesure de performance interactive à la limite de 2 000 idées.
- L'application utilise des itérations et des recherches pouvant devenir coûteuses sur de très grandes cartes ; la limite de taille borne le problème sans constituer une optimisation complète.
- Le dossier doit être accessible en écriture pour la session. Le mode lecture seule affiche un échec au lieu de rediriger vers le PC.
- Le support attendu est Windows x64 moderne. Le test réel a été effectué sur le Windows de cette machine, pas sur toutes les versions annoncées par les API.

## 13. Reproduire la construction et les tests

Pré-requis de développement utilisés : CMake, MinGW g++ C++20 et, pour les scénarios Python/génération seulement, Python 3.12.14. L'utilisateur final n'a besoin d'aucun de ces outils.

Depuis le dossier `sources` de la livraison :

```powershell
cmake -S . -B build-final -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-final -j 2
ctest --test-dir build-final --output-on-failure
```

Cette construction utilise le C++ généré livré, sans relancer py2x.

Pour régénérer depuis Python avec le convertisseur du workspace :

```powershell
python tools/generate.py "C:\Users\gabor\code\ia\transpiler"
```

Pour les vérifications complémentaires :

```powershell
python tests/differential.py g++ "C:\Users\gabor\code\ia\transpiler"
python tests/converter_probes.py "C:\Users\gabor\code\ia\transpiler"
python tests/portable_check.py
```

Si Python n'est pas dans PATH, remplacer `python` par son chemin. Le runtime Python utilisé pendant ce travail était :

```text
C:\Users\gabor\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe
```

Les fichiers du convertisseur se trouvent dans `C:\Users\gabor\code\ia\transpiler\py2x`. Le projet de travail est `C:\Users\gabor\code\ia\projects\mindmap_portable`.

Le script `tools/build.ps1` construit le projet ; il ne force pas de régénération sans chemin de convertisseur explicite. Ne pas exécuter un mode test sur la seule copie de documents utilisateur : utiliser un dossier de test distinct.

## 14. Transmission à une autre conversation

Une prochaine conversation peut partir des quatre éléments suivants :

1. `python/mindmap_core.py` : modèle réel utilisé par l'application.
2. `generated/provenance.json` : version exacte des sources py2x testées, identifiée par leurs empreintes.
3. `tests/preuves/converter-probes.json` : refus et divergences minimales reproductibles.
4. `tests/differential.py` et `core_tests.cpp` : critères de non-régression applicatifs.

Les observations Unicode et cycles sont démontrées. Les modules GUI, JSON et fichiers sont hors périmètre et ont été complétés manuellement. La différence doit rester claire pour éviter de traiter un choix d'intégration comme un bug d'émetteur.

La priorité utile est de renforcer le contrat du convertisseur et son intégration, puis de régénérer l'application. Le programme portable livré permet déjà de créer, modifier, sauvegarder et exporter des cartes sans installer d'outil de développement.
