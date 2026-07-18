# Pocket Tunes — Guide d'utilisation

Lecteur MP3/Opus pour Analogue Pocket. Interface façon iPod Classic en ambre :
une pile de listes plein écran (Bibliothèque → Albums → Titres → Lecture),
pochette 96×96, égaliseur 18 bandes, chapitres de livres audio au L/R.

## La console

```
             ┌─────────────────────────────┐
        L ═══╡                             ╞═══ R     chapitre − / chapitre +
             │   ┌─────────────────────┐   │            (livres audio)
             │   │ ‹ Menu   Titre  12:34│  │
             │   │  ─────────────────── │  │
             │   │  Artiste           › │  │
             │   │  Artiste           › │  │
             │   │  ♪ Piste libre  3:12 │  │
             │   │       ÉCRAN          │  │
             │   └─────────────────────┘   │
             │                             │
             │       ▲                (X)  │
             │    ◄──┼──►         (Y)      │
             │       ▼                (A)  │
             │     D-PAD             (B)   │
             │                             │
             │      (SELECT)  (START)      │
             │       répéter  aléatoire    │
             └─────────────────────────────┘
```

## Les écrans

```
  BIBLIOTHÈQUE ──A──► ALBUMS ──A──► TITRES ──A──► LECTURE
       ▲                │             │              │
       └───────B────────┴──────B──────┘              B (la musique continue,
                                                        ♪ reste dans la barre d'état)
```

- **Bibliothèque** : les dossiers d'artistes (`›`), plus les fichiers audio
  posés directement à la racine de `Music/` (lancés d'un `A`).
- **Albums** : les albums de l'artiste, plus ses fichiers « en vrac » (posés
  dans son dossier hors de tout album), listés après les albums.
- Un artiste **sans dossier d'album** s'ouvre directement sur ses Titres —
  l'écran Albums est sauté à l'aller comme au retour (`B`).
- Ouvrir un dossier (`A`) ne lance jamais la lecture — seul le choix d'une
  **piste** la lance. Quitter Lecture (`B`) ne l'arrête jamais : le glyphe
  `►`/`❚❚` reste dans la barre d'état et `X` y retourne d'une pression.

## Commandes

### Dans les listes (Bibliothèque / Albums / Titres)

| Bouton | Action |
|---|---|
| `▲` `▼` | Déplacer le curseur |
| `A` | Ouvrir le dossier / lire la piste |
| `B` | Revenir à l'écran précédent |
| `X` | Retourner à l'écran Lecture (si un morceau est chargé) |
| `Y` | Lecture / pause (marche partout) |

### Sur l'écran Lecture

| Bouton | Action |
|---|---|
| `▲` `▼` | Piste précédente / suivante (suit le mode aléatoire) |
| `◄` `►` | Reculer / avancer de 10 secondes |
| `L` `R` | Chapitre précédent / suivant (livres audio) |
| `A` ou `Y` | Lecture / pause |
| `B` | Revenir à la liste d'où l'on vient — **la musique continue** |

`L` relance d'abord le chapitre en cours (comme un lecteur CD) ; un second
appui dans les 3 premières secondes saute au chapitre précédent.

### Partout

| Bouton | Action |
|---|---|
| `START` | Mode aléatoire on/off (badge `ALEA`) |
| `SELECT` | Mode répétition : `RPT` → `RPT1` → `RPT0` |

## Modes de répétition

| Badge | Comportement en fin de piste |
|---|---|
| `RPT` | Piste suivante ; à la fin de la liste, reboucle sur la première *(défaut)* |
| `RPT1` | Rejoue la même piste en boucle |
| `RPT0` | Piste suivante ; s'arrête à la fin de la liste |

La « liste » est le contexte de la piste lancée : son album, les pistes en
vrac de l'artiste, ou les pistes libres de la racine. Le mode aléatoire
(`ALEA`) se combine avec la répétition (jamais deux fois de suite la même).

## Ce qu'affiche l'écran

- **Barre d'état** (partout) : `‹ Menu` à gauche (= bouton `B`), le titre de
  l'écran au centre, l'état de lecture (`►`/`❚❚`) et **l'heure** de la Pocket
  à droite.
- **Listes** : `›` marque un dossier, `►` orange la piste en cours de
  lecture, la durée à droite des pistes.
- **Lecture** : pochette 96×96 (vraie pochette si l'indexeur l'a extraite,
  dégradé sinon), titre, `Artiste · Album`, l'**égaliseur** 18 bandes (il se
  fige en pause), la barre de progression avec temps écoulé / restant et
  `Piste n/total`, puis les badges `ALEA` / format · débit / `RPT`.
- **Livres audio** : la ligne `Chap. n/total · titre du chapitre` suit la
  lecture ; `L`/`R` naviguent (les chapitres viennent des métadonnées ID3 —
  l'indexeur les lit tout seul).
- **Titres trop longs** : l'élément sélectionné défile automatiquement
  (aller-retour) pour être lisible en entier.
- **`ERR n`** sur l'écran Lecture : la piste n'a pas pu être ouverte — voir
  le [README de l'indexeur](../indexer/README.md) (le cas classique :
  fichiers ajoutés sans avoir relancé l'indexeur).

## Mode veille

Appuie sur le **bouton power** : la Pocket sauvegarde l'état du lecteur
(piste, position, modes, écran) et s'éteint. Au réveil, tout reprend
exactement où c'était — si la musique jouait, elle reprend à la seconde
près. Si la bibliothèque a changé entre-temps (ré-indexation), le lecteur
revient à l'accueil sans relancer la lecture.

## Raccourcis Analogue OS (Memories)

Fonctions de l'OS de la Pocket, disponibles pendant que le core tourne :

| Combo | Action |
|---|---|
| `ANALOGUE` + `START` | **Capture d'écran** → PNG dans `/Memories/Screenshots` |
| `ANALOGUE` + `▲` | **Save state** (même mécanique que la veille) |
| `ANALOGUE` | Menu de l'OS (Memories, réglages d'affichage…) |

Quand le bouton Analogue est tenu, l'OS intercepte les touches : le `START`
du combo capture ne déclenche pas le mode aléatoire du lecteur.

## Préparer la carte SD

1. Copier la musique dans `/Assets/pockettunes/common/Music/` sur la carte :
   `Artiste/Album/…` pour les albums, mais un fichier peut aussi être posé
   directement dans le dossier de l'artiste, ou à la racine de `Music/` —
   il apparaîtra au bon endroit.
2. Lancer l'indexeur (il renomme au besoin les fichiers accentués — l'écran
   garde les accents, seul le nom sur disque devient ASCII) :

   ```
   node indexer/src/index.js \
     -m "/Volumes/CARTE/Assets/pockettunes/common/Music" \
     -o "/Volumes/CARTE/Assets/pockettunes/common" \
     -r /Assets/pockettunes/common/Music
   ```

3. Éjecter, insérer dans la Pocket, lancer le core **Tunes**.
