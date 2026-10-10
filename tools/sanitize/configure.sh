#!/usr/bin/env bash
# Configure a sanitizer build directory once; build into it with tools/build/build.sh afterwards.
#
# Usage: tools/sanitize/configure.sh <kind>
#   asan    AddressSanitizer + UndefinedBehaviorSanitizer   -> cmake-build-asan
#   tsan    ThreadSanitizer                                 -> cmake-build-tsan
# Then:  BUILD_DIR=$PWD/cmake-build-asan tools/build/build.sh core-tests   (or unreal-qt)
# Guide: docs/testing/sanitizers.md
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
case "${1:-}" in
  asan) DIR=cmake-build-asan; SAN="address,undefined" ;;
  tsan) DIR=cmake-build-tsan; SAN="thread" ;;
  *) sed -n '2,9p' "${BASH_SOURCE[0]}"; exit 2 ;;
esac
OPENSSL=()
# A fresh configure picks Homebrew's openssl@4 when present; the tree builds against openssl@3
[ -d /opt/homebrew/opt/openssl@3 ] && OPENSSL=(-U 'OPENSSL*' -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3)
cmake -S . -B "$DIR" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTESTS=ON ${OPENSSL[@]+"${OPENSSL[@]}"} \
  "-DSANITIZE=$SAN" > "$DIR.configure.log" 2>&1 \
  || { cat "$DIR.configure.log"; exit 1; }
rm -f "$DIR.configure.log"
echo "$DIR configured ($SAN). Build: BUILD_DIR=$ROOT/$DIR tools/build/build.sh core-tests"
