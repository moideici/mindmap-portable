# Plan de fusion MindMap ↔ Nemo

**But** : faire de MindMap l'endroit où l'on *construit* ses cours (cartes, sous-cartes), et de Nemo l'endroit où on les *révise* (séances, exercices, répétition espacée). MindMap n'a plus de mode révision : tout ce qui touche à la mémorisation passe par Nemo.

> Établi par lecture du code de `nemo-https` (`~/Dev/projet/nemo-https`). Le serveur MCP Nemo était déconnecté pendant cette session (`CONNECT_TIMEOUT`) : rien n'a été vérifié sur des données réelles. À contrôler avant la phase 2.

## 1. Ce que Nemo est aujourd'hui (utile pour la fusion)

| Élément Nemo | Détail | Intérêt pour MindMap |
|---|---|---|
| Cours | Bucket R2 `NEMO_COURSES` (perso) et `NEMO_CLASS_COURSES` (classe). Clés `<matière>/<fichier>`, extensions `.pdf .docx .pptx .txt .md`. Un `<fichier>.txt` compagnon contient le texte extrait. | Un `.md` déposé dans le dossier de matière est un cours à part entière. |
| Synchronisation | `scripts/sync-courses.js` / `sync-daemon.js` copient `Documents/onedrive/cours/<matière>` vers R2 (dossiers listés dans `data/sync-folders.json`, dont `hist géo`). | Le dossier `hist géo` est déjà surveillé : c'est le point d'entrée le plus simple. |
| Dépôt direct | `PUT /api/admin/courses/<matière>/<fichier>` (jeton `Authorization: Bearer`, profil admin). Historique `added/updated/removed` par fichier. | Permet de publier sans démon, mais impose de gérer un jeton. |
| Notions | `stableItemId(matière, front)` : identifiant stable d'une carte-mémoire ; `MasteryState` (maîtrise, boîte, prochaine révision). `GET /api/progression`, `/api/progression/due`, tools MCP `get_progression`, `suggest_focus`, `generate_flashcards`. | Une idée de carte peut recevoir son niveau de maîtrise **sans que MindMap ne gère aucune révision**. |
| Fiches / exercices | Fiches en blocs, quiz, listes algorithmiques, séances hybrides, génération IA. | MindMap ne génère rien : Nemo le fait à partir du cours exporté. |
| Accès | Application web, application Android, serveur MCP HTTP. | Une carte visible dans Nemo est visible sur téléphone. |

## 2. Ce qui existe déjà côté MindMap (phase 0, faite)

- `mindmap-cli nemo-config --courses <dossier>` puis `nemo-export <carte>` : toute la hiérarchie → un Markdown « *<titre> (carte mentale).md* » (une section par sous-carte).
- *Fichier > Exporter pour Nemo…* dans l'application.
- `import-course --split N` : cours Markdown (par ex. lu avec `read_course`) → carte avec sous-cartes.
- Recherche globale, `search`, `tree`, `validate`.

## 3. Phases proposées

### Phase 1 — Publication automatique par fichier (petit, sans risque)
Objectif : *enregistrer une carte* suffit pour que Nemo la voie.
- Option « Publier vers Nemo à l'enregistrement manuel » (pas à l'autosauvegarde, pour ne pas polluer l'historique de changements de Nemo avec une mise à jour toutes les cinq secondes).
- `nemo-export --all <dossier>` : exporte toutes les cartes racines d'un dossier (celles qui ne sont la sous-carte de personne) ; `orphans` évite d'exporter une sous-carte comme cours indépendant.
- Nom de fichier stable (déjà le cas) et en-tête « généré, ne pas modifier » (déjà le cas).
- **Critères de réussite** : après un enregistrement, le fichier apparaît dans `list_courses` après la synchro ; un renommage de carte supprime l'ancien `.md` généré (via un petit manifeste `.mindmap-data/nemo-exports.json`).
- **Risque** : doublon avec un cours Word du même sujet → Nemo lit les deux. Atténuation : dossier ou préfixe dédié (`hist géo/cartes/…`) à valider côté Nemo.

