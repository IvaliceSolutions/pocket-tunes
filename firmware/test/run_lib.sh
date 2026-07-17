#!/bin/bash
# Host validation of the library parser (lib.c, schema v1+v2) and the chapter
# reader (chap.c). Needs a v2 library.json; pass one, or it builds the
# fixture with the indexer first (needs node + ffmpeg).
set -e
D="$(cd "$(dirname "$0")" && pwd)"
LIB_JSON="${1:-}"

if [ -z "$LIB_JSON" ]; then
  FIX="$D/fixture-v2"
  OUT="$D/fixture-out"
  if [ ! -f "$OUT/library.json" ]; then
    rm -rf "$FIX" "$OUT"
    mkdir -p "$FIX/Artiste Un/Album A" "$FIX/Artiste Un/Album B" \
             "$FIX/Artiste Deux" "$FIX/Artiste Trois/Album C"
    G() { ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=$3:duration=1" \
          -metadata title="$2" -metadata track="$4" -metadata date=2020 \
          -codec:a libmp3lame -b:a 64k "$1"; }
    G "$FIX/Artiste Un/Album A/01 Un.mp3"        "Piste Une"    440 1
    G "$FIX/Artiste Un/Album A/02 Deux.mp3"      "Piste Deux"   550 2
    G "$FIX/Artiste Un/Album B/01 Trois.mp3"     "Piste Trois"  660 1
    G "$FIX/Artiste Deux/Single Alpha.mp3"       "Single Alpha" 330 1
    G "$FIX/Artiste Deux/Single Beta.mp3"        "Single Bêta"  350 2
    G "$FIX/Artiste Trois/Album C/01 Quatre.mp3" "Piste Quatre" 770 1
    G "$FIX/Artiste Trois/En vrac.mp3"           "En vrac"      380 1
    # chaptered audiobook (ID3 CHAP) + a root-level opus
    cat > "$FIX/.chapmeta" <<'EOF'
;FFMETADATA1
title=Test Audiobook
[CHAPTER]
TIMEBASE=1/1000
START=0
END=4000
title=Chapitre 1 - Le début
[CHAPTER]
TIMEBASE=1/1000
START=4000
END=8000
title=Chapitre 2 - Le milieu
[CHAPTER]
TIMEBASE=1/1000
START=8000
END=12000
title=Chapitre 3 - La fin
EOF
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=200:duration=12" \
      -i "$FIX/.chapmeta" -map_metadata 1 -map_chapters 1 \
      -codec:a libmp3lame -b:a 64k -write_id3v2 1 "$FIX/Livre audio.mp3"
    ffmpeg -loglevel error -y -f lavfi -i "sine=frequency=300:duration=2" \
      -metadata title="Discours" -c:a libopus -b:a 32k "$FIX/Discours.opus"
    rm "$FIX/.chapmeta"
  fi
  (cd "$D/../../indexer" && node src/index.js -m "$FIX" -o "$OUT" -r /Music -q)
  LIB_JSON="$OUT/library.json"
fi

cc -DPT_HOST_TEST -O2 -I"$D/../src" \
  "$D/host_lib_test.c" "$D/../src/lib.c" "$D/../src/chap.c" \
  -o "$D/host_lib_test"

"$D/host_lib_test" "$LIB_JSON"
