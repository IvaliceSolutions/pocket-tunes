#!/bin/bash
# Deploy a CI build artifact to the Pocket SD card.
#
# Usage:
#   gh run download <run-id> --repo IvaliceSolutions/pocket-tunes -n pocket-tunes-sd -D /tmp/pt-sd
#   core/scripts/deploy_sd.sh /tmp/pt-sd "/Volumes/128 GO"
set -e
SRC="${1:?usage: deploy_sd.sh <artifact-dir> <sd-mount-point>}"
SD="${2:?usage: deploy_sd.sh <artifact-dir> <sd-mount-point>}"

[ -f "$SRC/Cores/jh.Tunes/pocket_tunes.rev" ] || { echo "no pocket_tunes.rev in $SRC"; exit 1; }
[ -d "$SD" ] || { echo "SD not mounted at $SD"; exit 1; }

mkdir -p "$SD/Cores" "$SD/Platforms"
cp -Rv "$SRC/Cores/jh.Tunes" "$SD/Cores/"
cp -v "$SRC/Platforms/pockettunes.json" "$SD/Platforms/"
# Assets (library.json + covers) come from the indexer, not the build — leave them alone.
echo
echo "Done. Eject the card, then: Pocket → openFPGA → Pocket Tunes."
