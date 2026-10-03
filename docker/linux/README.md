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

Build output lands in `scratch/linux-<arch>-<type>/` (git-ignored). Delete it
when finished: `rm -rf scratch/linux-*`.

Windows cross-builds: see `docker/windows/`.
