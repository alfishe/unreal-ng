# Steam Deck deploy tools

Build a checkout's Steam Deck bundle on a remote x86_64 Linux host, install it on a Deck, check the installed build.
The bundle itself (what it holds, the installer) is [install/steamdeck](../../../install/steamdeck/README.md); the
how-to with pitfalls is the recipe [.recipe/testing/steamdeck.md](../../../.recipe/testing/steamdeck.md); the rules
are the [deploy guidelines](../../../docs/guidelines/deploy-guidelines.md).

| Script | What it does |
|:--|:--|
| `remote-build.sh --host <build-host> --dir <path> [--deck <deck>]` | Copies this checkout's changes (commits since the host's HEAD, uncommitted edits, new files) into the host's checkout, builds the suite, the AppImage and the bundle there in the bundle's Docker image, fetches the bundle to `scratch/deploy-steamdeck/`; with `--deck` installs it (`deploy.sh`) |
| `deploy.sh <bundle> [<deck>]` | Installs a bundle on a Deck: copy, unpack, `install.sh`, clean up. Exit 0 only when the Steam entry is registered; 3 when a running game kept the entry unchanged |
| `check_machine_picker.py [--app <path>] [--port <n>]` | The handheld menu's machine picker on an installed build, through the GUI control plane of an instance of its own (offscreen, own ports and settings folders, windowless): families, `.` / `..`, B, the cursor, starting machines. Expectations from the build's `GET /emulator/machines`. Standard library only |

```bash
export UNREAL_DEPLOY_BUILD_HOST=<build-host> UNREAL_DEPLOY_BUILD_DIR=<path> UNREAL_DEPLOY_DECK=<deck>
tools/deploy/steamdeck/remote-build.sh                                    # build + fetch
tools/deploy/steamdeck/deploy.sh scratch/deploy-steamdeck/UnrealNG-SteamDeck-x86_64.tar.gz   # install
ssh "$UNREAL_DEPLOY_DECK" python3 - < tools/deploy/steamdeck/check_machine_picker.py         # check
```

Hosts, users, keys and paths are never written into these scripts or the repository: they come from the arguments,
the `UNREAL_DEPLOY_*` variables ([environment variables](../../../docs/emulator/environment-variables.md)) and your
`~/.ssh/config` (or `UNREAL_DEPLOY_SSH="ssh -i <key>"`).

## The build host's checkout

`--dir` is a checkout of its own on the host: a clone or a worktree of unreal-ng with its submodules
(`git submodule update --init --recursive`), named after the task (`<clone>/scratch/wt-<task>`), never another
session's. Its HEAD must be a commit this repository has (a pushed master is enough): `remote-build.sh` copies only the
files that differ from it. Deleted files and submodule changes are not copied; move the host's checkout on
(`git -C <path> fetch && git -C <path> checkout <commit>`) after such a change. The first build configures
`build-deck/` (release, bundled Qt, Stream Deck daemon) and builds the image `unreal-ng-deck-build` from
`install/steamdeck/Dockerfile.build`; later builds are incremental. Files the container writes are root's.
