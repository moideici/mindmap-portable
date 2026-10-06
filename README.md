# MindMap portable

Lancez **MindMap.exe**. Aucune installation, aucun compte et aucune dépendance à Python pour utiliser l'application.

Le dossier peut être copié sur une clé USB. L'application conserve sa session dans `session.mindmap`, à côté de l'exécutable. Les sources et les documents d'aide peuvent être retirés de la copie USB : ils ne sont pas utilisés à l'exécution.

## Une première carte en une minute

1. Choisissez **Fichier → Nouvelle carte**, puis renommez le sujet dans le panneau de droite.
2. Sélectionnez le sujet et appuyez sur **Tab** pour créer une idée enfant.
3. Écrivez son titre ; **Ctrl+Entrée** applique les modifications. Les notes sont aussi sauvegardées après une courte pause.
4. **Échap** remet le focus sur la carte. **Entrée** ajoute une idée au même niveau.
5. **Ctrl+S** enregistre votre carte sous le nom de votre choix. Par défaut, elle est placée dans le dossier de l'application.

La carte affichée au premier lancement est une démonstration modifiable. `Exemple - révisions.mindmap` fournit un autre exemple : ouvrez-le avec **Ctrl+O**, ou glissez le fichier sur la fenêtre.

## Navigation

| Action | Geste |
|---|---|
| Modifier un titre ou ses notes | Double-clic sur l'idée, ou F2 |
| Zoomer | Molette au-dessus de la carte |
| Déplacer la vue | Glisser le fond, ou bouton central |
| Déplacer une branche | Glisser l'idée |
| Changer son parent | Maj + glisser l'idée sur le futur parent |
| Replier une branche | Cliquer sur son bouton + / −, ou utiliser le panneau de droite |
| Tout remettre en ordre | Réorganiser, ou Ctrl+L |
| Voir toute la carte | Centrer, ou F quand la carte a le focus |
| Rechercher dans les titres et notes | Ctrl+F ; la recherche trouve aussi les branches repliées |
| Annuler / rétablir | Ctrl+Z / Ctrl+Y avec le focus sur la carte, ou boutons de la barre |
| Dupliquer une branche | Ctrl+D, ou panneau de droite |
| Supprimer une branche | Suppr, ou panneau de droite ; Ctrl+Z permet de revenir en arrière |

Le clic droit propose les actions courantes. **Aide** ouvre les raccourcis dans l'application.

## Exporter

**Fichier** permet d'exporter en **PNG**, **SVG** ou **Markdown**.

- Les images représentent les branches actuellement visibles. Ouvrez les branches repliées avant d'exporter pour tout montrer.
- Le Markdown contient tout le plan et toutes les notes, y compris celles des branches repliées.
- Les couleurs, positions, notes et plis restent conservés dans le fichier `.mindmap` original.

## Sauvegarde et clé USB

`session.mindmap` récupère les dernières modifications lors du lancement suivant. Ce fichier ne remplace pas vos cartes nommées : utilisez **Ctrl+S** pour enregistrer la carte dans son fichier.

La sauvegarde écrit un fichier temporaire à côté de la destination, puis le remplace. Un message indique une clé pleine ou un dossier non accessible. Fermez l'application et utilisez l'éjection Windows avant de retirer la clé.

Windows peut conserver ses traces habituelles de lancement ; l'application elle-même ne crée pas de dossier AppData, n'écrit pas de réglages dans le registre et ne se connecte pas à Internet. Le dossier demandé se trouve dans OneDrive : sa synchronisation dépend de OneDrive, pas de l'application.

## Compatibilité et limites

Application Windows x64, testée sur cette machine Windows 11. Elle utilise les composants graphiques fournis par Windows ; aucune DLL supplémentaire n'est livrée.

Limites : 2 000 idées, 63 niveaux sous le sujet, fichier de 8 Mio, titres de 1 024 octets UTF-8, notes de 16 000 octets UTF-8. Une seule fenêtre par dossier d'application. Pas d'import XMind/FreeMind, d'images dans les idées ou de liens transversaux : cette version représente un arbre.

L'affichage des emojis dépend des polices Windows ; ils sont conservés dans les documents même si un symbole apparaît à l'écran.

## Développement

La livraison place le projet dans `sources/`. Le modèle est écrit en Python puis généré en C++ avec py2x. L'interface Windows, les fichiers et les exports sont écrits à la main.

```powershell
cd sources
cmake -S . -B build-final -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build-final -j 2
ctest --test-dir build-final --output-on-failure
```

Le C++ généré est déjà livré : Python et le convertisseur ne sont nécessaires que pour le régénérer. Les explications détaillées, les preuves de test et les améliorations possibles de py2x sont dans **RAPPORT_COMPLET.md**.
