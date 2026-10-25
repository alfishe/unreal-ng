#!/usr/bin/env bash
# POC 035 on macOS: build every variant and run the whole measurement matrix, unattended (~40 min).
#
#   bash tools/poc/035-debugger-gui-bench/scripts/run_mac.sh       # from anywhere inside the checkout
#   POC035_OUT=scratch/<date>-poc-035 ... run_mac.sh                # another output folder
#   POC035_REPS=1 ... run_mac.sh                                    # quick pass (1 repetition instead of 3)
#
# Leave the Mac alone while it runs: every variant opens a 1600 x 1000 pt window that must stay in front and
# unobscured (an occluded window is throttled by macOS), and other activity shows up in the CPU numbers.
# Resumable: results that exist are skipped; delete a JSON to re-run it.
#
# Output (git-ignored, docs/guidelines/poc-guidelines.md): $POC035_OUT/{deps,build,results,logs,shots,env.txt,summary.md}
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
POC=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$POC/../../.." && pwd)
OUT=${POC035_OUT:-$ROOT/scratch/2026-10-24-poc-035}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
REPS=${POC035_REPS:-3}
[ "$(uname)" = Darwin ] || { echo "run_mac.sh is for macOS (Linux: scripts/run_linux.sh)"; exit 1; }
# Keep the display and the machine awake for the whole run
if [ -z "${POC035_CAFFEINATED:-}" ]; then
  POC035_CAFFEINATED=1 exec caffeinate -dimsu bash "$0" "$@"
fi

log() { echo "$(date +%T) $*" | tee -a "$OUT/run.log"; }

# --- 0. the machine -----------------------------------------------------------------------------------------------
{
  echo "date: $(date)"
  sw_vers
  echo "cpu: $(sysctl -n machdep.cpu.brand_string), $(sysctl -n hw.ncpu) logical cores ($(sysctl -n hw.perflevel0.logicalcpu 2>/dev/null) P + $(sysctl -n hw.perflevel1.logicalcpu 2>/dev/null) E)"
  echo "memory: $(( $(sysctl -n hw.memsize) / 1073741824 )) GB"
  echo "load: $(sysctl -n vm.loadavg)"
  echo "power: $(pmset -g | grep -E 'lowpowermode|powermode' | tr -s ' ' | tr '\n' ' ')"
  echo "displays:"
  system_profiler SPDisplaysDataType 2>/dev/null | grep -E 'Chipset Model|Resolution|UI Looks like|Main Display|Retina|Refresh' | sed 's/^ */  /'
  echo "repo: $(git -C "$ROOT" rev-parse --short=12 HEAD) $(git -C "$ROOT" status --porcelain | wc -l | tr -d ' ') changed files"
} > "$OUT/env.txt"
cat "$OUT/env.txt"
if ! ps -o time= -p "$(pgrep -x WindowServer | head -1)" >/dev/null 2>&1; then
  log "WARNING: WindowServer's CPU time is not readable by this user; ws_cpu_pct will be missing"
fi

# --- 1. third-party libraries at pinned commits -------------------------------------------------------------------
log "deps"
bash "$HERE/fetch-deps.sh" "$OUT/deps" || { log "fetch-deps failed"; exit 1; }

# --- 2. build through the project's build wrapper -----------------------------------------------------------------
B=$OUT/build
if [ ! -f "$B/CMakeCache.txt" ]; then
  log "configure $B (BUILD_POC=ON, POC035_GUI_BENCH=ON)"
  cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_POC=ON -DPOC035_GUI_BENCH=ON \
        -DPOC035_DEPS_PREFIX="$OUT/deps/prefix" > "$OUT/configure.log" 2>&1 \
    || { tail -30 "$OUT/configure.log"; log "configure failed (log: $OUT/configure.log)"; exit 1; }
fi
log "build poc035-all"
BUILD_DIR="$B" "$ROOT/tools/build/build.sh" poc035-all > "$OUT/build.log" 2>&1 \
  || { tail -40 "$OUT/build.log"; log "build failed (log: $OUT/build.log)"; exit 1; }
grep -E 'warning:' "$OUT/build.log" | grep -v '/deps/' | sort -u > "$OUT/build-warnings.txt"
log "build ok, $(wc -l < "$OUT/build-warnings.txt" | tr -d ' ') warnings in POC code"
BIN=$B/bin/poc035

# --- 3. the matrix ------------------------------------------------------------------------------------------------
R=$OUT/results
mkdir -p "$R/gpu" "$R/cpu" "$R/px1" "$R/switch" "$R/views" "$OUT/logs" "$OUT/shots"
run() {  # run <out.json> <binary> [args...]; the environment passes through
  local out=$1; shift
  [ -f "$out" ] && return 0
  local name
  name=$(basename "$(dirname "$out")")-$(basename "$out" .json)
  python3 "$POC/python/run_with_wscpu.py" "$out" "$@" > "$OUT/logs/$name.log" 2>&1 \
    || log "FAILED $name (log: $OUT/logs/$name.log)"
  sleep 2   # let WindowServer settle between windows
}

