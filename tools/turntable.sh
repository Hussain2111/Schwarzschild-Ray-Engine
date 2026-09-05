#!/usr/bin/env bash
# Render a seamless orbit around the black hole and encode it to MP4.
#
# Usage: tools/turntable.sh [frames] [width] [height]
#
# Requires ffmpeg for the encode step; without it you still get the PNG frames.
set -euo pipefail

FRAMES="${1:-120}"
WIDTH="${2:-960}"
HEIGHT="${3:-540}"
OUTDIR="${OUTDIR:-turntable}"
RENDER="${RENDER:-./build/sre-render}"

if [[ ! -x "$RENDER" ]]; then
    echo "error: $RENDER not found. Build first:" >&2
    echo "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build" >&2
    exit 1
fi

mkdir -p "$OUTDIR"

# --spin 360 sweeps a full circle across the frame range, and the disk
# animation phase advances with it, so the loop closes seamlessly.
"$RENDER" \
    --width "$WIDTH" --height "$HEIGHT" \
    --frames "$FRAMES" --spin 360 \
    --samples 2 \
    --output "$OUTDIR/frame.png"

echo "Rendered $FRAMES frames to $OUTDIR/"

if command -v ffmpeg >/dev/null 2>&1; then
    ffmpeg -y -framerate 30 -i "$OUTDIR/frame_%04d.png" \
        -c:v libx264 -pix_fmt yuv420p -crf 18 "$OUTDIR/turntable.mp4"
    echo "Encoded $OUTDIR/turntable.mp4"
else
    echo "ffmpeg not found; frames left in $OUTDIR/ for you to encode."
fi
