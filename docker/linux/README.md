# Linux build in Docker

Reproduces the GitHub Actions Linux build locally, inside the same image
(`ghcr.io/alfishe/unreal-ng:qt6.9.3-ubuntu24.04`, gcc + libstdc++ + Qt).

```bash
docker/linux/build.sh                     # build core-tests, host-native architecture
docker/linux/build.sh --test              # ...and run the tests
docker/linux/build.sh --platform amd64    # force x86_64 (emulated on Apple Silicon, slow)
docker/linux/build.sh --filter '*FatName*' --test
docker/linux/build.sh --help
```

The image is multi-arch (amd64 + arm64). The default is the host's native
architecture, so there is no emulation and the build is several times faster.
Compiler errors such as a missing `#include <algorithm>` depend on the gcc and
libstdc++ versions, which are identical in both variants - use `--platform`
only to chase an architecture-specific problem.

Works from the main checkout and from any git worktree (the main repository's `.git` is
mounted read-only at its own path). Build output lands in `scratch/linux-<arch>-<type>/` (git-ignored). Delete it
when finished: `rm -rf scratch/linux-*`.

The tests' own scratch files (copies of test data, written images) go to a container-local tmpfs
(`/scratch-tmp`, set through `UNREAL_TEST_SCRATCH_DIR`), not into `scratch/` on the mounted checkout. On Docker
Desktop for Mac the checkout is a virtiofs bind mount, and creating a file with mode 0200 fails there with
"Permission denied"; `std::filesystem::copy_file` creates its destination that way, so about twenty tests (CHD, media,
disk save, snapshot paths) failed on the mount and pass on a real Linux host and on the tmpfs.

Windows cross-builds: see `docker/windows/`.