# Screenshots first (short runs): what each variant draws, with and without the GPU
SHOT=(--active=3 --unthrottled=0.1 --idle=0.1 --warmup=1 --screenshot-frame=100)
log "screenshots"
run "$OUT/shots/imgui.json" "$BIN/poc035-imgui" "${SHOT[@]}" --screenshot="$OUT/shots/imgui.png"
POC035_GL_SOFTWARE=1 run "$OUT/shots/imgui-swgl.json" "$BIN/poc035-imgui" "${SHOT[@]}" --screenshot="$OUT/shots/imgui-swgl.png"
run "$OUT/shots/rmlui-dom.json" "$BIN/poc035-rmlui-dom" "${SHOT[@]}" --screenshot="$OUT/shots/rmlui-dom.png"
run "$OUT/shots/rmlui-custom.json" "$BIN/poc035-rmlui-custom" "${SHOT[@]}" --screenshot="$OUT/shots/rmlui-custom.png"
run "$OUT/shots/qtwidgets.json" "$BIN/poc035-qtwidgets" "${SHOT[@]}" --screenshot="$OUT/shots/qtwidgets.png"
run "$OUT/shots/qtwidgets-raster.json" "$BIN/poc035-qtwidgets-raster" "${SHOT[@]}" --screenshot="$OUT/shots/qtwidgets-raster.png"
for v in qtquick qtquick-raster qtquick-sg; do
  run "$OUT/shots/$v.json" "$BIN/poc035-$v" "${SHOT[@]}" --screenshot="$OUT/shots/$v.png"
  QT_QUICK_BACKEND=software run "$OUT/shots/$v-software.json" "$BIN/poc035-$v" "${SHOT[@]}" --screenshot="$OUT/shots/$v-software.png"
done

for rep in $(seq 1 "$REPS"); do
  # gpu/: what each variant costs with the GPU in use (Metal for Qt Quick, the platform default; OpenGL for SDL)
  for v in imgui rmlui-custom rmlui-dom qtwidgets qtwidgets-raster qtquick qtquick-raster qtquick-sg; do
    run "$R/gpu/${v}_r$rep.json" "$BIN/poc035-$v"
  done
  POC035_RHI=opengl run "$R/gpu/qtquick-raster-opengl_r$rep.json" "$BIN/poc035-qtquick-raster"
  # cpu/: the application uses no GPU. Apple's software OpenGL renderer for the SDL variants, Qt's raster for Widgets
  # (the same binaries as gpu/: Widgets never used the GPU), the Quick software adaptation.
  # WindowServer still composites on the GPU: macOS has no way to switch that off.
  POC035_GL_SOFTWARE=1 run "$R/cpu/imgui_r$rep.json" "$BIN/poc035-imgui"
  POC035_GL_SOFTWARE=1 run "$R/cpu/rmlui-custom_r$rep.json" "$BIN/poc035-rmlui-custom"
  for v in qtquick qtquick-raster qtquick-sg; do
    QT_QUICK_BACKEND=software run "$R/cpu/${v}-software_r$rep.json" "$BIN/poc035-$v"
  done
  log "rep $rep done"
done

# px1/: one framebuffer pixel per logical pixel (1600 x 1000 px, as on the Linux machines): --scale=0.5 on a 2x display
log "px1"
DPR=$(python3 -c "import json;print(json.load(open('$R/gpu/imgui_r1.json')).get('device_pixel_ratio',1))" 2>/dev/null || echo 1)
if [ "${DPR%%.*}" = 2 ]; then
  run "$R/px1/imgui.json" "$BIN/poc035-imgui" --scale=0.5
  POC035_GL_SOFTWARE=1 run "$R/px1/imgui-swgl.json" "$BIN/poc035-imgui" --scale=0.5
  run "$R/px1/qtwidgets-raster.json" "$BIN/poc035-qtwidgets-raster" --scale=0.5
  run "$R/px1/qtquick-raster.json" "$BIN/poc035-qtquick-raster" --scale=0.5
  QT_QUICK_BACKEND=software run "$R/px1/qtquick-raster-software.json" "$BIN/poc035-qtquick-raster" --scale=0.5
else
  log "display pixel ratio $DPR: px1 runs not needed (gpu/ and cpu/ are already 1 px per logical pixel)"
fi

# switch/: run-time font size 12 <-> 16 px and UI scale x1 <-> x1.5, every 250 ms for 10 s
log "switch"
for v in imgui rmlui-custom rmlui-dom qtwidgets qtwidgets-raster qtquick qtquick-raster qtquick-sg; do
  run "$R/switch/${v}_font.json" "$BIN/poc035-$v" --switch=font
  run "$R/switch/${v}_scale.json" "$BIN/poc035-$v" --switch=scale
done

# views/: each dense view alone, for the variants the recommendation is about
log "views"
for view in disasm hex images timeline; do
  V=(--views=$view --active=8 --unthrottled=2 --idle=1)
  run "$R/views/qtwidgets-raster_$view.json" "$BIN/poc035-qtwidgets-raster" "${V[@]}"
  run "$R/views/qtquick-raster_$view.json" "$BIN/poc035-qtquick-raster" "${V[@]}"
  QT_QUICK_BACKEND=software run "$R/views/qtquick-raster-software_$view.json" "$BIN/poc035-qtquick-raster" "${V[@]}"
  run "$R/views/imgui_$view.json" "$BIN/poc035-imgui" "${V[@]}"
done

# --- 4. summary ---------------------------------------------------------------------------------------------------
python3 "$POC/python/summarize_matrix.py" "$R" > "$OUT/summary.md" 2> "$OUT/summary.err" \
  || log "summary failed (see $OUT/summary.err)"
log "DONE: $OUT/summary.md"
