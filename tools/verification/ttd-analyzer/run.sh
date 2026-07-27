#!/usr/bin/env bash
# Convenience launcher for the TTD analyzer.
# Usage:
#   ./run.sh analyze   path/to/session.ttd              [--out report/]
#   ./run.sh render    path/to/session.ttd --frames ... [--out frames/]
#   ./run.sh heatmap   path/to/session.ttd               --out heatmap.png
#   ./run.sh validate  path/to/session.ttd
#   ./run.sh info      path/to/session.ttd
set -euo pipefail
cd "$(dirname "$0")"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <command> [args]"
    echo "Commands: analyze, render, heatmap, validate, info"
    exit 1
fi

# Prefer a project-local venv if present, else use system python3.
if [ -x ".venv/bin/python" ]; then
    PY=".venv/bin/python"
else
    PY="python3"
fi

exec "$PY" src/main.py "$@"
