# unreal-mcp-bridge

Stdio ↔ HTTP adapter that lets MCP-capable IDEs and agents talk to the
Unreal-NG MCP server without speaking Streamable HTTP themselves.

```
MCP client (IDE)                unreal-mcp-bridge                       drogon (shared app)
────────────── stdio: lines ──▶ pure pipe, one POST per line ──▶ POST http://…:8092/mcp
◀── stdout: lines ◀──────────── response (JSON) or per-event lines ◀── application/json
                                                              ◀── text/event-stream (SSE)
```

## Protocol behavior

- Reads newline-delimited JSON-RPC 2.0 from stdin. **No JSON parsing** — the
  bridge is a byte pipe; malformed lines are forwarded verbatim and answered
  by the server's `-32700` parse error.
- One new TCP connection per request, no state kept. The request advertises
  `Accept: application/json, text/event-stream` and `keep-alive`; response
  completion is detected from the wire framing (terminal chunk for SSE
  streams, `Content-Length` otherwise), then the socket is discarded.
- **SSE translation**: when the server answers `text/event-stream` (which it
  does for `tools/call` requests carrying `_meta.progressToken`), every
  `data:` payload becomes exactly one stdout line — `notifications/progress`
  objects arrive before the final JSON-RPC response, in server order.
  `event:`/`id:` lines and `: keepalive` comment frames are dropped. Still no
  JSON parsing — line-level extraction only.
- Writes the response body + `\n` to stdout **only when non-empty**
  (notifications → HTTP 202 → silence, keeping the line protocol clean).
- Connect/transport failure emits
  `{"jsonrpc":"2.0","id":null,"error":{"code":-32603,"message":"bridge: cannot reach <url> (…)"}}`
  and keeps running — start the emulator later and retry.
- stdin EOF → exit 0. `SIGPIPE` ignored (POSIX). Winsock guarded (Windows).
- Response bodies: `Content-Length` and `Transfer-Encoding: chunked`
  handling; 5-minute receive ceiling so a wedged server cannot hang the daemon.

## Usage

```bash
# default URL http://127.0.0.1:8092/mcp
./cmake-build-release/bin/unreal-mcp-bridge

# explicit endpoint
./cmake-build-release/bin/unreal-mcp-bridge --url http://127.0.0.1:8092/mcp

# or via environment
UNREAL_MCP_URL=http://127.0.0.1:8092/mcp ./cmake-build-release/bin/unreal-mcp-bridge
```

Manual round-trip test:

```bash
echo '{"jsonrpc":"2.0","id":1,"method":"ping"}' | ./cmake-build-release/bin/unreal-mcp-bridge
# → {"id":1,"jsonrpc":"2.0","result":{}}

# SSE path: a progressToken request streams notifications, then the result —
# each as its own stdout line
echo '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"inspect_state","arguments":{"aspects":["registers"]},"_meta":{"progressToken":1}}}' \
  | ./cmake-build-release/bin/unreal-mcp-bridge
# → {"jsonrpc":"2.0","method":"notifications/progress","params":{"progress":1.0,"progressToken":1,...}}
# → {"id":2,"jsonrpc":"2.0","result":{...}}
```

## IDE registration (repo-root `.mcp.json`)

```json
{
  "mcpServers": {
    "unreal-ng": {
      "command": "cmake-build-release/bin/unreal-mcp-bridge"
    }
  }
}
```

The emulator application must be running (it serves `/mcp` on :8092 — and,
harmlessly, also on the WebAPI port :8090 because both listeners share the
same drogon app instance).

## Dependencies

None beyond C++20 and the OS socket layer (`ws2_32` on Windows). No drogon,
no jsoncpp — the bridge must stay usable in sandboxed tool environments.
