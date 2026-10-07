# MindMap v3 — accueil et cartes portables

Branche locale : `feature/portable-workspace`. La version 3 est implémentée
dans les sources ; aucun nouvel exécutable Windows n'a été publié.

## Le nouveau parcours

1. L'application ouvre un accueil, avec les cartes récentes, les brouillons
   et les modèles. Elle ne restaure plus silencieusement `session.mindmap`.
2. « Nouvelle carte » ou un modèle demande le sujet. Les modifications sont
   protégées dans un brouillon identifiable à l'accueil.
3. Le premier enregistrement choisit un nom et un emplacement. Ensuite,
   la sauvegarde automatique écrit dans ce même fichier ; son emplacement
   est affiché dans l'interface. Le brouillon est retiré après enregistrement.
4. Changer de carte ou revenir à l'accueil protège d'abord la carte actuelle.
   Une erreur de sauvegarde empêche le changement de carte.
5. L'ancienne session est récupérable explicitement. Une seule copie de
   récupération est créée, même si la migration doit être retentée. Après
   récupération, l'original est archivé. Un fichier illisible reste intact.

## Fonctionnalités ajoutées

- Images et schémas en PNG, JPEG ou BMP, ajoutés à l'idée sélectionnée ou par
  glisser-déposer. L'image est normalisée en PNG (640 pixels au maximum),
  intégrée au document, prise en compte dans les dispositions et exportée
  dans le PNG, SVG et PDF. L'annulation et la duplication conservent les images.
- Dix versions de secours au maximum par carte, avec restauration depuis
  Fichier. Les sauvegardes automatiques espacent les versions de cinq minutes ;
  un enregistrement manuel crée aussi une version si le contenu a changé.
- Modèles intégrés : carte vide, chapitre, dissertation, chronologie et
  comparaison. Les modèles personnels se créent depuis une carte existante.
- Import FreeMind `.mm`, XMind `.xmind` (anciens XML et nouveaux JSON,
  archives stockées ou compressées), Markdown et texte. Plusieurs feuilles
  XMind deviennent plusieurs branches.
- Export PDF A4 paysage à partir du rendu, avec images ; PDF rasterisé.
- Conservation de l'ordre des branches lors du chargement, y compris après
  un changement de parent : cet ordre suit aussi les modèles et imports.

Le format passe à la version 3 pour les images. Les versions 1 et 2 restent
lisibles. Les styles et illustrations des fichiers FreeMind/XMind ne sont
pas repris ; les titres, branches et notes le sont. Les liens sont repris
depuis FreeMind et XMind JSON.

## Vérifications effectuées

- Compilation GCC 14.2 avec `-Wall -Wextra -Werror`, puis réussite de la suite
  existante : modèle, documents, liens, dispositions, plans, exports,
  données invalides et cent cycles de libération de mémoire.
- Nouvelle suite réussie : écritures de fichiers réels dans un dossier
  temporaire, dédoublonnage des cartes récentes, sauvegarde sans nouveau
  fichier, conflits avec des modifications externes, fichier disparu,
  conservation et limite des versions, brouillons, modèles et déplacement
  d'un dossier portable.
- Vérifications des images : encodage, dimensions et CRC PNG, conservation
  dans le format v3, duplication et suppression, SVG et dispositions sans
  chevauchement avec images.
- Imports FreeMind, XMind XML, XMind JSON, liens et notes ; archive DEFLATE
  réelle ; rejet des archives corrompues et XML invalide ; carte XMind à
  63 niveaux importée avec succès.
- Structure PDF et offsets de table vérifiés ; ouverture avec pypdf et rendu
  avec MuPDF réussis, y compris la couleur de l'image embarquée. Le codage
  d'image GDI+ et le rendu Windows ne sont pas testables depuis cet environnement.
- Les vérifications AddressSanitizer et UndefinedBehaviorSanitizer passent
  pour la suite des nouvelles fonctions. LeakSanitizer reste désactivé,
  car le traçage de l'environnement le rend indisponible.
- Le découpage UTF-8 reste compatible avec les 30 656 cas de comparaison
  de la première optimisation. Le cœur py2x généré n'a pas été modifié.

## Validation Windows restante

Cet environnement Linux n'a ni MinGW, ni CMake, ni Wine. L'application Win32
n'a donc pas été compilée et l'interface n'a pas été lancée ici.

Le smoke-test Windows a été étendu : image ajoutée, export PDF, sauvegarde
automatique dans le fichier d'origine, versions, modèle personnel et accueil.
Le workflow `.github/workflows/windows.yml` prépare une compilation MinGW,
les tests et ce smoke-test dans un dossier isolé, puis conserve l'exécutable,
les captures et les rapports en artefacts. Ce workflow n'a pas encore été
exécuté : les changements restent locaux.

Miniz 3.1.0 est fourni dans `sources/vendor/miniz/`, avec sa licence MIT.
Seul son décompresseur DEFLATE est compilé ; l'application reste sans
dépendance réseau ni DLL ajoutée à distribuer.
