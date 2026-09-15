#!/usr/bin/env bash
# MCP smoke test — end-to-end verification of the MCP server + bridge,
# including SSE progress streaming (POST answer mode, GET keepalive, 405s)
# and the bridge's SSE → stdout-line translation.
# Follows the AGENTS.md WebAPI verification flow (fresh instance, cleanup).
#
# Usage: ./scripts/mcp-smoke-test.sh   (from the repository root)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/cmake-build-release"
MCP_URL="http://localhost:8092/mcp"

# Platform-specific binary paths (AGENTS.md)
case "$(uname -s)" in
    Darwin) QT_APP="$BUILD/bin/unreal-qt.app/Contents/MacOS/unreal-qt" ;;
    Linux)  QT_APP="$BUILD/bin/unreal-qt" ;;
    MINGW*|MSYS*|CYGWIN*) QT_APP="$BUILD/bin/unreal-qt.exe" ;;
    *) echo "Unsupported platform: $(uname -s)"; exit 2 ;;
esac

BRIDGE="$BUILD/bin/unreal-mcp-bridge"
if [ ! -x "$BRIDGE" ]; then
    echo "FAIL: bridge not built: $BRIDGE (ninja -C cmake-build-release unreal-mcp-bridge)"
    exit 1
fi
if [ ! -x "$QT_APP" ]; then
    echo "FAIL: emulator app not found: $QT_APP"
    exit 1
fi

cleanup() {
    pkill -9 unreal-qt 2>/dev/null || true
}
trap cleanup EXIT

echo "== 1. Kill stale instances (only one can bind :8090/:8092)"
pkill -9 unreal-qt 2>/dev/null || true
sleep 1

echo "== 2. Start the emulator (MCP listener comes up with WebAPI)"
"$QT_APP" >/dev/null 2>&1 &
sleep 4

echo "== 3. initialize handshake"
INIT=$(curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"smoke","version":"0"}}}')
echo "$INIT" | jq -c '{protocolVersion: .result.protocolVersion, server: .result.serverInfo.name}' >/dev/null
echo "$INIT" | jq -e '.result.serverInfo.name == "unreal-ng"' >/dev/null \
    || { echo "FAIL: initialize: $INIT"; exit 1; }

echo "== 4. notification → HTTP 202 + empty body"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","method":"notifications/initialized"}')
[ "$STATUS" = "202" ] || { echo "FAIL: notification expected 202, got $STATUS"; exit 1; }

echo "== 5. tools/list"
TOOLS=$(curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}')
echo "$TOOLS" | jq -r '.result.tools[].name' | sed 's/^/    /'
TOOL_COUNT=$(echo "$TOOLS" | jq '.result.tools | length')
# Full toolset: 5 core + 4 Phase-2 smart tools + 2 router tools
EXPECTED=11
[ "$TOOL_COUNT" -ge "$EXPECTED" ] \
    || { echo "FAIL: expected >= $EXPECTED tools, got $TOOL_COUNT"; exit 1; }

echo "== 6. ping"
PING=$(curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":3,"method":"ping"}')
echo "$PING" | jq -e '.result == {}' >/dev/null || { echo "FAIL: ping: $PING"; exit 1; }

echo "== 7. tools/call emulator_manage list (loopback WebAPI round-trip)"
LIST=$(curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"emulator_manage","arguments":{"action":"list"}}}')
echo "$LIST" | jq -c '{isError: .result.isError, emulators: (.result.structuredContent.emulators | length)}' >/dev/null
echo "$LIST" | jq -e 'has("result")' >/dev/null || { echo "FAIL: emulator_manage list: $LIST"; exit 1; }
echo "$LIST" | jq -c '.result.content[0].text' | sed 's/^/    /'

