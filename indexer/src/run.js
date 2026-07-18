#!/usr/bin/env node
// Lanceur convivial de l'indexeur : trouve les chemins tout seul.
//
// Deux modes :
//  1. Le dossier `indexer/` est posé SUR la carte SD → tout est détecté
//     automatiquement (le script remonte l'arborescence jusqu'à trouver
//     `Assets/pockettunes/common`).
//  2. Sinon, un fichier `indexer.config.json` est créé à côté du dossier
//     `src/` au premier lancement : ouvrez-le et renseignez vos chemins.
//
// Les habitués peuvent toujours appeler `node src/index.js -m … -o … -r …`
// directement — ce lanceur ne fait que préparer ces trois arguments.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";

const HERE = path.dirname(fileURLToPath(import.meta.url)); // …/indexer/src
const INDEXER_DIR = path.dirname(HERE);                    // …/indexer
const CONFIG = path.join(INDEXER_DIR, "indexer.config.json");

/** Remonte depuis `start` à la recherche de Assets/pockettunes/common. */
function findCardRoot(start) {
  let dir = start;
  for (let i = 0; i < 6; i++) {
    if (fs.existsSync(path.join(dir, "Assets", "pockettunes", "common"))) return dir;
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return null;
}

function fail(msg) {
  console.error(`\n✗ ${msg}\n`);
  process.exit(1);
}

let musicDir, outDir, sdRoot;

const card = findCardRoot(INDEXER_DIR);
if (card) {
  const common = path.join(card, "Assets", "pockettunes", "common");
  musicDir = path.join(common, "Music");
  outDir = common;
  sdRoot = "/Assets/pockettunes/common/Music";
  console.log(`Carte SD détectée : ${card}`);
} else if (fs.existsSync(CONFIG)) {
  const cfg = JSON.parse(fs.readFileSync(CONFIG, "utf8"));
  musicDir = cfg.musicDir;
  outDir = cfg.outDir;
  sdRoot = cfg.sdRoot;
  if (!musicDir || !outDir || !sdRoot)
    fail(`Complétez les trois chemins dans ${CONFIG}`);
} else {
  fs.writeFileSync(
    CONFIG,
    JSON.stringify(
      {
        "//": "Renseignez vos chemins puis relancez. musicDir = dossier musique sur la carte montée ; outDir = dossier où écrire library.json (…/Assets/pockettunes/common) ; sdRoot = même dossier Music vu par la Pocket (laisser tel quel en général).",
        musicDir: "/Volumes/CARTE/Assets/pockettunes/common/Music",
        outDir: "/Volumes/CARTE/Assets/pockettunes/common",
        sdRoot: "/Assets/pockettunes/common/Music",
      },
      null,
      2
    )
  );
  fail(
    `Premier lancement : je viens de créer ${CONFIG}\n` +
      `  Ouvrez-le, renseignez vos chemins, puis relancez.\n` +
      `  (Ou copiez simplement le dossier indexer/ sur la carte SD : tout sera automatique.)`
  );
}

if (!fs.existsSync(musicDir)) fail(`Dossier musique introuvable : ${musicDir}`);

console.log(`Musique : ${musicDir}`);
console.log(`Sortie  : ${outDir}\n`);

const r = spawnSync(
  process.execPath,
  [path.join(HERE, "index.js"), "-m", musicDir, "-o", outDir, "-r", sdRoot],
  { stdio: "inherit" }
);
process.exit(r.status ?? 1);
