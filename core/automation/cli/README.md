# CLI Automation Module

Command-line interface for controlling the emulator via TCP socket. Provides a scriptable, thread-safe interface for automation and debugging.

## Connection

```bash
# Default port: 8765
telnet localhost 8765
nc localhost 8765
```

## Command Reference

### Lifecycle
| Command | Description |
|---------|-------------|
| `list` | List all emulator instances |
| `create [model]` | Create new instance |
| `start` | Start selected instance |
| `stop` / `remove` | Destroy instance |
| `select <uuid>` | Set active instance |
| `status` | Show all instances status |
| `models` | List supported models |
| `zxpoly start <model> [file]` | Start a ZX-Poly machine: four synchronized instances (`model`: `ZXPOLY-48K`, `ZXPOLY-128K`, `ZXPOLY-PENTAGON` or a base model; `file`: `.zxp`, `.prom` or multiloader disk) |
| `zxpoly status [id]` | ZX-Poly group status: modules, R0-R3, `#3D00`, lock, video mode, lockstep check |

### Execution Control
| Command | Description |
|---------|-------------|
| `pause` | Pause emulation |
| `resume` | Resume emulation |
| `reset` | Hardware reset |
| `step` / `stepin` | Execute one instruction |
| `stepover` | Step over CALL/RST |
| `steps <N>` | Execute N instructions |
| `run_frame` | Run one video frame |
| `run_frames <N>` | Run N video frames |
| `run_tstates <N>` | Run N T-states |
| `run_to_scanline <N>` | Run until scanline N |
| `run_scanlines <N>` | Run N scanlines |
| `run_to_pixel` | Run to next screen pixel |
| `run_to_interrupt` | Run until next interrupt |

### Registers
| Command | Description |
|---------|-------------|
| `registers` | Show all Z80 registers |
| `reg <name>` | Get single register |
| `reg get <name>` | Get single register (alias) |
| `reg <name> <value>` | Set register |
| `reg set <name> <value>` | Set register (alias) |

### Memory
| Command | Description |
|---------|-------------|
| `memory <addr> [len]` | Hex dump memory |
| `state memory` | Bank mapping info |

### Breakpoints
| Command | Description |
|---------|-------------|
| `bp <addr>` | Set execution breakpoint |
| `wp <addr> <r\|w\|rw>` | Set memory watchpoint |
| `bport <port> <in\|out>` | Set I/O breakpoint |
| `bplist` | List all breakpoints |
| `bpclear [id\|all]` | Remove breakpoints |
| `bpon` / `bpoff` | Toggle breakpoints |

### Disassembly
| Command | Description |
|---------|-------------|
| `disasm [addr] [count]` | Disassemble from address |
| `disasm_page <ram\|rom> <page> [offset] [count]` | Disassemble physical page |

### Audio
| Command | Description |
|---------|-------------|
| `state audio gs [--verbose]` | General Sound card state: mailbox flags, MPAG page, DAC channels; `--verbose` adds the coprocessor registers |

### Capture
| Command | Description |
|---------|-------------|
| `capture screen [--area=full\|screen] [--format=png\|gif] [file]` | Screenshot of the presented frame of the session's emulator. `--area=full` (default): the whole frame with its border; `screen`: the working picture (the paper of a Spectrum, the graphics window of a TS-Conf, the whole FT812 picture). PNG (default) or GIF. Prints the geometry and a data URI, or saves to `file`. |
| `capture framebuffer <file> [rgba\|index]` | Raw pixels of the same frame |
| `capture ocr` | OCR text from the screen |

### Media
| Command | Description |
|---------|-------------|
| `open <file>` | Auto-detect and load file |
| `snapshot save <file>` | Save snapshot |
| `tape load/eject/play/stop` | Tape control |
| `disk insert/eject/catalog` | Disk control |
| `media list/info/insert/swap/eject/save/export/discard/rescan/create/protect` | Every slot (floppy drives, SD card, ...): see [docs/features/media.md](../../../docs/features/media.md); `media help` lists the options |

### Features
| Command | Description |
|---------|-------------|
| `feature list` | List toggleable features |
| `feature <name> on/off` | Toggle feature |
| `debugmode on/off` | Enable/disable debug mode |

## Building

Enabled by default with `ENABLE_AUTOMATION=ON`. Disable with `ENABLE_CLI_AUTOMATION=OFF`.
