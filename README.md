# Pocket Tunes

Un lecteur de musique pour l'**Analogue Pocket**.

L'idée est simple : transformer la Pocket en petit iPod. On copie sa musique
sur la carte SD, on lance le core, et on retrouve une interface à l'ancienne —
listes plein écran, pochettes, égaliseur qui danse — pour écouter ses albums
ou un livre audio de 20 heures, chapitre par chapitre.

Le tout tourne entièrement dans la puce FPGA de la console. Pas de système
d'exploitation caché derrière : juste du matériel reconfiguré en lecteur de
musique.

**➡️ [Guide d'utilisation (boutons et écrans)](docs/guide-utilisation.md)**

## Ce que ça fait

- Lit les **MP3** et les **Opus**, y compris les gros fichiers (un livre audio
  de 20 h et 780 Mo, ça passe).
- **Navigation par chapitres** dans les livres audio, avec les gâchettes L et R.
- Affiche les **pochettes** de tes albums.
- **Égaliseur animé** qui suit la musique.
- **Mode veille** : on éteint, on rallume, la lecture reprend exactement où
  elle en était — à la seconde près.
- Aléatoire, répétition, avance/retour rapide, reprise de piste… tout ce qu'on
  attend d'un lecteur.
- Les captures d'écran de la Pocket fonctionnent aussi (ANALOGUE + START).

## Installation

1. **Le core** : copier le dossier `Cores/jh.Tunes` et `Platforms/pockettunes.json`
   sur la carte SD (récupérés depuis un build de ce dépôt).
2. **La musique** : la déposer dans `/Assets/pockettunes/common/Music/` sur la
   carte. Ranger par dossiers `Artiste/Album/` si on veut, mais un fichier posé
   en vrac marche aussi.
3. **L'indexeur** : la Pocket ne sait pas lister les dossiers toute seule, donc
   un petit outil prépare le catalogue et les pochettes. Le plus simple :
   copier le dossier `indexer/` sur la carte SD, puis **double-cliquer** le
   lanceur de son système — `Indexer macOS.command`, `Indexer Windows.bat`,
   ou `indexer-linux.sh`. Aucune installation : s'il manque quelque chose, le
   lanceur le télécharge tout seul dans son propre dossier, et les chemins
   sont détectés automatiquement puisque l'indexeur est sur la carte.

   À relancer chaque fois qu'on ajoute ou retire de la musique. Il s'occupe de
   tout, y compris renommer les fichiers accentués que la Pocket ne sait pas
   ouvrir (les titres affichés gardent leurs accents, rassurez-vous).

   Les habitués de la ligne de commande peuvent toujours faire
   `node indexer/src/index.js -m … -o … -r …` (voir `indexer/README.md`).

4. Éjecter la carte, la remettre dans la Pocket, lancer **Tunes** dans le menu
   openFPGA. C'est tout.

## Pour les curieux et les développeurs

Sous le capot, un petit processeur RISC-V créé dans le FPGA fait tout le
travail : l'interface, et surtout le décodage audio en temps réel — ce qui a
demandé quelques acrobaties racontées dans [`docs/architecture.md`](docs/architecture.md)
(en anglais). Les instructions de compilation (firmware et bitstream) sont
dans [`core/README.md`](core/README.md).

Au programme des prochaines versions, si l'envie et le temps s'y prêtent :
lecture WAV/FLAC, pochettes dans les listes, et un portage MiSTer.

## Crédits et licences

- Décodeur MP3 [Helix](https://en.wikipedia.org/wiki/Helix_Universal_Server) —
  licence RealNetworks RPSL/RCSL (`firmware/helix/LICENSE.txt`)
- [libopus](https://opus-codec.org) 1.5.2 — licence BSD-3
- Police [Cozette](https://github.com/the-moonwitch/Cozette) — licence MIT
- Base du core issue du template openFPGA d'Analogue
