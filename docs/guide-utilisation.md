# Pocket Tunes — Guide d'utilisation

Lecteur MP3 pour Analogue Pocket. Interface « terminal ambre » : navigation
Artiste → Album → Piste, lecteur plein écran avec égaliseur, et mini-barre de
lecture qui te suit pendant que tu navigues.

## La console

```
             ┌─────────────────────────────┐
        L ═══╡                             ╞═══ R        (non utilisés)
             │   ┌─────────────────────┐   │
             │   │  Artistes │ Albums  │   │
             │   │  ─────────┼──────── │   │
             │   │           │ Pistes  │   │
             │   │       ÉCRAN         │   │
             │   │  ~~~~~~~~~~~~~~~~~  │   │
             │   │  ♪ mini-barre   ►   │   │
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

## Les trois écrans

```
  ARTISTES  ──A──►  ALBUMS  ──A──►  PISTES  ──A──►  LECTEUR (plein écran)
     ▲                 │               │                │
     └───────B─────────┴───────B───────┘                B (la musique continue,
                                                           la mini-barre apparaît)
```

Ouvrir un dossier (`A`) ne lance jamais la lecture — seul le choix d'une
**piste** la lance. Fermer le lecteur (`B`) ne l'arrête jamais : la musique
continue en fond et la **mini-barre** en bas de l'écran garde le titre, la
progression et l'état de lecture sous les yeux à tous les niveaux.

## Commandes

### Pendant la navigation (artistes / albums / pistes)

| Bouton | Action |
|---|---|
| `▲` `▼` | Déplacer le curseur |
| `A` | Ouvrir (artiste → albums → pistes → lecture) |
| `B` | Revenir au niveau précédent |
| `X` | Rouvrir le lecteur (si un morceau est chargé) |
| `Y` | Lecture / pause (marche partout, lecteur ouvert ou non) |

### Dans le lecteur (drawer plein écran)

| Bouton | Action |
|---|---|
| `▲` `▼` | Piste précédente / suivante (suit le mode aléatoire) |
| `◄` `►` | Reculer / avancer de 10 secondes |
| `A` ou `Y` | Lecture / pause |
| `B` | Fermer le lecteur — **la musique continue** |

### Partout

| Bouton | Action |
|---|---|
| `START` | Mode aléatoire on/off (badge `ALEA`) |
| `SELECT` | Mode répétition : `RPT` → `RPT1` → `RPT0` |

## Modes de répétition

| Badge | Comportement en fin de piste |
|---|---|
| `RPT` | Piste suivante ; à la fin de l'album, reboucle sur la première *(défaut)* |
| `RPT1` | Rejoue la même piste en boucle |
| `RPT0` | Piste suivante ; s'arrête à la fin de l'album |

Le mode aléatoire (`ALEA`) se combine avec la répétition : la « piste
suivante » est alors tirée au hasard dans l'album (jamais deux fois de suite
la même).

## Ce qu'affiche l'écran

- **En-tête** : le chemin (`Artiste/Album/`) et **l'heure** (horloge de la
  Pocket) en haut à droite.
- **Pied de page** : `NIVEAU : n/total` (position dans la liste), les badges
  `RPT`/`ALEA` actifs, et le rappel `X lecteur` quand un morceau est chargé.
- **Liste des pistes** : `►` à droite marque la piste chargée dans le lecteur.
- **Lecteur** : état `► LECTURE` / `❚❚ PAUSE`, temps écoulé à gauche et
  temps restant (`-m:ss`) à droite de la barre de progression, et
  l'**égaliseur** 18 bandes qui suit le spectre du morceau (graves à gauche,
  aigus à droite — il se fige en pause).
- **Titres trop longs** : l'élément sélectionné défile automatiquement
  (aller-retour) pour être lisible en entier.
- **`ERR n`** dans le lecteur : la piste n'a pas pu être ouverte — voir le
  [README de l'indexeur](../indexer/README.md) (le cas classique : fichiers
  ajoutés sans avoir relancé l'indexeur).

## Mode veille

Appuie sur le **bouton power** : la Pocket sauvegarde l'état du lecteur
(piste, position, modes, écran) et s'éteint. Au réveil, tout reprend
exactement où c'était — si la musique jouait, elle reprend à la seconde
près. Si la bibliothèque a changé entre-temps (ré-indexation), le lecteur
revient à l'accueil sans relancer la lecture.

## Préparer la carte SD

1. Copier la musique dans `/Assets/pockettunes/common/Music/Artiste/Album/…`
   sur la carte.
2. Lancer l'indexeur (il renomme au besoin les fichiers accentués — l'écran
   garde les accents, seul le nom sur disque devient ASCII) :

   ```
   node indexer/src/index.js \
     -m "/Volumes/CARTE/Assets/pockettunes/common/Music" \
     -o "/Volumes/CARTE/Assets/pockettunes/common" \
     -r /Assets/pockettunes/common/Music
   ```

3. Éjecter, insérer dans la Pocket, lancer le core **Tunes**.
