# BASIC Language Analyzer Verification Scripts

This folder contains verification scripts for the BASIC language analyzer and command injection functionality.

## Overview

These scripts test the ZX Spectrum BASIC command injection via the UnrealSpeccy emulator's WebAPI. They verify that commands can be injected into the BASIC editor buffer and appear correctly on screen.

## Scripts

### verify_48k_injection.py

Tests 48K BASIC command injection:
- Switches Pentagon from 128K menu to 48K BASIC mode
- Injects **tokenized** commands into the E_LINE buffer
- Verifies command appears on screen via OCR

```bash
python3 verify_48k_injection.py "PRINT 42"
```

### verify_128k_injection.py

Tests 128K BASIC command injection:
- Switches Pentagon from 128K menu to 128K BASIC editor
- Injects **plain ASCII** commands (128K editor is NOT tokenized)
- Verifies command appears on screen via OCR

```bash
python3 verify_128k_injection.py "PRINT 42"
```

## Key Differences: 48K vs 128K

| Feature | 48K BASIC | 128K BASIC |
|---------|-----------|------------|
| Buffer | E_LINE ($5C59) | Screen Line Edit Buffer ($EC16) |
| Tokenization | Pre-tokenized (PRINT → $F5) | Plain ASCII (P-R-I-N-T) |
| ROM | SOS ROM (page 3) | Editor ROM (page 2) |
| API State | `basic48k` | `basic128k` |

## Requirements

- UnrealSpeccy emulator running with `--webapi` flag
- Python 3.x
- requests library: `pip install requests`

## API Endpoints Used

- `GET /api/v1/emulator` - List emulator instances
- `POST /api/v1/emulator/start` - Create and start new instance
- `POST /api/v1/emulator/{id}/reset` - Reset to 128K menu
- `POST /api/v1/emulator/{id}/basic/mode` - Switch mode (48k/128k)
- `POST /api/v1/emulator/{id}/basic/inject` - Inject command
- `GET /api/v1/emulator/{id}/capture/ocr` - Screen OCR

## Exit Codes

- `0` - Test passed
- `1` - Test failed
