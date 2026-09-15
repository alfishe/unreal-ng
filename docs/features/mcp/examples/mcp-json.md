# Root `.mcp.json` reference

Claude Code picks up a `.mcp.json` at the repository root automatically —
every session in this repo then has the `unreal-ng` tools available as
`mcp__unreal-ng__*` (verify with `/mcp` inside a session).

This repository ships a minimal one:

```json
{
  "mcpServers": {
    "unreal-ng": {
      "command": "cmake-build-release/bin/unreal-mcp-bridge"
    }
  }
}
```

## Fields

| Field | Meaning |
|:--|:--|
| `mcpServers.<name>` | server alias — becomes the tool prefix (`unreal-ng:*`) |
| `command` | path to the bridge binary; relative paths resolve against the repo root (the cwd when Claude Code launches the server) |
| `args` | optional argv — the bridge needs none |
| `env` | optional environment; `UNREAL_MCP_URL` overrides the default `http://127.0.0.1:8092/mcp` |

## Variations

Explicit endpoint and arguments:

```json
{
  "mcpServers": {
    "unreal-ng": {
      "command": "cmake-build-release/bin/unreal-mcp-bridge",
      "args": ["--url", "http://127.0.0.1:8092/mcp"],
      "env": {}
    }
  }
}
```

Absolute path (needed when the client resolves relative paths against
something other than the repo root, e.g. some sandboxed tool environments):

```json
{
  "mcpServers": {
    "unreal-ng": {
      "command": "/absolute/path/to/unreal/cmake-build-release/bin/unreal-mcp-bridge"
    }
  }
}
```

Windows — double every backslash, use the `.exe`:

```json
{
  "mcpServers": {
    "unreal-ng": {
      "command": "C:\\dev\\unreal\\cmake-build-release\\bin\\unreal-mcp-bridge.exe"
    }
  }
}
```

Direct HTTP instead of the bridge (Claude Code supports Streamable HTTP
servers; no subprocess involved):

```json
{
  "mcpServers": {
    "unreal-ng": {
      "type": "http",
      "url": "http://localhost:8092/mcp"
    }
  }
}
```

## Notes

- Other clients use sibling files with the same `mcpServers` shape:
  `.cursor/mcp.json` (Cursor), `.vscode/mcp.json` (VS Code — `servers` key
  instead, see the [integration guide](../integration-guide.md)).
- The bridge starts per client process and exits with stdin EOF — no daemon
  to manage. If the emulator isn't running yet, tool calls answer with a
  "WebAPI unreachable" hint instead of hanging; start the emulator and retry.
- Long tool calls stream `notifications/progress` as separate stdout lines —
  stdio clients get progress without any SSE handling.