### Phase 2 — Dépôt direct sans démon (moyen)
Objectif : publier depuis un PC sans le démon de synchro, y compris vers les **cours de classe**.
- Client HTTP (WinHTTP) dans `mindmap-cli` et l'application : `PUT /api/admin/courses/…` ou `/admin/classe/…`.
- Jeton stocké dans le Gestionnaire d'identification Windows (jamais dans `.mindmap-data` ni dans le dépôt), saisi une fois.
- File d'attente hors-ligne : si le réseau manque, l'export reste dans `.mindmap-data/nemo-queue/` et part à la prochaine ouverture.
- **Critères** : publication d'un cours de 50 Ko ; plafond `MAX_TOTAL_BYTES` (413) géré avec un message clair ; aucun secret dans les journaux.
- **Risque** : l'application est « portable, sans compte, sans connexion » : cette phase doit rester **facultative et désactivée par défaut**.

### Phase 3 — Maîtrise Nemo affichée sur la carte (moyen, le plus utile)
Objectif : voir sur la carte ce qui est acquis ou à revoir selon Nemo, sans remettre de mode révision dans MindMap.
- Convention : le recto d'une carte-mémoire Nemo = titre d'idée (normalisé comme `stableItemId`). `generate_flashcards` reçoit déjà le cours exporté ; on s'aligne sur des titres d'idées.
- Lecture seule : `mindmap-cli nemo-mastery <carte>` interroge `GET /api/progression` et pose les repères existants (✓ acquis, ? à revoir) **ou** une pastille de maîtrise séparée qui ne touche pas aux repères que l'utilisateur a posés à la main (à trancher, voir §5).
- Affichage : badge de maîtrise sur les idées + « à revoir aujourd'hui » dans **Cartes ▾**.
- **Critères** : une idée dont la carte Nemo est en boîte 1 s'affiche « à revoir » ; aucune écriture vers Nemo.
- **Risque** : fragilité de l'appariement par titre si l'idée est renommée → conserver un identifiant par idée (voir phase 4).

### Phase 4 — Identifiants stables et cours « natif » côté Nemo (grand, côté Nemo surtout)
- Écrire dans le fichier `.mindmap` un identifiant d'idée stable (UUID court) et l'exporter en commentaire HTML dans le Markdown (`<!-- id:xxxx -->`) : l'appariement survit aux renommages.
- Côté Nemo : ajouter `.mindmap` à `SUPPORTED_EXTENSIONS` (scripts) et `COURSE_SOURCE_EXTENSIONS` (Worker) + un extracteur `src/extract/mindmap.js` qui produit le même texte que `nemo-export` (nécessite de résoudre les sous-cartes du même dossier). Avantage : l'**original** reste une carte, Nemo peut l'afficher (SVG exporté par `mindmap-cli export --to x.svg`) sur le site et sur Android.
- Nouveaux tools MCP possibles : `list_mindmaps`, `read_mindmap` (JSON arborescent identique à `show --json`).
- **Risque** : double implémentation de l'aplatissement (C++ et JS) → figer le format par des tests croisés (mêmes fichiers `.mindmap` de référence, même sortie attendue).

### Phase 5 — Boucle d'agent complète (peu de code, surtout des consignes)
Un agent qui dispose des deux outils (MCP Nemo + `mindmap-cli`) peut déjà :
1. `read_course` → `import-course --split 2` (cours Word → cartes) ;
2. `suggest_focus` / `get_progression` → `set --marker revoir` sur les idées faibles ;
3. `nemo-export` → `generate_flashcards` / `generate_mixed_session` sur le cours à jour.
À livrer : un guide `docs/AGENTS.md` avec ces trois recettes et le jeu de commandes minimal.

## 4. Ordre recommandé

