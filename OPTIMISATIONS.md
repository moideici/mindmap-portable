# Optimisations du 7 octobre 2026

Le dépôt `moidefr/mindmap-portable` a été récupéré par le connecteur GitHub,
puis reconstitué comme un checkout Git superficiel du commit
`98b56d2732d25a2def9cfb2df65ef1ac3f735ea8`. Les empreintes de tous les fichiers,
de l'arbre Git et du commit ont été vérifiées. Branche locale :
`optimize/performance`.

## Modifications

- Le découpage des titres et notes lit seulement les préfixes nécessaires
  aux lignes affichées, et utilise `std::string_view` pour éviter les copies
  et déplacements du texte restant.
- Les dimensions sont calculées une fois par idée et par rendu, puis
  réutilisées pour le texte et les boutons de repli. Le cache reste local au
  rendu : pas de risque de dimensions périmées après une modification.
- Demander zéro ligne renvoie une liste vide. Auparavant, un texte multiligne
  pouvait faire accéder à une liste vide et planter.
- La suite de tests indépendante de Windows peut se compiler sous Linux ;
  les tests de compression Windows restent actifs sur Windows.

Le cœur C++ généré par py2x reste identique au dépôt d'origine.

## Résultats

Comparaison avec l'ancien découpage : **30 656 cas réussis**, comprenant des
textes aléatoires, accents, emojis, retours à la ligne, espaces et limites.
Les cas zéro ligne vérifient le résultat vide sans appeler l'ancien code qui
plante.

Mesure avec GCC 14.2, `-O2`, sur 10 000 appels affichant au maximum quatre
lignes de 27 caractères :

| Note | Avant | Après | Gain |
|---|---:|---:|---:|
| 29 octets | 1,92 ms | 1,46 ms | 1,32× |
| 15 930 octets | 524,42 ms | 5,54 ms | 94,73× |

Ce gain concerne le découpage des notes, pas la vitesse globale de
l'application. Le dessin GDI+ et le texte Windows n'ont pas été mesurés.

La suite existante du modèle, documents, liens, dispositions, plans, exports,
entrées invalides et 100 cycles de mémoire passe sous Linux. L'historique
compressé est explicitement ignoré sur cet hôte.

La même suite passe avec AddressSanitizer et UndefinedBehaviorSanitizer.
La détection automatique des fuites est désactivée pour cette exécution :
LeakSanitizer ne fonctionne pas sous le mécanisme de traçage de cet
environnement. Les 100 cycles de libération vérifiés par la suite restent
exécutés.

## Limite de l'installation

L'environnement est Linux, sans CMake, MinGW ni Wine. Les tests natifs sont
compilés dans `sources/build/`, mais `MindMap.exe` n'a pas été compilé ni lancé.
La validation finale de l'interface demande Windows et les commandes de
compilation et de smoke-test décrites dans le README. Aucun changement
n'a été envoyé sur GitHub.
