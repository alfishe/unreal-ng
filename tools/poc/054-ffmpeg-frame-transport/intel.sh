#!/bin/sh
# POC 054, Linux on an Intel GPU: every transport x 4K frame x output (VA-API, QSV), interleaved rounds.
# From the repo's scratch/: intel.sh BINARY SRC.bgra(352x288) RENDER_NODE ROUNDS
BIN=$1; SRC=$2; NODE=$3; ROUNDS=${4:-3}
uname -sm; uptime
VA="-vaapi_device $NODE"
for r in $(seq 1 "$ROUNDS"); do
  for size in "3840 2160 4 100" "3840 2160 2 200" "2640 2160 4 100"; do
    set -- $size
    if [ "$3" = 4 ]; then export POC_SRC="$SRC:352x288"; else unset POC_SRC; fi
    for out in "-c copy -f null -" \
               "$VA -vf format=nv12,hwupload -c:v h264_vaapi -qp 18 -f null -" \
               "$VA -vf format=nv12,hwupload -c:v hevc_vaapi -qp 18 -f null -" \
               "-c:v h264_qsv -pix_fmt nv12 -global_quality 18 -f null -"; do
      for m in fifo fifobig stdin unix; do
        printf "r%s %-11s " "$r" "$(echo "$out" | grep -oE '(copy|h264_vaapi|hevc_vaapi|h264_qsv)' | head -1)"
        POC_PIXFMT=bgra POC_OUT="$out" "$BIN" $m $1 $2 $3 $4
      done
    done
  done
done
uptime
