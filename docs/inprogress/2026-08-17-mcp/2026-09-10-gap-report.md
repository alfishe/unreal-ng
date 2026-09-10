# MCP Implementation Gap Report

> **Date**: 2026-09-10  
> **Status**: Phase 1 + Phase 2 complete; Phase 3 deferred  
> **Server**: `POST http://localhost:8092/mcp` (Streamable HTTP, JSON-RPC 2.0)

---

## Executive Summary

The MCP server implementation covers all Phase 1 (MVP) and Phase 2 (Parity) goals. 11 tools and 6 resources are operational. Phase 3 "superiority" features remain unimplemented.

---

## Implementation Status

### Tools (11/11 Planned)

| Tool | Status | Notes |
|:-----|:------:|:------|
| `emulator_manage` | ✅ | create/list/list_models/status/start/stop/pause/resume/reset/destroy |
| `load_software` | ✅ | Auto-detect .sna/.z80/.tap/.tzx/.trd/.scl |
| `control_execution` | ✅ | run/pause/step/step_n/step_over/step_out/run_frame/breakpoints |
| `inspect_state` | ✅ | registers/memory/disasm/stack/screen_ocr/beam/digest/rom |
| `type_input` | ✅ | type/tap/press/release/combo/macro/release_all |
| `search_api` | ✅ | Keyword search with auto_invoke on single match |
| `invoke_api` | ✅ | Execute any WebAPI endpoint with {id} substitution |
| `manage_symbols` | ✅ | load_labels/load_listing/list/resolve/source_at/step_line/run_to_line |
| `debug_code` | ✅ | disassemble/assemble/find_bytes/trace |
| `analyze_performance` | ✅ | coverage_*/frame_cost/profile_*/porttrace |
| `capture_media` | ✅ | screenshot/screen_digest/record_*/audio_* |

### Resources (6/6 Planned)

| Resource | Status |
|:---------|:------:|
| `unreal://keyboard-layout` | ✅ |
| `unreal://basic-reference` | ✅ |
| `unreal://z80-isa` | ✅ |
| `unreal://trdos-commands` | ✅ |
| `unreal://memory-map` | ✅ |
| `unreal://emulator-state` | ✅ (dynamic) |

---

## Phase 3 Gaps (Unimplemented)

### High-Performance Bridge

| Feature | Priority | Effort | Description |
|:--------|:--------:|:------:|:------------|
| gRPC/Protobuf transport | Medium | High | Replace HTTP with binary IPC in `unreal-mcp-bridge` |
| Shared memory for bulk data | Low | Medium | Zero-copy screen/memory extraction |

### Video Recording

| Feature | Priority | Effort | Description |
|:--------|:--------:|:------:|:------------|
| MP4 recording | Medium | High | Requires ffmpeg/libx264 integration |
| WebM recording | Low | High | Requires libvpx integration |

*Current state: GIF recording via gif-h (header-only) is working.*

### Screen Analysis

| Feature | Priority | Effort | Description |
|:--------|:--------:|:------:|:------------|
| xxHash digest | Low | Low | FNV-1a 64-bit fallback is shipped and working |
| Semantic frame diffing | Low | High | Block-matching sprite detection with motion vectors |

### Audio Analysis

| Feature | Priority | Effort | Description |
|:--------|:--------:|:------:|:------------|
| FFT frequency analysis | Low | Medium | Currently: RMS/peak/dominant-Hz only |

### Symbol Management

| Feature | Priority | Effort | Description |
|:--------|:--------:|:------:|:------------|
| Auto-reloading symbol tables | Medium | Medium | File watcher for .map/.sym hot-reload on recompile |
| Fuzzy symbol matching | Low | Low | Regex pattern search (`player_*_init`) |
| Batch address resolution | Low | Low | Resolve 100 addresses in one call |

### Breakpoint Enhancements

| Feature | Priority | Effort | Description |
|:--------|:--------:|:------:|:------------|
| Conditional breakpoints | Low | Medium | Expression-based conditions (`if HL == 0`) |

*Note: Memory watchpoints and port breakpoints are already implemented via `bp_add` with type `read`/`write`/`port_in`/`port_out`.*

---

## Verified Working (Originally Listed as Gaps)

These features were identified as gaps in the capability docs but are now implemented:

| Feature | Tool | Endpoint |
|:--------|:-----|:---------|
| `set_register` | `invoke_api` | `PUT /api/v1/emulator/{id}/registers/{name}` |
| `beam_position` | `inspect_state` (beam aspect) | `GET /api/v1/emulator/{id}/video/beam` |
| `find_bytes` | `debug_code` | Pattern search action |
| `step_out` | `control_execution` | SP-tracking implementation |
| Port breakpoints | `control_execution` | `bp_add` with `type: "port_in"` or `type: "port_out"` |
| Memory watchpoints | `control_execution` | `bp_add` with `type: "read"` or `type: "write"` |
| ROM signatures | `inspect_state` (rom aspect) | `GET /api/v1/emulator/{id}/state/memory/rom` |

---

## Recommendations

### Quick Wins (Low Effort, High Value)

1. **Batch address resolution** — trivial loop in `manage_symbols`
2. **Fuzzy symbol matching** — regex filter over existing label list
3. **Conditional breakpoints** — expression evaluator + register context

### Medium-Term

4. **Memory watchpoints** — requires DebugManager extension for read/write traps
5. **Auto-reload symbols** — QFileSystemWatcher integration in AutomationMCP
6. **FFT audio** — integrate kissfft (header-only) into audio capture

### Long-Term

7. **gRPC transport** — architectural change, defer until HTTP bottleneck proven
8. **MP4/WebM** — ffmpeg subprocess or libav linkage, significant build complexity
9. **Semantic diffing** — research project, low practical demand

---

## Test Coverage

| Component | Tests |
|:----------|------:|
| MCP dispatcher | 24 |
| MCP router | 18 |
| MCP tools | 31 |
| Z80TextAssembler | 12 |
| ListingParser | 8 |
| CoverageAnalyzer | 4 |
| **Total new** | **97** |

---

## Appendix: Live Verification

```bash
# Initialize session
curl -X POST http://localhost:8092/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{
    "protocolVersion":"2024-11-05",
    "capabilities":{},
    "clientInfo":{"name":"test","version":"1.0"}
  }}'

# List tools
curl -X POST http://localhost:8092/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'

# Get emulator state
curl -X POST http://localhost:8092/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{
    "name":"inspect_state",
    "arguments":{"category":"cpu"}
  }}'
```

All endpoints verified working as of 2026-09-10.
