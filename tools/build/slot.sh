#!/usr/bin/env bash
# Thin wrapper: finds a Python 3 and runs slot.py. See slot.py for what it does and
# tools/build/README.md for how agents use it.
HERE=$(cd "$(dirname "$0")" && pwd)
for py in python3 python; do
  if command -v "$py" >/dev/null 2>&1 && "$py" -c 'import sys; sys.exit(sys.version_info < (3, 6))' 2>/dev/null; then
    exec "$py" "$HERE/slot.py" "$@"
  fi
done
echo "slot.sh: Python 3 is required (python3 not found)" >&2
exit 70
