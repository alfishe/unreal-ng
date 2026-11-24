#!/bin/sh
# POC 054: the window recorder's transport (stdin today) against a unix socket, the window's picture at the CRT
# sizes of a 1080p and a 4K profile, ingest and the two encoders it uses. Run from the repo's scratch/:
# winrec.sh BINARY SRC.bgra SWxSH ROUNDS  (SRC = BGRA window grabs; each is scaled to the size by FrameScaler)
BIN=$1; SRC=$2; SRCSIZE=$3; ROUNDS=${4:-3}
uname -sm; uptime
for r in $(seq 1 "$ROUNDS"); do
  for size in "1320 1080 300" "2640 2160 120"; do
    set -- $size
    for out in "-c copy -f null -" "-c:v h264_videotoolbox -q:v 85 -pix_fmt yuv420p -f null -" \
               "-c:v libx264 -preset ultrafast -crf 12 -pix_fmt yuv444p -f null -"; do
      for m in fifo stdin unix; do
        printf "r%s %-18s " "$r" "$(echo "$out" | awk '{print $2}')"
        POC_PIXFMT=bgra POC_SRC="$SRC:$SRCSIZE" POC_OUT="$out" "$BIN" $m $1 $2 4 $3
      done
    done
  done
done
uptime
