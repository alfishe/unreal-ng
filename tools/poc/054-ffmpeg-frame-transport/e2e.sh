#!/bin/sh
# e2e.sh TRANSPORT SRC ROUNDS: 4K libx264 recording of 352x288 pictures, today (4K RGBA in) against the source in
C="scale=in_range=full:out_range=limited:out_color_matrix=bt709"; ENC="-c:v libx264 -preset ultrafast -crf 23 -f null -"
OLD="-vf $C,format=yuv420p $ENC"
NEW="-vf $C,format=yuv444p,scale=2640:2160:flags=neighbor,pad=3840:2160:600:0:black,scale=flags=area,format=yuv420p $ENC"
uname -sm
for r in $(seq 1 "$3"); do
  POC_SRC="$2:352x288" POC_OUT="$OLD" "$1" fifo 3840 2160 4 200
  POC_SRC="$2:352x288" POC_OUT="$OLD" "$1" unix 3840 2160 4 200
  POC_SRC="$2:352x288" POC_OUT="$NEW" "$1" fifo 352 288 4 200
  uptime
done