| Ordre | Phase | Taille | Dépend de |
|---|---|---|---|
| 1 | Phase 1 (publication à l'enregistrement) | S | rien |
| 2 | Phase 5 (guide d'agent) | S | rien |
| 3 | Phase 3 (maîtrise affichée) | M | phase 1, endpoint de lecture |
| 4 | Phase 2 (dépôt direct, facultatif) | M | rien (utile avant la 3 si pas de démon) |
| 5 | Phase 4 (identifiants stables, extracteur Nemo) | L | phases 1 et 3 |

## 5. Décisions à prendre avant de coder

1. **Source de vérité** : la carte (le `.md` est jetable, régénéré) — recommandé — ou Nemo ?
2. **Portée** : cours personnels seulement, ou aussi cours de classe (partagés avec les élèves) ?
3. **Maîtrise** : repères existants (risque d'écraser ✓/? posés à la main) ou pastille séparée (recommandé) ?
4. **Dossier cible** : mélangé aux cours Word de `hist géo`, ou sous-dossier `cartes/` ?
5. **Jeton** : acceptable de stocker un jeton Nemo dans le Gestionnaire d'identification Windows ? Sinon, rester sur la phase 1 (démon de synchro).
6. **Android** : la carte doit-elle être consultable sur téléphone via Nemo (phase 4 nécessaire) ou le Markdown suffit-il ?

## 6. Tests et garde-fous

- Export : test croisé « carte de référence → Markdown attendu » (déjà couvert en CLI par `cli_check.py`) étendu aux sous-cartes manquantes, boucles et titres vides.
- Aucune écriture dans le dossier des cours autre que le fichier généré (test : le dossier contient les mêmes autres fichiers avant et après).
- Réseau : tous les appels ont un délai maximal, un message d'erreur lisible et sont désactivés par défaut.
- Rétrocompatibilité : une carte sans sous-carte ni Nemo reste en `version 3` et lisible par les anciennes versions.

## 7. Avancement (7 octobre 2026)

Le serveur MCP Nemo était toujours déconnecté : les échanges réseau sont testés contre un **faux serveur Nemo local** (`sources/tests/nemo_check.py`, 4 tests CTest verts), pas contre le vrai.

| Phase | État |
|---|---|
| 1. Publication par fichier | **Fait.** `nemo-export --all <dossier>` (cartes racines seulement), manifeste `.mindmap-data/nemo-exports.json` (une carte renommée remplace son ancien cours, une carte supprimée retire le sien, jamais un fichier étranger), option *Fichier > Publier vers Nemo à l'enregistrement (Ctrl+S)* (jamais à l'autosauvegarde, jamais par le réseau). |
| 2. Dépôt direct sans démon | **Fait, facultatif et désactivé par défaut.** `nemo-remote`, `nemo-login` (Gestionnaire d'identification Windows), `nemo-push` (fichier + texte compagnon sous `<matière>/cartes/`, file d'attente hors-ligne, 413 et jeton refusé expliqués, https obligatoire). Pas d'interface graphique : CLI seulement. |
| 3. Maîtrise Nemo | **Fait côté Nemo** (`POST /api/mastery/lookup`) **et côté CLI** (`nemo-mastery`, lecture seule, pastille séparée des repères manuels : décision 3). **Badges dans la fenêtre : fait** (*Affichage > Badges de maîtrise Nemo*, lecture d'un cache écrit par `nemo-mastery --cache` ; la fenêtre n'utilise pas le réseau). |
| 4. Identifiants stables, `.mindmap` comme cours Nemo | **Fait, autrement que prévu.** (a) Nemo synchronise les fichiers `.mindmap` comme cours (`src/extract/mindmap.js`, `scripts/lib/extract.js`) : carte + sous-cartes = un Markdown **identique** à l'export C++ (test croisé sur les mêmes fichiers) ; une sous-carte contenue par une autre carte n'est pas un cours à part ; modifier une sous-carte met à jour le cours de la racine. (b) Plutôt qu'un UUID dans le format de fichier (changement de format, extracteur JS dépendant), MindMap retient pour chaque idée (fichier + numéro, stables au renommage) ses anciens titres dans `.mindmap-data/nemo-ideas.json` ; `nemo-mastery` les interroge aussi et affiche l'historique retrouvé (`ancien titre`). Limite : un renommage n'est suivi que si la carte a été publiée ou interrogée (`nemo-export`, `nemo-push`, Ctrl+S avec publication automatique, `nemo-mastery`) avant. Pas d'outils MCP `list_mindmaps`/`read_mindmap` : les clés Nemo sont plates (`matière/nom`), les sous-cartes ne s'y résolvent pas ; `read_course` lit le texte de la carte. |
| 5. Boucle d'agent | **Fait** : `docs/AGENTS.md` (3 recettes) ; côté Nemo, `/nemo-prompt-fiche` lit les cartes mentales. |

Décisions prises par défaut (modifiables) : la carte est la source de vérité, le `.md` est jetable (1) ; cours personnels par défaut, `--scope class` pour la classe (2) ; pastille séparée (3) ; sous-dossier `cartes/` côté réseau (4) ; jeton dans le Gestionnaire d'identification, jamais dans un fichier (5) ; le Markdown suffit sur Android tant que la phase 4 n'existe pas (6).
