# Disassembler quality check

`disasmcheck.py` checks the emulator's Z80 disassembler the way the agents use it: `GET /disasm` on the WebAPI,
`debug_code disassemble` and `inspect_state disasm` on MCP. It runs against a live application and creates its own
emulator instances (removed at the end).

| Check | What |
|-------|------|
| `length` | each of the 1780 Z80 encodings (unprefixed, CB, ED, DD, FD, DD CB, FD CB; undocumented included) has the size z88dk-dis gives |
| `text` | the mnemonic equals z88dk-dis' once spelling is normalized (`#34` / `$34` / `34h`, `hx` = `ixh`, `add a,b` = `add b`, `rlc (ix+d),b` = `ld b,rlc (ix+d)`, the `*` undocumented mark); the remaining synonyms are listed in `SYNONYMS` |
| `target` | jr / djnz / jp / call / rst carry `target` with the right address |
| `syntax` | no `??` or comments in a mnemonic |
| `reasm` | sjasmplus assembles the mnemonic back to the same bytes: a failure for a documented opcode, a note for an undocumented one |
| `rom` | a linear sweep of the 48K ROM with its symbols: the same boundaries and text as z88dk-dis (`jp START-NEW (#11CB)` read as `jp #11CB`), `bytes` equal to memory |
| `wire` | `address` as decimal, `0x`, `$`, `#` or `h`; a bad address or count is a 400; `count=256` gives 256 lines |
| `mcp` | the `debug_code disassemble` listing is in `content[0].text` and matches `structuredContent`; `inspect_state disasm` shows it too; with `response_format: "json"` a structured result is echoed in `content[]` |

## Run

Run the build under test on ports of its own when other sessions use the defaults:

```bash
PORT=8095
UNREAL_WEBAPI_PORT=$PORT UNREAL_CLI_PORT=$((PORT+100)) UNREAL_MCP_PORT=$((PORT+200)) UNREAL_GDB_PORT=$((PORT+300)) \
UNREAL_DEZOG_PORT=$((PORT+400)) UNREAL_ZRCP_PORT=$((PORT+500)) \
  cmake-build-agent-release/bin/unreal-qt.app/Contents/MacOS/unreal-qt -platform offscreen &

tools/verification/disasm/disasmcheck.py --port $PORT --mcp-port $((PORT+200)) \
  --z88dk-dis <z88dk>/bin/z88dk-dis --sjasmplus <sjasmplus>
```

`--z88dk-dis` (or `UNREAL_Z88DK_DIS`) is required; without `--sjasmplus` (or `UNREAL_ASM_SJASMPLUS`) the `reasm` check
is skipped; `--mcp-port 0` skips the MCP checks. `--verbose` lists every failure and note. Exit code 0 = every hard
check passed.

Result on 2026-10-10: every check passes; 645 undocumented encodings do not reassemble with sjasmplus (the `*` mark,
the DD CB register copies, the BIT mirrors), which is expected.
