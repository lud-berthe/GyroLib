# Audit du SDK GyroLib

[Documentation](INDEX.md) / Maintenance

La préparation de la version 1.0.0 a ensuite rejoué les suites depuis des dossiers
de compilation neufs : 135/135 exécutions réussies, dont deux consommateurs du
cœur installé en plus de la matrice ci-dessous. Voir [Validation](VALIDATION.md)
pour les contrôles du paquet final. Les revues ci-dessous conservent leurs
versions, résultats et empreintes historiques.

**Revue F et contre-revue du 4 octobre 2026 — GyroLib 0.2.0, ABI C 1, INI 0.2.0.**

Les 20 constats des revues A à F sont corrigés. Après correction de F1 et F2,
la contre-revue n'a confirmé aucun autre défaut dans le périmètre ci-dessous.
Les **133 exécutions CTest passent**. Ce résultat décrit une revue technique
interne sur Windows x64 ; il ne garantit pas l'absence de tout bug.

- [Résultat actuel](#résultat-actuel)
- [Revues A et B](#revues-a-et-b)
- [Revue C — flick et courbes héritées](#revue-c--flick-et-courbes-héritées)
- [Revue D — destination et notifications](#revue-d--destination-et-notifications)
- [Revue E — événements et persistance](#revue-e--événements-et-persistance)
- [Suivi Steam du 4 octobre 2026](#suivi-steam-du-4-octobre-2026)
- [Revue F — raccourci et ouverture du panneau](#revue-f--raccourci-et-ouverture-du-panneau)
- [Preuves de la revue F](#preuves-de-la-revue-f)
- [Matrices et preuves des revues A–E](#matrices-et-preuves-des-revues-ae)
- [Vérifications des 1er et 2 octobre 2026](#vérifications-des-1er-et-2-octobre-2026)
- [Artefacts historiques](#artefacts-historiques)
- [Périmètre restant](#périmètre-restant)

## Résultat actuel

Windows x64/MSVC Release, sources locales non commitées. Les régressions F font
partie de la suite principale et sont aussi compilées contre les SDK installés.

| Vérification | Résultat |
|---|---:|
| DLL bundled, SDL, panneaux, démo | 46/46 |
| Statique complet | 46/46 |
| Cœur statique sans SDL/UI | 20/20 |
| Consommateurs C/C++/DX12 des SDK installés | 4/4 DLL + 4/4 statique |
| Régressions E–F contre les SDK installés | 7/7 DLL + 6/6 cœur statique |
| Total | **133/133** |

La contre-revue a relu la continuité des entrées, le cycle d'ouverture et la
synchronisation des panneaux, puis les contrats d'API, de persistance,
d'héritage et de lecture Steam empruntée. Les observations A–D ont été rejouées
et leur sortie contrôlée : notifications, alias de contacts, flick reçu en
retard, courbes héritées, passage à Custom et erreurs d'allocation ciblées.
Les 600 opérations de sauvegarde/rechargement ne présentent aucun écart.
Ces probes historiques impriment des observations ; leur code de sortie seul
ne suffit pas à valider le résultat et ils ne sont pas ajoutés au total CTest.

Les huit headers installés correspondent aux sources ; les **144 exports**
publics contrôlés sont présents. Le test de distribution passe et l'INI du
joueur est inchangé. Les vérifications documentaires contrôlent les liens locaux
et les symboles d'API cités, dans les sources et le SDK installé.

## Revues A et B

### A — défauts initiaux

| ID | Défaut reproduit | Correction et couverture |
|---|---|---|
| A1 | Changer de manette transformait durablement le bouton 22 en contact, y compris dans l'INI | Résolution matérielle sans réécrire préférence/exception ; A → B → A, INI identique, parent/enfant |
| A2 | Lissage Off laissait la stabilisation cachée agir : −0,025° au lieu de −0,05° à 5 deg/s, gain 1, 10 ms | Bypass de la stabilisation avec lissage Off ; tests `advanced_motion` |
| A3 | Troncature de `context.4294967295.flick.touchpad_release_threshold` dans l'événement ABI 1 | `gl_event_ex`, ID 128 octets ; ancien ABI conservé avec invalidation globale si trop long ; file, poll partagé, consommateurs C |
| A4 | `std::bad_alloc` pouvait sortir du parsing C | Parsing sans allocation, file fixe, interception aux points corrigés ; injection statique avec `/EHa` |
| A5 | Un endpoint SDL périmé masquait un flick Steam frais associé | Fournisseurs frais/autorisés uniquement, autorité physique maintenue ; repli, retour SDL et exclusion d'une autre manette |

Les contrats d'horloge/traduction ont été harmonisés et la CI étendue aux trois
variantes et aux consommateurs Overlay. Cette définition n'a pas été exécutée
sur GitHub pendant les interventions.

### B — réglages, sources complémentaires et événements

| ID | Défaut reproduit | Correction et suivi |
|---|---|---|
| B1 | Après résolution d'un alias, choisir Off laissait le contact actif ; choisir Right donnait Left or Right | Une édition explicite retire l'alias masqué dans la vue éditée ; le changement de matériel seul garde la préférence |
| B2 | Restaurer X hérité laissait une exception Fast X cachée pouvant provoquer une décélération | Dérivation sans exception automatique ; cas locaux/Custom complétés par C2 et C3 |
| B3 | INI partiel X=6 avec Off/Low/High chargé comme Custom, Fast X=2,5 | Composants absents dérivés du préréglage, composants explicitement édités conservés ; restauration partielle complétée par E3 |
| B4 | Un fournisseur unique masquait le stick quand stick et pad venaient de deux endpoints associés | Sélection par famille, historiques/horloges séparés et dédoublonnage ; arrivée tardive complétée par C1 |
| B5 | Certains changements de sélection, capacités ou vue active n'émettaient rien | DEVICE/CONTEXT sur changement, expiration et reprise ; retrait direct complété par C4 |

`sdk_audit_regressions` couvre ces scénarios, dont les trois familles tactiles,
la persistance, les quatre préréglages, le maintien du parent et les rotations des
deux familles. Les anciennes exceptions sauvegardées sont conservées : leur
origine ne peut être distinguée d'une édition volontaire.

### Lecteur auxiliaire : échec intermittent puis correction

`sensor_process_fragment` a échoué une fois sans endpoint/échantillon, puis réussi
cinq fois. Ces reruns seuls n'identifiaient pas la cause. Le suivi a mis en évidence
un watchdog de deux secondes dès le démarrage, avant le premier message, et des
fixtures dépendant des pilotes physiques.

Le lecteur attend désormais jusqu'à dix secondes avant le premier message, puis
tolère deux secondes de silence en fonctionnement. Les diagnostics distinguent
démarrage, flux et arrêt ; qualification et âge des échantillons restent inchangés.
Les fixtures de protocole n'ouvrent plus le matériel. Un cas ajoute 2,3 secondes
avant des messages fragmentés. Les deux cas fragmentés ont passé cinq reruns
après correction, puis les suites C et E. Le test de distribution emploie toujours
le vrai lecteur de production.

## Revue C — flick et courbes héritées

### C1 — Rapport de flick frais reçu après sa frame

Le lecteur comparait l'horodatage à l'horloge d'animation, déjà avancée par le
rendu. Une position droite reçue avec 15 ms de retard, mais encore fraîche,
produisait 0° au lieu des 90° obtenus à l'heure.

La correction sépare le dernier rapport consommé du temps d'animation. Les rapports
frais retardés sont traités une fois avec leurs propres intervalles, sans rejouer
l'animation écoulée. Le probe donne 90° dans les deux cas, puis 0° sans nouveau
rapport. Les tests couvrent stick/pad, lots, doublons, ordre, expiration/reprise,
durée de pivot et vitesse du lissage.

### C2 — Préréglage local devenu Custom après une sensibilité

Sélectionner Low puis X=6 gardait Fast X=9 dans une vue indépendante, mais laissait
Fast X=3,75 et affichait Custom dans une vue héritée. La correction B2 excluait à
tort les composantes déjà locales d'un préréglage explicite.

Les valeurs rapides locales suivent maintenant l'édition de sensibilité ; les
valeurs héritées restent dérivées sans exception cachée. Les quatre préréglages,
X/Y, trois niveaux de parents, persistance, recommandations et une courbe Custom
volontaire sont testés. Le probe conserve Low et Fast X=9 avec ou sans parent.

### C3 — Valeur non éditée modifiée au passage en Custom

Avec un parent X=6/Low et un enfant X=2, Fast X vaut 3. Modifier seulement le seuil
lent de 5 à 10 faisait passer Fast X à 9 lors de l'entrée en Custom.

La transition conserve désormais la courbe effective. Les valeurs dérivées qui
reviendraient à des valeurs parent différentes deviennent des exceptions ; celles
déjà égales au parent restent héritées. Restaurer l'accélération retire les
exceptions de toute la courbe. Les quatre composants sont testés avec parents,
valeur inchangée, sauvegarde, recommandations et restauration. Le probe garde 3.

### C4 — Retrait direct sans événement

`gl_forget_endpoint` retirait une manette non sélectionnée sans événement. Comme
la sélection et les capacités actives ne changeaient pas, un menu natif pouvait
conserver cette manette dans sa liste.

Chaque retrait réussi émet maintenant DEVICE, détail 0 et ID retiré, sans exiger
un appel préalable à `gl_disconnect_endpoint`. Le probe passe de 0 à 1 événement.
Les tests couvrent endpoint connecté/déconnecté et répétition d'un retrait inconnu
(erreur sans événement).

## Revue D — destination et notifications

### D1 — Modèle de menu non invalidé

Une vue sans destination explicite suit `gl_set_output_target`. Cet appel changeait
la visibilité du flick sans notifier un menu natif qui conserve ses widgets.
Le panneau relisant le modèle à chaque frame masquait ce défaut.

Avant correction, le probe constatait `flick_visible_before=1`, `after=0`,
`events=0`. Après correction, ses trois vues suivant le défaut reçoivent trois
événements CONTEXT, détail 3, valeur égale à l'ID de la vue. Les vues explicites,
valeurs répétées et valeurs invalides n'en reçoivent pas.

Les tests permanents couvrent les deux destinations, vues actives/inactives,
priorité des destinations explicites et appels sans changement. Le probe donne
les mêmes résultats sur DLL et core statique installés :

```text
global_output: flick_visible_before=1 after=0 events=3
declared_output: flick_visible=1 events=1
roundtrip: 600 operations, mismatches=0
```

Les 600 opérations du probe D comparaient neuf valeurs par vue après chaque
rechargement. Elles n'examinaient pas toutes les exceptions d'héritage, ce qui
explique que le cas E3 ait nécessité une couverture supplémentaire.

## Revue E — événements et persistance

Les trois tests de `tests/sdk_audit_loop_tests.cpp` échouaient d'abord sur le code
précédent (`loop-e-red-tests.log`). Ils passent après correction, dans les trois
builds et contre les deux SDK installés.

### E1 — Notification des noms de gâchettes

`gl_set_trigger_label` changeait le nom sans notifier les menus natifs pilotés par
événements. La correction émet `GL_EVENT_BUTTON_LABELS`, détail −1 et identifiant
d'endpoint. Répéter le nom ou effacer un nom déjà vide reste silencieux.

La régression vérifie les deux côtés, les effacements, les identifiants invalides,
la longueur maximale et l'absence de mutation/événement sur erreur.

### E2 — Interruption d'un flick par la destination globale

Changer le défaut global en curseur réinitialisait le flick d'une vue pourtant
déclarée explicitement caméra. Un pivot de 90° s'arrêtait à 24,390001°, contre
90,000003° sans cet appel.

`gl_set_output_target` ne réinitialise désormais les états temporels que si la
destination effective active change. Les notifications D1 restent émises pour
les vues suivant le défaut. Le test confirme à la fois la continuité de la vue
explicite et la suspension effective d'une vue qui passe réellement en curseur.

### E3 — Héritage partiel perdu au rechargement

Après un préréglage local, restaurer un seul seuil à sa valeur héritée retirait
son exception. Le chargeur interprétait ensuite son absence dans l'INI comme une
composante de préréglage à compléter, recréant l'exception.

La séquence déterministe de graine 42, opération 158, faisait passer
`context.3.gyro.slow_threshold_dps` de `overridden=0, source=1` à
`overridden=1, source=3`. Des valeurs égales masquaient le défaut jusqu'au prochain
changement du parent.

La sauvegarde écrit `=inherit` pour distinguer ce cas d'un INI partiel à compléter.
Ce marqueur exige un parent non nul dans le même fichier. Les anciens INI
numériques restent lisibles ; la baseline prépublication reste 0.2.0.
[Le format](INHERITANCE.md#persistence) documente cette distinction.

Les tests couvrent quatre préréglages, quatre composants avancés, trois niveaux de
vues, des changements ultérieurs du parent et des fichiers invalides sans mutation.
Une séquence de **800 opérations sur 46 réglages de vue** compare après chaque
sauvegarde/rechargement les valeurs, parents, exceptions et origines. Elle inclut
édition, changement de parent, restauration et recommandations.

Le build bundled produit deux avertissements MSVC C4996 préexistants : copie
bornée d'un nom SDL dans un endpoint initialisé à zéro et copie d'un nom Steam
littéral dans un tampon de 128 octets. Les sites ont été relus sans dépassement
confirmé ; aucun masquage global des avertissements n'a été ajouté.

## Suivi Steam du 4 octobre 2026

Ce suivi est postérieur à la matrice 112/112 et ne la remplace pas. Le raccord
Windows à l'API publique chargée a été ajouté, puis testé dans un jeu hôte avec un Steam Controller 2026/puck.
Le service Steam Input était initialisé par l'hôte ; GyroLib continue de
l'emprunter. Aucun SDK Steam ou binaire Valve n'a été téléchargé ou redistribué.

Une interruption ciblée du lecteur SDL a produit 350 observations sur 30 secondes :
42 avec SDL, puis 308 avec Steam. Les 308 observations Steam contiennent une
rotation non nulle en visée d'arme. L'utilisateur n'a constaté aucune anomalie.

**P2 — retour SDL bloqué après redémarrage du lecteur.** Le capteur SDL recevait
de nouveau des données, mais `native_motion` considérait le flux Steam sain comme
une raison de ne pas réassocier ce capteur. Le nouveau test `steam_loaded_runtime`
reproduit l'échec avec un pad virtuel sans capteur, un endpoint Steam et un compagnon
SDL recréé. La garde porte maintenant sur un flux SDL natif ; la qualification,
l'unicité des candidats et les associations manuelles restent respectées. Le test
passe après correction, y compris après deux associations successives.

Le test a aussi vérifié que les valeurs Steam non finies sont rejetées avec
`GL_INVALID`, conformément au header, au lieu d'être ignorées avec `GL_OK`.
Les fixtures ne chargent aucun vrai service Steam et vérifient l'absence d'appels
à Init, RunFrame, Shutdown et ActivateActionSet dans le lecteur emprunté.

Les contrôles ciblés passent : 7/7 en DLL et 2/2 en statique. Les vérifications propres au
consommateur sont conservées dans son projet. La suite complète de la DLL passe ensuite 42/42. Une seconde
capture matérielle confirme le parcours SDL → Steam (seconde 4) → SDL (seconde 11),
avec 348 observations en 30 secondes. Le retour SDL est automatique et reste
stable jusqu'à la fin. L'utilisateur confirme n'avoir ressenti ni coupure, ni saut,
ni changement de sens ou de vitesse. La DLL de cette correction a pour SHA-256
`FB8CD17B004A8074276A1E2E93D4CEB2597CE6412B27B40FEFA55269228F5878`.
Voir [VALIDATION.md](VALIDATION.md) pour les résultats matériels actualisés.

## Revue F — raccourci et ouverture du panneau

### F1 — Raccourci après une interruption des entrées

**P2, corrigé.** Une combinaison déjà maintenue pouvait ouvrir ou fermer le
panneau après une reconnexion rapide gardant la même identité, ou après une
longue interruption entre deux appels à `gl_update`.

L'armement suit désormais le périphérique, le fournisseur des boutons et la
continuité des horodatages. Une déconnexion, une suppression d'endpoint, un
changement de boutons disponibles ou de sélection le désarme immédiatement.
Après une rupture, le raccourci attend de nouveau le relâchement des deux boutons.
Les bascules de source gyro ne changent pas à elles seules ce suivi des boutons.

Les tests couvrent la reconnexion entre deux mises à jour, la recréation avec le
même identifiant, le changement d'autorité des boutons, le retour à un
périphérique précédent, la perte de capacités et les interruptions des paquets
ou des mises à jour. Ils vérifient aussi qu'un relâchement rétablit le raccourci
et qu'un flux lent mais continu reste utilisable.

### F2 — Onglet différent selon le moyen d'ouverture

**P2, corrigé.** F10 sélectionnait la vue active, tandis que l'ouverture à la
manette conservait l'onglet initial ou celui précédemment édité.

Le cœur mémorise maintenant la vue à chaque ouverture. Le panneau utilise cet
état commun pour le clavier, la manette et l'API. Le rendu DX12 le transmet dans
ses instantanés. La sélection ne change pas pendant l'édition, même si le jeu
change de vue ; une fermeture/réouverture sélectionne de nouveau la vue active.
La requête C `gl_get_panel_opening` permet aux autres interfaces d'utiliser le
même comportement, sans dépendance à ImGui.

Les tests du vrai panneau vérifient la vue attendue, la conservation d'un onglet
choisi par l'utilisateur, la réouverture sans image intermédiaire, la création
tardive du frontend et l'absence de vue active. Les tests DX12 contrôlent le
résultat d'une modification de sensibilité, depuis un thread de rendu distinct,
après ouverture à la manette, au clavier et par l'API. Ils passent aussi avec
une autre instance d'ImGui dans le programme hôte.

## Preuves de la revue F

Avant correction, les suites existantes passaient 115 exécutions et les nouvelles
reproductions échouaient cinq fois : F1 contre DLL et cœur statique, F2 sur le
panneau statique. Ces échecs couvrent deux défauts, pas cinq. Les journaux
`build-sdk-audit/audit-f-*.log` conservent ce résultat initial.

Les résultats corrigés sont dans `build-sdk-audit/fix-f-*.log` et
`fix-f-package.json`. Les reproductions sont dans `tests/sdk_audit/audit_f.cpp`
et `audit_f_panel.cpp` ; les interactions DX12 sont dans `tests/overlay_tests.cpp`.
Un clic initial du nouveau banc DX12 était placé au-dessus du curseur : sa
capture a permis de corriger le test, sans modifier le comportement du SDK.

| Artefact après correction | SHA-256 |
|---|---|
| `dist/sdk/bin/gyrolib.dll` | `B8154DC76671F9B74C778F51432A1115562D9DC89839330C4F9C245AB86ADDF4` |
| INI du joueur, conservé | `6CCBB46B61D222584F51C81C16025B7E41C26B82BD93D393B917EC832B77C6CD` |

Aucun défaut confirmé ne reste ouvert après cette contre-revue. Elle ne couvre
pas tous les entrelacements de threads ni les erreurs d'allocation du rendu.
Elle n'ajoute aucune validation sur manette réelle ou sur Linux/Proton. Les
validations matérielles antérieures restent décrites dans [VALIDATION.md](VALIDATION.md).
Aucun jeu n'a été lancé et aucune publication n'a été faite durant cette passe.

## Matrices et preuves des revues A–E

### Version et preuves après E

Le SDK contrôlé est `dist/sdk`, construit à partir des sources locales modifiées.
HEAD était `06f1d1731088b14e7b800132fa9eab5826e0ab26` ; ce commit seul ne représente
pas le contenu audité, qui comprend des modifications non commitées.

| Élément | Identité après E |
|---|---|
| DLL | `dist/sdk/bin/gyrolib.dll`, 3 742 720 octets |
| SHA-256 DLL | `C68E75705D8765EECF485F05FC8042302BB46A7B933F4473CD8C03A5D7643866` |
| Headers | Les huit headers installés correspondent aux sources |
| INI du joueur | Inchangé ; SHA-256 `339ADCC69E224FADF763758AA150A01BDAD62AFE5B93E56EF24D0DB1255BD45D` |

Les reproductions publiques sont dans `tests/sdk_audit/` ; son `README.md`
explique leur compilation contre les SDK installés. Les probes A–D impriment des
observations : leur sortie 0 confirme l'exécution, pas la correction du résultat.
Les tests permanents vérifient le comportement attendu. Les régressions E sont
aussi compilées séparément contre les SDK DLL et core statique installés.

### Résultats de validation A–E

Toutes les lignes ci-dessous sont des vérifications locales Windows x64 Release
du 3 octobre 2026. Une ligne « avant correction » ne couvre pas encore les défauts
que ses nouveaux probes ont révélés.

| Étape | DLL bundled | Statique complet | Core statique | Consommateurs installés |
|---|---:|---:|---:|---|
| Audit A avant correction | 36/36 | 35/35 | 11/11 | DLL 4/4 |
| Après A | 37/37 | 37/37 | 13/13 | DLL 4/4, statique 4/4 |
| Audit B avant correction | 37/37 | 36/37 au premier passage | 13/13 | DLL 4/4, statique 4/4 |
| Après B | 38/38 | 38/38 | 13/13 | DLL 4/4, statique 4/4 |
| Audit C avant correction | 38/38 | 38/38 | 13/13 | DLL 4/4, statique 4/4 |
| Après C | 38/38 | 38/38 | 13/13 | DLL 4/4, statique 4/4 |
| Audit D avant correction | 38/38 | 38/38 | 13/13 | DLL 4/4, statique 4/4 |
| Après D1, ciblé | 9/9 | 9/9 | 13/13 | DLL 4/4, statique 4/4 |
| Après E, complet | 41/41 | 41/41 | 16/16 | DLL 4/4, statique 4/4 ; régressions E 3/3 + 3/3 |

Le total après E était de 112 exécutions CTest. Les centaines d'opérations internes aux
tests ne sont pas comptées comme autant de tests supplémentaires. D1 avait été
vérifié par 39 exécutions ciblées ; E rejoue l'ensemble des suites SDL/lecteur,
menus, overlay, cœur et distribution.

Les journaux locaux sont conservés dans `build-sdk-audit` : `corrections-*`,
`reaudit-*`, `fix-*`, `audit3-*`, `fix-c-*`, `audit4-*`, `fix-d-*` et `loop-e-*`.
Les échecs initiaux ne sont pas effacés du bilan par les passages suivants.

## Vérifications des 1er et 2 octobre 2026

Ces résultats antérieurs ne s’ajoutent pas au total actuel. Builds MSVC 19.51, x64 Release.

| Date | Scope | Recorded result |
|---|---|---|
| 2026-10-02 | Earlier full matrix | Bundled 36/36; static 35/35 covered, including an isolated startup rerun; core 11/11 |
| 2026-10-02 | Relocated SDK, C/C++ with SDL/Panel | 2/2; SDL resolved inside the SDK from `CMAKE_PREFIX_PATH` |
| 2026-10-02 | Installed bundled Core/Steam/Panel with SDL package discovery disabled | 2/2 |
| 2026-10-02 | Installed core-only SDK | 2/2 |
| 2026-10-02 | Installed bundled Overlay with SDL package discovery disabled | 4/4 |
| 2026-10-02 | Original README example, now in QUICKSTART.md | Compiled unchanged against a relocated SDK; unavailable cache returned exit 1 |
| 2026-10-02 | Installed demo smoke; prerequisite/configuration checks | Smoke passed; missing pwsh reported; external SDL configuration with tests disabled passed |
| 2026-10-02 | Menu-camera opt-in | Bundled 33/33 and core 9/9 at that revision |
| 2026-10-02 | Registered-view editability fix | Bundled 33/33, core 9/9, seven affected static tests 7/7 |
| 2026-10-01 | Earlier static SDL/panel/demo | 30/30 |
| 2026-10-01 | Installed static Core/Steam with SDL discovery disabled | 2/2; requesting absent Panel correctly rejected |

Le 2 octobre, `sensor_process_auto` a manqué une fois sa fenêtre d'acquisition et
son délai d'arrêt, puis passé trois relances isolées sans changement de code ni de
délai. La revue B a ensuite reproduit un autre échec de démarrage et corrigé le
watchdog ainsi que la dépendance des fixtures aux pilotes physiques. Le suivi du
lecteur dans ce rapport conserve cette séquence ; une relance réussie ne suffisait
pas à en établir la cause.

La définition GitHub Actions n'a pas été exécutée sur GitHub pendant ces revues.
Son résultat hébergé reste non vérifié.

## Artefacts historiques

| Étape | Taille DLL | SHA-256 |
|---|---:|---|
| Avant A | 3 736 576 | `E233038E7CFE0FB96DB37FFD8C43C096AF25365C4845F84FEC001E758F5A7720` |
| Après A / audit B | 3 738 112 | `B2DE2646AF7F97B5FC50F6CA82BBE6EA854E5897D1E86A11354F460746ED49A8` |
| Après B / audit C | 3 739 136 | `B84B90E86412365FD7D7CBA19F0DEED7630F9A2CF66C982F2710BD5843BEA00C` |
| Après C | 3 740 160 | `F6DCA3D5D3AA28732B44F0ED7F30F46D2A78EB2CDA6124D368BF63C1E244C983` |
| Après D1 | 3 740 672 | `103F80A68DD08E855AA203745ADCCC251C62C86257541AA0CBDA4B08C6FDE215` |
| Après E | 3 742 720 | `C68E75705D8765EECF485F05FC8042302BB46A7B933F4473CD8C03A5D7643866` |

Le probe core seul mesurait environ 1,23 µs/frame et 11 allocations avant A, puis
0,61–0,67 µs/frame et 4 allocations après correction, sur six vues et 10 000 frames
synthétiques. Cela exclut SDL, UI, disque et latence matérielle ; ce n'est pas une
promesse de performance en jeu.

## Périmètre restant

La matrice contrôle API/lifetime, gyro/calibration, flick/acquisition, réglages,
menus, overlay et distribution. Elle n'est ni un fuzzing exhaustif, ni une preuve
formelle de sûreté des threads, ni une recherche complète de vulnérabilités.
L'injection d'erreurs mémoire ne couvre pas tous les backends.

Restent à valider : matériel réel dans le jeu hôte, entrées/vibrations natives,
plusieurs manettes et transports, ressenti du flick/feedback, autres combinaisons
matérielles pour Steam Input public, Linux/Proton, Debug, shared modulaire, HDR et autres
renderers. Les tests DX12 utilisent WARP ; les catalogues n'ont pas reçu une
relecture humaine complète dans les six langues. Antivirus/environnements durcis
et sauvegarde concurrente entre processus ne sont pas certifiés.

[VALIDATION.md](VALIDATION.md) rassemble les commandes et observations matérielles.
[LIMITS.md](LIMITS.md) décrit les limites du produit. Les licences restent dans le
SDK et doivent accompagner les mods ; ce rapport ne remplace pas leurs notices.
