#!/usr/bin/env bash
# Side-by-side before/after video from pairs of clips of the same run.
# Each pair plays at full speed, then again at half speed; pairs crossfade.
#   tools/before-after.sh out.mp4 before1.mp4 after1.mp4 [before2.mp4 after2.mp4 ...]
# Labels: BEFORE_LABEL / AFTER_LABEL (default "before" / "after").
set -euo pipefail
out=$1; shift
(( $# >= 2 && $# % 2 == 0 )) || { echo "usage: $0 out.mp4 before.mp4 after.mp4 [...]" >&2; exit 2; }
font="$(cd "$(dirname "$0")/.." && pwd)/resources/fonts/ChakraPetch-SemiBold.ttf"
before=${BEFORE_LABEL:-before} after=${AFTER_LABEL:-after}
fade=0.4
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

label() { echo "drawtext=fontfile=$font:text='$1':x=24:y=20:fontsize=h/18:fontcolor=white:box=1:boxcolor=0x2a1730@0.75:boxborderw=10"; }
dur() { ffprobe -v error -show_entries format=duration -of csv=p=0 "$1"; }

segs=()
while (( $# )); do
  a=$1 b=$2; shift 2
  n=${#segs[@]}
  # Full speed, then half speed, as one segment.
  ffmpeg -v error -y -i "$a" -i "$b" -filter_complex "
    [0:v]$(label "$before"),split[a1][a2];
    [1:v]$(label "$after"),split[b1][b2];
    [a1][b1]hstack,fps=30[full];
    [a2][b2]hstack,setpts=2*PTS,fps=30[slow];
    [full][slow]concat=n=2:v=1[v]" \
    -map '[v]' -an -c:v libx264 -crf 16 -preset slow -pix_fmt yuv420p "$tmp/seg$n.mp4"
  segs+=("$tmp/seg$n.mp4")
done

# Crossfade the segments into one video.
inputs=() chain="" prev="[0:v]" offset=0
for i in "${!segs[@]}"; do inputs+=(-i "${segs[$i]}"); done
for (( i=1; i<${#segs[@]}; i++ )); do
  offset=$(echo "$offset + $(dur "${segs[$((i-1))]}") - $fade" | bc)
  chain+="$prev[$i:v]xfade=transition=fade:duration=$fade:offset=$offset[x$i];"
  prev="[x$i]"
done
if (( ${#segs[@]} == 1 )); then cp "${segs[0]}" "$out"; exit; fi
ffmpeg -v error -y "${inputs[@]}" -filter_complex "${chain%;}" -map "$prev" -an \
  -c:v libx264 -crf 18 -preset slow -pix_fmt yuv420p -movflags +faststart "$out"
