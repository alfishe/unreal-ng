#!/usr/bin/env bash
# Build the Steam Deck bundle of this checkout on a remote x86_64 Linux host and fetch it; optionally install it on a
# Deck (deploy.sh). The host builds in the bundle's Docker image (install/steamdeck/Dockerfile.build, built there on
# first use) inside a checkout of its own: this checkout's committed and uncommitted changes are copied into it, its
# HEAD stays. Recipe: .recipe/testing/steamdeck.md#build-on-a-remote-host
#
#   tools/deploy/steamdeck/remote-build.sh --host <build-host> --dir <path>        # build, fetch the bundle
#   tools/deploy/steamdeck/remote-build.sh --host <build-host> --dir <path> --deck <deck>   # ... and install it
#
#   --host  SSH target of the build host (UNREAL_DEPLOY_BUILD_HOST)
#   --dir   the host's checkout, a clone or worktree of unreal-ng with its submodules, yours and named after the task
#           (relative to the home folder or absolute; UNREAL_DEPLOY_BUILD_DIR)
#   --deck  SSH target of the Deck to install on (UNREAL_DEPLOY_DECK); none: only build and fetch
#   --out   where the bundle goes (scratch/deploy-steamdeck/UnrealNG-SteamDeck-x86_64.tar.gz)
#   --no-sync  build what the host's checkout has
#
# SSH options (a key, a port) come from ~/.ssh/config, or UNREAL_DEPLOY_SSH="ssh -i <key>" for every ssh / rsync here.
set -euo pipefail

host="${UNREAL_DEPLOY_BUILD_HOST:-}"
dir="${UNREAL_DEPLOY_BUILD_DIR:-}"
deck="${UNREAL_DEPLOY_DECK:-}"
out=""
sync=1
while [ $# -gt 0 ]; do
  case "$1" in
    --host) host="$2"; shift 2 ;;
    --dir) dir="$2"; shift 2 ;;
    --deck) deck="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --no-sync) sync=0; shift ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -n "$host" ] && [ -n "$dir" ] || { echo "usage: remote-build.sh --host <build-host> --dir <path> [--deck <deck>]" >&2; exit 2; }

SSH="${UNREAL_DEPLOY_SSH:-ssh}"
root="$(git rev-parse --show-toplevel)"
cd "$root"
state="$root/scratch/deploy-steamdeck"
mkdir -p "$state"
out="${out:-$state/UnrealNG-SteamDeck-x86_64.tar.gz}"
image="unreal-ng-deck-build"
stamp="$(date +%Y%m%d.%H%M%S)"
remote() { $SSH "$host" "cd $(printf %q "$dir") && $1"; }

# 1. The changes: files differing from the host's HEAD (commits since it and uncommitted edits), new files, and the
#    files of the previous sync (one reverted here must go back there). Deleted files stay on the host (the build globs
#    what it finds); submodule changes are not copied
if [ "$sync" = 1 ]; then
  base="$(remote 'git rev-parse HEAD')"
  git cat-file -e "$base^{commit}" 2>/dev/null || {
    echo "the host's HEAD $base is not in this repository: fetch it, or move the host's checkout to a commit you have" >&2
    exit 1
  }
  list="$state/sync-files.txt"
  prev="$(cat "$list" 2>/dev/null || true)"
  { git diff --name-only "$base"; git ls-files --others --exclude-standard; echo "$prev"; } | { grep -v '^$' || true; } | sort -u > "$list.new"  # nothing to copy is fine
  mv "$list.new" "$list"
  # no -t: a copied file is newer than the objects built from its old copy (ninja compares times)
  rsync -rlp --checksum --ignore-missing-args -e "$SSH" --files-from="$list" . "$host:$dir/"
  echo "sync: $(wc -l < "$list" | tr -d ' ') files against the host's $(echo "$base" | cut -c1-9)"
fi

# 2. The build image, once per host
remote "docker image inspect $image >/dev/null 2>&1 || docker build -q -t $image -f install/steamdeck/Dockerfile.build install/steamdeck >/dev/null"

# 3. Configure (first time), build, package: suite, AppImage, Steam Deck bundle. A worktree's .git points to the main
#    repository's: the container sees that too. Files the container writes are root's (run every step inside it)
remote "W=\$PWD; G=\$(cd \"\$(git rev-parse --git-common-dir)\" && pwd); \
  docker run --rm --name \"$image-\$(id -un)-\$(basename \"\$W\")\" -e UNREAL_PACKAGE_VERSION=$stamp \
    -v \"\$W\":\"\$W\" -v \"\$G\":\"\$G\" -w \"\$W\" $image bash -c '
    set -e
    cd /usr/local/bin && for t in linuxdeploy linuxdeploy-plugin-qt; do [ -e \$t ] || ln -sf \$t-x86_64.AppImage \$t; done; cd - >/dev/null
    [ -f build-deck/CMakeCache.txt ] || cmake -B build-deck -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_QT_APPS=ON \
      -DTESTS=OFF -DBENCHMARKS=OFF -DTRANTOR_USE_TLS=none -DBUILD_C-ARES=OFF -DQT_INSTALL_PATH=/opt/qt/current \
      -DUNREAL_BUNDLE_QT=ON -DUNREAL_STREAMDECK=ON -DUNREAL_PACKAGE_BASENAME=UnrealNG-Suite-Linux-x86_64 \
      > build-deck-configure.log 2>&1 || { tail -30 build-deck-configure.log; exit 1; }
    rm -rf build-deck/packages
    cmake build-deck > build-deck/configure.log 2>&1 || { tail -30 build-deck/configure.log; exit 1; }
    cmake --build build-deck --target package_suite_linux > build-deck/package-build.log 2>&1 || { tail -30 build-deck/package-build.log; exit 1; }
    bash tools/package-appimage.sh build-deck > build-deck/appimage.log 2>&1 || { tail -30 build-deck/appimage.log; exit 1; }
    install/steamdeck/package.sh build-deck >/dev/null'"

# 4. Fetch it
rsync -q -e "$SSH" "$host:$dir/build-deck/packages/UnrealNG-SteamDeck-x86_64.tar.gz" "$out"
echo "bundle: $out ($stamp)"

# 5. Install
if [ -n "$deck" ]; then
  exec "$(dirname "$0")/deploy.sh" "$out" "$deck"
fi
