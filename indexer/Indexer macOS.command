#!/bin/bash
# Pocket Tunes — indexeur (macOS). Double-cliquez ce fichier.
#
# Utilise Node s'il est déjà installé ; sinon télécharge une version PORTABLE
# dans indexer/runtime/ (aucune installation système, aucun droit admin).
# Les chemins se règlent dans indexer.config.json (créé au premier lancement),
# ou automatiquement si ce dossier indexer/ est copié sur la carte SD.
set -e
cd "$(dirname "$0")"

NODE_VERSION=v22.12.0
[ "$(uname -m)" = "arm64" ] && A=darwin-arm64 || A=darwin-x64
PDIR="runtime/node-$NODE_VERSION-$A"

if command -v node >/dev/null 2>&1 && [ "$(node -p 'parseInt(process.versions.node)')" -ge 18 ]; then
  NODE=node
  NPM="npm"
else
  if [ ! -x "$PDIR/bin/node" ]; then
    echo "Node absent : téléchargement d'une version portable (une seule fois)…"
    mkdir -p runtime
    curl -# -L "https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-$A.tar.gz" | tar -xz -C runtime
  fi
  NODE="$PDIR/bin/node"
  NPM="$NODE $PDIR/lib/node_modules/npm/bin/npm-cli.js"
fi

if [ ! -d node_modules ]; then
  echo "Installation des dépendances (une seule fois)…"
  $NPM install --omit=dev --no-audit --no-fund
fi

$NODE src/run.js
echo ""
read -p "Terminé. Appuyez sur Entrée pour fermer." _
