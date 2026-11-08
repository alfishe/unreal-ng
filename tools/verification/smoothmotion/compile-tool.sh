#!/bin/bash
# Builds a smooth-motion measuring tool against the core's CPU reference, without CMake: the one source list for
# the filters harness (filters.cpp), live/replay.cpp, POC 048's smoothmotion-eval.cpp and POC 047's run.sh.
#
#   compile-tool.sh <tool.cpp> <output binary> [extra compiler flags]
#
# CXX (default clang++) and CXXFLAGS (default -O2) override.
set -euo pipefail
cd "$(dirname "$0")/../../.."
TOOL=${1:?tool source}
OUT=${2:?output binary}
shift 2
CORE=(
    core/src/emulator/video/smoothmotion.cpp
    core/src/emulator/video/smoothlayers.cpp
    core/src/common/threadpool.cpp
    core/src/common/threadhelper.cpp
)
mkdir -p "$(dirname "$OUT")"
${CXX:-clang++} -std=c++20 ${CXXFLAGS:--O2} -Wall -Wextra -Icore/src "$@" "$TOOL" "${CORE[@]}" -o "$OUT"
