#!/usr/bin/env bash
# MCP curl session — guided tour of the Unreal-NG MCP server over plain HTTP.
# initialize → notification → tools/list → JSON tool call → SSE progress
# stream → GET keepalive stream → DELETE 405.
#
# Requirements: curl, a running emulator app (WebAPI :8090 + MCP :8092).
#
# Usage:
#   ./docs/features/mcp/examples/curl-session.sh
#   MCP_URL=http://192.168.1.20:8092/mcp ./docs/features/mcp/examples/curl-session.sh

set -euo pipefail

MCP_URL="${MCP_URL:-http://localhost:8092/mcp}"

step() { printf '\n=== %s ===\n' "$1"; }

step "0. server reachable?"
if ! curl -s -m 3 -o /dev/null -X POST "$MCP_URL" \
        -H 'Content-Type: application/json' \
        -d '{"jsonrpc":"2.0","id":0,"method":"ping"}'; then
    echo "cannot reach $MCP_URL — start the emulator app first" >&2
    exit 1
fi
echo "ok"

step "1. initialize (stateless server: courtesy handshake, no session id)"
curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":1,"method":"initialize",
  "params":{"protocolVersion":"2025-03-26","capabilities":{},
            "clientInfo":{"name":"curl-session","version":"0"}}}'
echo

step "2. notifications/initialized (HTTP 202 + empty body)"
curl -s -o /dev/null -w 'HTTP %{http_code}\n' -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'

step "3. tools/list"
curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
echo

step "4. tools/call, JSON answer (no Accept: text/event-stream)"
echo "# progressToken is present, but SSE needs Accept too — progress is dropped:"
curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' -d '{
  "jsonrpc":"2.0","id":3,"method":"tools/call",
  "params":{"name":"inspect_state","arguments":{"aspects":["registers","machine"]},
            "_meta":{"progressToken":"demo"}}}'
echo

step "5. tools/call, SSE answer (-N + Accept + progressToken)"
echo "# one notifications/progress frame per aspect, final frame carries the result:"
curl -N -s -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' -H 'Accept: text/event-stream' -d '{
  "jsonrpc":"2.0","id":4,"method":"tools/call",
  "params":{"name":"inspect_state","arguments":{"aspects":["registers","disasm"]},
            "_meta":{"progressToken":"demo"}}}'
echo

step "6. GET /mcp — keepalive stream (: keepalive every 15 s; --max-time cuts it)"
curl -N -s --max-time 3 "$MCP_URL" || true
echo

step "7. DELETE /mcp → 405 (stateless server, no sessions)"
curl -s -o /dev/null -w 'HTTP %{http_code}\n' -X DELETE "$MCP_URL"

echo
echo "session complete — see docs/features/mcp/README.md for the full protocol"
