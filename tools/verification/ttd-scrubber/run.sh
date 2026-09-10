#!/usr/bin/env bash
# Convenience launcher for the TTD Scrubber.
# Usage: ./run.sh [WebAPI URL]
set -euo pipefail
cd "$(dirname "$0")"

URL="${1:-http://localhost:8090}"

# Prefer a project-local venv if present, else use system python3.
if [ -x ".venv/bin/python" ]; then
    PY=".venv/bin/python"
else
    PY="python3"
fi

exec "$PY" src/main.py --url "$URL"
