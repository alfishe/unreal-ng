# DONE — BASIC injection & OCR (2026-01-17)

**Status:** complete.

## What landed
- BASIC line-editor injection (based on the 128K line-editor analysis), the BASIC
  encoder/tokenizer, ROM print detection, and on-screen OCR — per the plans and
  walkthrough in this folder.

## Evidence
- `core/automation/webapi/src/api/basic_api.cpp` and `interpreter_api.cpp`;
  `inspect_state` `screen_ocr` aspect; keyboard `type/tap/macro` on all surfaces
  (verified 2026-09-10 MCP gap report).

## Follow-ups
- None. (Folder name predates the kebab-case rule; left as-is.)
