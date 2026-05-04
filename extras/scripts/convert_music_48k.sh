#!/usr/bin/env bash
set -euo pipefail

src_dir="${1:-extracted_data/assets/music}"
out_dir="${2:-build_audio_48k/music}"

mkdir -p "$out_dir"
for src in "$src_dir"/music*.dat; do
  name="$(basename "$src")"
  ffmpeg -hide_banner -loglevel error -y \
    -i "$src" \
    -ac 2 -ar 48000 -sample_fmt s16 \
    -f wav \
    "$out_dir/$name"
done

echo "Converted music written to $out_dir"