echo "== 8. unknown method → -32601"
UNKNOWN=$(curl -s -X POST "$MCP_URL" -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":5,"method":"no/such/method"}')
echo "$UNKNOWN" | jq -e '.error.code == -32601' >/dev/null || { echo "FAIL: unknown method: $UNKNOWN"; exit 1; }

echo "== 9. malformed JSON → HTTP 400"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' -d '{not json')
[ "$STATUS" = "400" ] || { echo "FAIL: malformed JSON expected 400, got $STATUS"; exit 1; }

echo "== 10. bridge round-trip (stdio → :8092)"
BRIDGE_PING=$(echo '{"jsonrpc":"2.0","id":99,"method":"ping"}' | "$BRIDGE")
echo "$BRIDGE_PING" | jq -e '.id == 99 and .result == {}' >/dev/null \
    || { echo "FAIL: bridge round-trip: $BRIDGE_PING"; exit 1; }

echo "== 11. resources/list (6 resources)"
RESOURCES=$(echo '{"jsonrpc":"2.0","id":6,"method":"resources/list"}' | "$BRIDGE")
echo "$RESOURCES" | jq -e '.result.resources | length == 6' >/dev/null \
    || { echo "FAIL: resources/list: $RESOURCES"; exit 1; }

echo "== 12. SSE answer mode (POST + Accept: text/event-stream + progressToken)"
HEADERS_FILE="$(mktemp)"
SSE=$(curl -N -s -D "$HEADERS_FILE" -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' -H 'Accept: text/event-stream' \
    -d '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"inspect_state","arguments":{"aspects":["registers","machine"]},"_meta":{"progressToken":"smoke"}}}')
grep -qi '^content-type: text/event-stream' "$HEADERS_FILE" \
    || { echo "FAIL: SSE answer expected text/event-stream"; cat "$HEADERS_FILE"; rm -f "$HEADERS_FILE"; exit 1; }
PROGRESS_FRAMES=$(printf '%s\n' "$SSE" | grep -c '^data: {"jsonrpc":"2.0","method":"notifications/progress"' || true)
[ "$PROGRESS_FRAMES" -eq 2 ] \
    || { echo "FAIL: expected 2 progress frames (one per aspect), got $PROGRESS_FRAMES"; exit 1; }
LAST_DATA=$(printf '%s\n' "$SSE" | grep '^data:' | tail -1)
LAST_DATA="${LAST_DATA#data: }"   # strip SSE prefix so jq sees pure JSON
echo "$LAST_DATA" | jq -e '.id == 7 and has("result")' >/dev/null \
    || { echo "FAIL: final SSE frame should carry the id=7 result"; exit 1; }
echo "    2 notifications/progress frames, then the result frame — OK"
rm -f "$HEADERS_FILE"

echo "== 13. progressToken without Accept stays JSON"
HEADERS_FILE="$(mktemp)"
JSON_ANSWER=$(curl -s -D "$HEADERS_FILE" -X POST "$MCP_URL" \
    -H 'Content-Type: application/json' \
    -d '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"inspect_state","arguments":{"aspects":["registers"]},"_meta":{"progressToken":"smoke"}}}')
grep -qi '^content-type: application/json' "$HEADERS_FILE" \
    || { echo "FAIL: no Accept header → expected application/json"; cat "$HEADERS_FILE"; rm -f "$HEADERS_FILE"; exit 1; }
echo "$JSON_ANSWER" | jq -e '.id == 8 and has("result")' >/dev/null \
    || { echo "FAIL: JSON answer: $JSON_ANSWER"; exit 1; }
rm -f "$HEADERS_FILE"

echo "== 14. GET /mcp keepalive stream (immediate frame)"
GET_STREAM=$(curl -N -s --max-time 3 "$MCP_URL" || true)
printf '%s' "$GET_STREAM" | grep -q '^: keepalive' \
    || { echo "FAIL: GET stream should open with a keepalive comment"; exit 1; }

echo "== 15. DELETE/HEAD /mcp → 405 (stateless server)"
STATUS=$(curl -s -o /dev/null -w '%{http_code}' -X DELETE "$MCP_URL")
[ "$STATUS" = "405" ] || { echo "FAIL: DELETE expected 405, got $STATUS"; exit 1; }
STATUS=$(curl -s -o /dev/null -w '%{http_code}' -I "$MCP_URL")
[ "$STATUS" = "405" ] || { echo "FAIL: HEAD expected 405, got $STATUS"; exit 1; }

echo "== 16. bridge SSE translation (progress lines, then result line)"
BRIDGE_SSE=$(echo '{"jsonrpc":"2.0","id":100,"method":"tools/call","params":{"name":"inspect_state","arguments":{"aspects":["registers","machine"]},"_meta":{"progressToken":1}}}' | "$BRIDGE")
LINE_COUNT=$(printf '%s\n' "$BRIDGE_SSE" | grep -c . || true)
[ "$LINE_COUNT" -eq 3 ] \
    || { echo "FAIL: expected 3 stdout lines (2 progress + result), got $LINE_COUNT: $BRIDGE_SSE"; exit 1; }
printf '%s\n' "$BRIDGE_SSE" | head -1 | jq -e '.method == "notifications/progress" and .params.progressToken == 1' >/dev/null \
    || { echo "FAIL: first bridge line should be a progress notification"; exit 1; }
printf '%s\n' "$BRIDGE_SSE" | tail -1 | jq -e '.id == 100 and has("result")' >/dev/null \
    || { echo "FAIL: last bridge line should be the id=100 result"; exit 1; }

echo
echo "MCP smoke test PASSED"
