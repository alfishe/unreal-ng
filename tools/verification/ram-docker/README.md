# Docker + RAM Disk Build Verification

Cross-platform build verification using Docker container with RAM disk mount for maximum performance.

## Quick Start

```bash
# Full build cycle
./run_all.sh

# With options
./run_all.sh --keep           # Keep RAM disk mounted
./run_all.sh --rebuild-image  # Force rebuild Docker image
./run_all.sh 8                # 8GB RAM disk
```

## Workflow

1. **Ensure Docker image** - Builds `unrealng-build:qt6.9.3` if missing
2. **Create RAM disk** - APFS (macOS) or tmpfs (Linux)
3. **Sync project** - rsync to RAM disk
4. **Build in Docker** - Mount RAM disk, run CMake/Ninja
5. **Cleanup** - Unmount RAM disk (unless `--keep`)

## Scripts

| Script | Purpose |
|--------|---------|
| `run_all.sh` | Orchestrator - runs complete cycle |
| `ensure_image.sh` | Verifies/builds Docker image for platform |
| `create_ram_disk.sh` | Creates/mounts RAM disk |
| `copy_to_ram.sh` | Syncs project to RAM disk |
| `build_in_docker.sh` | Runs builds inside Docker container |
| `cleanup_ram_disk.sh` | Unmounts RAM disk |
| `common.sh` | Shared utilities |

## Platform Support

| Platform | Architecture | Docker Image |
|----------|--------------|--------------|
| macOS | ARM64 (M1/M2) | `linux/arm64` |
| macOS | Intel | `linux/amd64` |
| Linux | x86_64 | `linux/amd64` |
| Linux | ARM64 | `linux/arm64` |

Platform is auto-detected. Docker image is built for matching architecture.

## Docker Image

Based on `docker/Dockerfile.universal`:
- Debian testing (GLIBC 2.38+)
- Qt 6.9.3 (qtmultimedia, qt5compat)
- ICU 56 (legacy support)
- OpenSSL, Brotli, UUID

First run builds the image (~10-15 min). Subsequent runs use cache.

## Build Targets

| Target | Source | Description |
|--------|--------|-------------|
| unreal-qt | unreal-qt/ | Main application |
| automation | unreal-qt/ | Automation plugin |
| core | / | Core library |
| core-tests | / | Unit tests |

## Requirements

- **Docker** with daemon running
- **6GB+ free RAM** (default)
- **rsync** for file sync

## Logs

Build logs: `<RAM_DISK>/unreal-ng/build-logs/`

## Why RAM Disk?

Docker volume mounts on macOS have significant I/O overhead due to the Linux VM layer. RAM disk provides:
- Native-speed I/O inside container
- 2-3x faster builds vs direct mount
- Consistent performance across platforms

## Troubleshooting

```bash
# Check Docker image exists
docker images | grep unrealng-build

# Force rebuild image
./ensure_image.sh --rebuild

# Interactive container shell
docker run -it --rm -v /Volumes/UnrealDockerRAM/unreal-ng:/workspace unrealng-build:qt6.9.3 bash
```
