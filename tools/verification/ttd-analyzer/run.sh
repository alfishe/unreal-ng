#!/usr/bin/env bash
# Convenience launcher for the TTD analyzer.
# Usage:
#   ./run.sh info      path/to/session.ttd
#   ./run.sh validate  path/to/session.ttd
#   ./run.sh parts     path/to/session.ttd
#   ./run.sh recover   path/to/session.ttd
set -euo pipefail
# No cd: file arguments stay relative to the caller's directory (run it from
# the project root with root-relative paths); the package is found via PYTHONPATH
DIR="$(cd "$(dirname "$0")" && pwd)"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <command> [args]"
    echo "Commands: info, validate, parts, recover"
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
