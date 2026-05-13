# RAM Disk Build Verification

High-speed native build verification using RAM disk for maximum I/O performance.

## Quick Start

```bash
# Full build cycle (creates RAM disk, builds, cleans up)
./run_all.sh

# Keep RAM disk after build (for inspection)
./run_all.sh --keep

# Custom RAM size (8GB)
./run_all.sh 8
```

## Scripts

| Script | Purpose |
|--------|---------|
| `run_all.sh` | Orchestrator - runs complete build cycle |
| `create_ram_disk.sh` | Creates/mounts RAM disk (macOS APFS, Linux tmpfs) |
| `copy_to_ram.sh` | Syncs project to RAM disk |
| `build_on_ram.sh` | Builds all components on RAM disk |
| `cleanup_ram_disk.sh` | Unmounts RAM disk or wipes contents |
| `common.sh` | Shared utilities and configuration |

## Build Targets

1. **core** - Core library with tests/benchmarks enabled
2. **automation** - Automation plugin
3. **core-tests** - Unit tests
4. **core-benchmarks** - Performance benchmarks
5. **testclient** - CLI test client
6. **unreal-qt** - Main Qt application

## Requirements

- **macOS**: 4GB+ free RAM (default)
- **Linux**: 4GB+ free RAM, sudo for tmpfs mount
- **CMake** 3.20+
- **rsync**

## Logs

Build logs saved to: `<RAM_DISK>/unreal-ng/build-logs/`

On failure, RAM disk is kept mounted for investigation.

## Performance

Typical native build times (Apple M2, 4GB RAM disk):
- Full cycle: ~3-4 minutes
- unreal-qt only: ~1.5 minutes
