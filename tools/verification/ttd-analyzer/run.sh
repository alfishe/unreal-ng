#!/usr/bin/env bash
# Convenience launcher for the TTD analyzer.
# Usage:
#   ./run.sh analyze   path/to/session.ttd              [--out report/]
#   ./run.sh render    path/to/session.ttd --frames ... [--out frames/]
#   ./run.sh heatmap   path/to/session.ttd               --out heatmap.png
#   ./run.sh validate  path/to/session.ttd
#   ./run.sh info      path/to/session.ttd
set -euo pipefail
# No cd: file arguments stay relative to the caller's directory (run it from
# the project root with root-relative paths); the package is found via PYTHONPATH
DIR="$(cd "$(dirname "$0")" && pwd)"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <command> [args]"
    echo "Commands: analyze, render, heatmap, validate, info"
    exit 1
fi

# Prefer a project-local venv if present, else use system python3.
if [ -x "$DIR/.venv/bin/python" ]; then
    PY="$DIR/.venv/bin/python"
else
    PY="python3"
fi

# src/main.py uses package-relative imports ("from . import __version__"), so it
# has to run as a module - launching it as a script raises ImportError.
PYTHONPATH="$DIR${PYTHONPATH:+:$PYTHONPATH}" exec "$PY" -m src.main "$@"
