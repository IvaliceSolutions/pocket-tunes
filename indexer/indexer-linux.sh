#!/bin/bash
# Pocket Tunes — indexeur (Linux). Lancez : ./indexer-linux.sh
#
# Utilise Node s'il est déjà installé ; sinon télécharge une version PORTABLE
# dans indexer/runtime/ (aucune installation système, aucun droit root).
# Les chemins se règlent dans indexer.config.json (créé au premier lancement),
# ou automatiquement si ce dossier indexer/ est copié sur la carte SD.
set -e
cd "$(dirname "$0")"

NODE_VERSION=v22.12.0
case "$(uname -m)" in
  aarch64|arm64) A=linux-arm64 ;;
  *) A=linux-x64 ;;
esac
PDIR="runtime/node-$NODE_VERSION-$A"

if command -v node >/dev/null 2>&1 && [ "$(node -p 'parseInt(process.versions.node)')" -ge 18 ]; then
  NODE=node
  NPM="npm"
else
  if [ ! -x "$PDIR/bin/node" ]; then
    echo "Node absent : téléchargement d'une version portable (une seule fois)…"
    mkdir -p runtime
    curl -# -L "https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-$A.tar.xz" | tar -xJ -C runtime
  fi
  NODE="$PDIR/bin/node"
  NPM="$NODE $PDIR/lib/node_modules/npm/bin/npm-cli.js"
fi

if [ ! -d node_modules ]; then
  echo "Installation des dépendances (une seule fois)…"
  $NPM install --omit=dev --no-audit --no-fund
fi

$NODE src/run.js
