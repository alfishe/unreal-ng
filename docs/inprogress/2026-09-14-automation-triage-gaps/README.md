# Automation Triage Gaps — New Machine Configurations & Peripherals

**Date:** 2026-09-14
**Analysis target:** MCP + WebAPI surface vs. triage needs for newly developed
machine configurations and peripherals:
ATM Turbo (ports, screen modes), Profi, ZX Evo (TSConf), MoonSound, and the
Kempston Mouse across different configs.
**Baseline:** `master` branch, working tree as of 2026-09-14, cross-referenced
with the `atm` branch (`git diff master...atm`).
**Trigger:** the 2026-09-10 ATM video-mode debugging session
(`docs/inprogress/2026-09-10-atm-debugging/bug-report.md`) documented six bugs,
three of which are automation-surface gaps that remain open today.

## Files

| File | Content |
|:--|:--|
| [current-state.md](current-state.md) | Inventory of what exists today: WebAPI endpoint groups, MCP tools/resources, runtime features, machine model table, peripheral implementation status, branch topology. |
| [gap-analysis.md](gap-analysis.md) | The 18 findings, grouped A–F by triage workflow stage, each with evidence (`file:line`, verified on master 2026-09-14) and impact. |
| [recommendations.md](recommendations.md) | Prioritized remediation plan (P0–P3) with concrete endpoint/tool proposals, response schemas, and acceptance criteria. |
| [triage-workflows.md](triage-workflows.md) | Before/after walkthroughs for the concrete triage scenarios named in the analysis (ATM black screen, mouse-on-configs, MoonSound bring-up). |

## Executive summary

The emulator core is ahead of the automation layer. The core already knows about
ATM video modes (`VideoModeEnum`), ATM/Profi paging registers (`pFF77`, `aFE`,
`p1FFD`, `pDFFD`), per-model port decoders, and a mouse input funnel with TTD
journalling — but the WebAPI/MCP surface that agents use to *triage* was built
around 48K/128K semantics and reports none of this. The `atm` branch adds
~7.6k lines of machine bring-up work (port decoders, configs, video-mode test
suites) with **zero** changes under `core/automation/`.

The five highest-impact gaps:

1. **No machine identity.** `GET /emulator/{id}` — the source for the MCP
   `machine` aspect — returns no model, RAM size, video mode, or peripherals.
2. **Silent 48K fallback on create.** Requesting a model that cannot be
   initialized (every ATM/ZX-Evo/Profi request on master) returns `201` with a
   default 48K machine and no indication of the substitution.
3. **Screen state APIs are hardcoded to standard mode.** `video_mode` is the
   literal string `"standard"`; the ATM/Profi/TSConf mode machinery in the core
   is invisible (`/video/beam` is the only exception).
4. **No port-decode introspection.** Porttrace decode rules exist only for
   Pentagon128; there is no endpoint that answers "which devices respond to
   which ports on this machine right now" — the central question for ATM port
   and Kempston-mouse-routing triage.
5. **Peripheral observability is 2 of N.** The `DeviceState` report pattern
   exists (FM, FDC) but GS/Covox are placeholders, MoonSound has no surface and
   its design has no automation section at all.

Remediation is small relative to impact: the P0 items are field additions and
error-path fixes in existing handlers; the P1 items are reporting endpoints
over state the core already holds. Details and schemas in
[recommendations.md](recommendations.md).

## Scope and non-goals

**In scope:** the automation surface only (WebAPI REST, MCP tools/resources,
and the agent-facing documentation that feeds them). Core emulation
correctness (e.g. the ATM16 renderer stub) is referenced where it constrains
the API but is tracked by the `atm` branch work, not proposed here.

**Non-goals:** GUI (unreal-qt) debugger surface; CLI/Lua/Python parity (the
WebAPI is the single source of truth that MCP fans out to — fixing it first
fixes MCP); performance work.

## Verification method

All `file:line` citations were verified on the master working tree on
2026-09-14 with full-file reads and `rg` searches; branch-state claims come
from `git diff --stat master...atm`. Where a fact could not be re-verified
(e.g. runtime behavior of the `atm` branch), the document says so explicitly.
