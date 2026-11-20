#!/bin/bash
# POC 052: the window recorder's ffmpeg. A Matroska picture stream on stdin is saved to $POC052_CAPTURE (when set)
# and passed on to the real ffmpeg; everything else goes to the real ffmpeg untouched.
REAL=${POC052_FFMPEG:-/opt/homebrew/bin/ffmpeg}  # the real ffmpeg: this wrapper is first on PATH
if [ -n "$POC052_CAPTURE" ] && [[ " $* " == *" -f matroska -i - "* ]]; then
  N=$(ls "$POC052_CAPTURE".*.mkv 2>/dev/null | wc -l | tr -d ' ')
  printf "%s\n" "$@" > "$POC052_CAPTURE.$N.args"
  tee "$POC052_CAPTURE.$N.mkv" | exec "$REAL" "$@"
  exit ${PIPESTATUS[1]}
fi
exec "$REAL" "$@"
