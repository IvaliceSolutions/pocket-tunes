#!/bin/bash
# Host validation of the Opus playback chain (opus_play.c + ogg.c + libopus),
# same structure as run.sh for MP3. Decodes test_tone.opus through the real
# firmware code path and compares against ffmpeg's decode of the same file.
set -e
cd "$(dirname "$0")"
D=..
OPUS=$D/opus
CC=${CC:-cc}

# Match the firmware's libopus configuration (build.sh), minus PT_FIRMWARE so
# libopus uses its normal (VAR_ARRAYS) allocation on the host.
DEFS="-DOPUS_BUILD -DFIXED_POINT -DDISABLE_FLOAT_API -DVAR_ARRAYS -DPT_HOST_TEST -DOPUS_VERSION=\"1.5.2-pt\""
INC="-I$D/src -I$OPUS/include -I$OPUS/celt -I$OPUS/silk -I$OPUS/silk/fixed -I$OPUS/src"

echo "compiling host_opus_test (libopus decoder + ogg + opus_play)..."
$CC -O2 $DEFS $INC \
  host_opus_test.c $D/src/opus_play.c $D/src/ogg.c \
  "$OPUS"/src/*.c "$OPUS"/celt/*.c "$OPUS"/silk/*.c "$OPUS"/silk/fixed/*.c \
  -lm -o host_opus_test

IN=${1:-test_tone.opus}
# Generate the test signal on the fly if absent: 3 s, 440 Hz left / 660 Hz
# right, so channel separation and tone accuracy are both checkable.
if [ ! -f "$IN" ] && [ "$IN" = "test_tone.opus" ]; then
  echo "generating test_tone.opus..."
  ffmpeg -v error -y \
    -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=3" \
    -f lavfi -i "sine=frequency=660:sample_rate=48000:duration=3" \
    -filter_complex "[0:a][1:a]join=inputs=2:channel_layout=stereo[a]" -map "[a]" \
    -c:a libopus -b:a 96k -vbr on test_tone.opus
fi
echo "decoding $IN through firmware chain..."
./host_opus_test "$IN" > out_opus.raw

echo "reference decode via ffmpeg..."
ffmpeg -v error -y -i "$IN" -f s16le -acodec pcm_s16le -ar 48000 -ac 2 ref_opus.raw
python3 compare_opus.py out_opus.raw ref_opus.raw
