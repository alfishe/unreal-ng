# Recipe: Manual TR-DOS — Select and Run a Specific Program

Goal: with a disk mounted (no autostart), drive TR-DOS yourself and run an
exact `<something>.B` file. Use this when the disk has several programs, or
when you want to observe the TR-DOS boot path itself.

Prerequisite: disk mounted per [insert-disk.md](../media/insert-disk.md),
TR-DOS-capable model (`128k`/`PENTAGON`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `type_input`
> types, `invoke_api` drives `basic/run` and the catalog. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

The whole flow (`NAME` = catalog name, trailing padding stripped):

```text
# Step 1: Navigate Pentagon menu to TR-DOS (Option 5: Down x4 + Enter):
type_input         {"action":"tap","key":"Down"}  # repeat x4
type_input         {"action":"tap","key":"Enter"}
control_execution  {"action":"run_frames","frames":100}

# Step 2: At TR-DOS prompt (A>), tap 'R' (types RUN) then 'Enter':
type_input         {"action":"tap","key":"R"}
type_input         {"action":"tap","key":"Enter"}
control_execution  {"action":"run_frames","frames":200}
inspect_state      {"aspects":["registers","screen_ocr","fdc"]}
```

`basic/run` and the catalog have no smart tool — they ride `invoke_api`.
Send BASIC lines and TR-DOS prompt commands alike through `basic/run`: the
`A>` prompt is the 48K editor, where `CAT`, `LIST`, `RUN` are keyword keys
(raw `type_input` would type `C` as CONTINUE). `type_input` with `tokenized`
types the same way but without ENTER. The command crib and `.C`-block notes below apply to both
transports.

## WebAPI

Curl form — right choice when a Python/bash pipeline drives these steps.

### Step 1 — Pick the file from the catalog (host side, no emulation)

```bash
curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" | jq -r '.files[] | select(.type=="B") | .name'
```

Names are 8 chars space-padded — normalize once and keep it:

```bash
NAME=$(curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" \
       | jq -r '.files[] | select(.type=="B") | .name' | head -1 | sed 's/ *$//')
echo "will run: $NAME"
```

`.B` = BASIC (run directly). `.C` = code block — run with
`RANDOMIZE USR <start>` using the `start` field from the catalog entry.

### Step 2 — Enter TR-DOS

Option A — **Pentagon 128 Main Menu Navigation** (direct, 1-shot):
On fresh `PENTAGON` / `128k` instances showing the 128K Sinclair menu:
```bash
# Tap Down x4, then Enter to select option 5 ("TR-DOS")
for key in Down Down Down Down Enter; do
  curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" \
       -H 'Content-Type: application/json' -d "{\"key\": \"$key\"}" >/dev/null
  curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
       -H 'Content-Type: application/json' -d '{"frames": 15}' >/dev/null
done
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" -H 'Content-Type: application/json' -d '{"frames": 100}' >/dev/null
```
Verify OCR shows `* TR-DOS Ver 5.04T*` and the `A>` prompt.

Option B — **Via 48K BASIC** (`RANDOMIZE USR 15616`):
Select `48 BASIC` from the main menu (tap `Down` x3 + `Enter`, or tap `4`), then run `RANDOMIZE USR 15616`. Note: `basic/run` called at the 128K menu enters `128 BASIC` and types there.

### Step 3 — Run the chosen file / boot disk

At the TR-DOS prompt (`A>`), the system is in **Keyword Mode** (`K` cursor):
- **Boot default program / `boot.B`**: A single tap of key `R` types `RUN ` automatically!
```bash
# Tap 'R' then 'Enter' at the clean A> prompt
for key in R Enter; do
  curl -s -X POST "$BASE/emulator/$EMU_ID/keyboard/tap" \
       -H 'Content-Type: application/json' -d "{\"key\": \"$key\"}" >/dev/null
  curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
       -H 'Content-Type: application/json' -d '{"frames": 20}' >/dev/null
done
```
- **Run a specific named file**: Tap `R`, tap `"`, type `<NAME>`, tap `"`, tap `Enter`. (Avoid sending raw `"RUN"` string via `keyboard/type` at `A>` as `R` already expands to `RUN `).

### Step 4 — Verify the program is up

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
     -H 'Content-Type: application/json' -d '{"frames": 200}' >/dev/null
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r '.text'
curl -s "$BASE/emulator/$EMU_ID/registers"     | jq '.registers.pc'   # left BASIC editor?
```

The PC leaving the BASIC editor range (roughly `$12A2` interpreter loop /
`$0D00`-area editor) plus a stable screen digest is strong evidence the
program is executing.

### Code blocks (.C files)

```bash
START=$(curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" \
        | jq '.files[] | select(.type=="C") | .start')
# then at the TR-DOS prompt:
#   LOAD *"name" CODE
# followed by:
curl -s -X POST "$BASE/emulator/$EMU_ID/basic/run" \
     -H 'Content-Type: application/json' \
     -d '{"command": "RANDOMIZE USR '"$START"'"}' | jq .
```

## Pitfalls

- **`basic/run` at the `A>` prompt** types the line through the 48K editor
  TR-DOS uses and reports what TR-DOS did: `outcome: "trdos_command"` once
  TR-DOS found the command, `failure: "trdos_rejected"` with `err_nr`
  otherwise. TR-DOS switches the drive on before it looks a command up, so
  with no disk every command ends in `err_nr` 26 — insert the disk first.
- **Names with quotes/embedded spaces**: catalog names cannot contain `"`;
  padding spaces must be stripped or TR-DOS won't match the file.
- **Typing races**: BASIC lines through `basic/run` or `keyboard/type`
  with `tokenized: true` are paced by the ROM itself (each key waits until
  the editor has taken it) and the reply comes when the result is known.
  Only raw `keyboard/type` still queues keys with `delay_frames` per key.
- **128K editor menu**: fresh `128k`/`PENTAGON`/`SCORPION` boots into the menu.
  `basic/run` leaves it for `128 BASIC` (every key verified) and types there;
  the 128K editor takes keywords spelled out and tokenises them at ENTER. On Pentagon machines, selecting option 5 (`TR-DOS` via 4x `Down` + `Enter`)
  enters `A>` directly; alternatively, select option 4 (`48 BASIC`) before running `RANDOMIZE USR 15616`.
- **Reset vs page-in**: `RANDOMIZE USR 15616` enters TR-DOS at `$3D00`
  (prompt). `15619` + `REM:` enters at `$3D03` and executes the trailing
  command — the two are not interchangeable.
