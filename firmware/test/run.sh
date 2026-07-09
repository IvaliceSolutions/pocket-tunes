#!/bin/bash
# Host validation of the firmware audio chain (mp3.c + Helix + resampler).
# Decodes a test MP3 natively and checks it against an ffmpeg reference:
# correct L/R tones and near-perfect correlation. Runs in ~2 s.
set -e
D="$(cd "$(dirname "$0")" && pwd)"
SIM="$D/../../core/sim"

# make a deterministic stereo test tone (440 Hz L / 880 Hz R) if missing
if [ ! -f "$SIM/test_audio.mp3" ]; then
  ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:duration=2" \
    -f lavfi -i "sine=frequency=880:duration=2" \
    -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" \
    -ar 44100 -b:a 128k -c:a libmp3lame "$SIM/test_audio.mp3"
fi
ffmpeg -y -loglevel error -i "$SIM/test_audio.mp3" -ar 48000 -ac 2 -f s16le "$SIM/reference_48k.raw"
SIZE=$(wc -c < "$SIM/test_audio.mp3" | tr -d ' ')

cc -DPT_HOST_TEST -DHELIX_GENERIC_C -O2 \
  -I"$D/../src" -I"$D/../shim" -I"$D/../helix/pub" -I"$D/../helix/real" \
  "$D/host_mp3_test.c" "$D/../src/mp3.c" \
  "$D/../helix/mp3dec.c" "$D/../helix/mp3tabs.c" "$D"/../helix/real/*.c \
  -o "$D/host_mp3_test" 2>/dev/null

"$D/host_mp3_test" "$SIM/test_audio.mp3" "$SIZE" > "$D/host_pcm.raw"
python3 "$D/analyze.py" "$D/host_pcm.raw" "$SIM/reference_48k.raw"
