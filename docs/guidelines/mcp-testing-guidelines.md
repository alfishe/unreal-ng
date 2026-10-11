# MCP testing guidelines

The MCP server (`core/automation/mcp/`) is what agents use to drive the emulator: claude.ai connectors, Claude Code,
other MCP clients. A change there is right when an agent gets the right answer **from what it reads**. These rules
say what to test, with what, and when.

## 1. What an agent reads

- A tool answers with `content[]` (text) and `structuredContent` (JSON). Some clients (claude.ai connectors) hand the
  model **only `content[]`**. A value that is only in `structuredContent` is invisible there.
- So the default text (`response_format: "text"`) carries the data, not a headline: the bytes of a read, the listing,
  one line per breakpoint / label / trace event, `key=value` fields of a small result (`FlatFields`, `AppendFields`).
  `response_format: "json"` adds the whole `structuredContent` as JSON text (cut at 64 KB) for the rest.
- A large result gets filters and a line limit with a note on how to narrow it (`manage_symbols list`: `name`, `from`,
  `to`, `limit`), not a 300 KB JSON that the echo cuts in half.
- Pictures go as an MCP `image` content block (`capture_media screenshot` with `include_image`), never as base64 text.
- Errors are tool results with `isError: true` and a text that names the argument and the fix
  (`"press requires 'buttons'"`). A JSON-RPC error is only for an unknown tool or method.
- A trap the agent cannot see goes into the tool description or is removed (`bp_add` switches the breakpoints feature
  on; `run_frames` skipping breakpoints is in the `control_execution` description).

## 2. Three layers

| Layer | Where | Runs | Answers |
|-------|-------|------|---------|
| Unit | `core/tests/automation/mcp-*-test.cpp` (`FakeApiCaller` routes, `RunTool`, `DispatchSync`) | `tools/build/test.sh --gtest_filter='*Mcp*'`, no emulator | the tool builds the right WebAPI calls, the text and `structuredContent` it makes from a known body, errors before any call |
| Live | [`tools/verification/mcp/`](../../tools/verification/mcp/README.md), one script per block of tools | `run_all.py --launch <unreal-qt>`, ~5 s | the real server end to end: the data in the text, round trips (write / read, assemble / disassemble), breakpoints firing, images, the text-coverage floors |
| Quality | [`tools/verification/disasm/`](../../tools/verification/disasm/README.md) (z88dk, sjasmplus) | against a live server | the disassembler's output against reference tools |

A unit test pins the format of one answer; the live scripts catch what only the real server shows (a feature that is
off, a body shape the fake did not have, a crash). Use both for a change of behavior.

## 3. When to run what

| Change | Run |
|--------|-----|
| A tool's text, arguments, schema or description | unit tests of the tool + its live script (`run_all.py … test_<block>`) |
| The dispatcher, `ToolResult`, `mcp-tool-utils.h`, the protocol | all `*Mcp*` unit tests + the whole live set |
| A WebAPI endpoint an MCP tool forwards to | the live script of that tool (the fake routes do not see the real body) |
| The disassembler, `GET /disasm`, `debug_code disassemble` | + `tools/verification/disasm/disasmcheck.py` |
| A new tool or action | a unit test with the body it reads, a test in the block's live script, a `test_text_coverage.py` row when it returns data |
| Before landing any of the above | `tools/build/build.sh` (zero warnings), `*Mcp*` unit tests, `run_all.py --launch …`, gcc:16 for changed C++ ([cross-platform](cross-platform-compatibility.md)) |

## 4. How

- **Your own instance.** `run_all.py --launch cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt`
  starts `unreal-qt -platform offscreen` on ports of its own (`--port-base`, default 8095: MCP 8295) and stops it.
  The scripts destroy every instance on the server they talk to: never point them at an emulator someone works in.
  Port variables: [environment-variables.md](../emulator/environment-variables.md).
- **The fresh binary.** `tools/build/build.sh` before a live run: a stale `unreal-qt` answers with yesterday's text.
- **Check the text first** (`r.text`), then exact values in `r.structured`. An assertion only on `structuredContent`
  passes while the model sees nothing.
- **Independent of the past.** Write the code the test runs and set PC (`memory_access write`, `invoke_api PUT
  /registers/pc`), wait for an event with `resume` + `wait`, not `run_frames` + hope.
- **Measure a text change.** `test_text_coverage.py` counts the share of `structuredContent` values the text carries.
  Before filling in a tool's text, measure the call (share, characters, calls until the model has the data) and
  measure again after; keep the change when it wins: `key=value` for a 3.5 KB list of models made it dearer and was
  dropped.
- **A crash or hang of the server** is a bug of its own: keep the log, reproduce under ASan
  ([sanitizers](../testing/sanitizers.md)), do not soften the test.

## 5. Writing a live test

`tools/verification/mcp/mcplib.py`: `Session.call(tool, args)` (a tool error fails the test; `ok=False` allows it),
`Result.text / structured / blocks('image') / is_error`, `expect(condition, what, details)`. A script is `test_*`
functions plus an optional `setup(s)`; `main(globals())` runs it. Details and an example:
[tools/verification/mcp/README.md](../../tools/verification/mcp/README.md).
