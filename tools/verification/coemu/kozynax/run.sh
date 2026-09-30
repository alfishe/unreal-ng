#!/usr/bin/env bash
# Co-emulation runner: Kozynax, headless (see ../README.md for the contract).
#   KOZYNAX_DIR=<Kozynax checkout> ./run.sh [machine...]
# Builds Kozynax's own project (src/Kozynax.Sdl.csproj), unmodified, from the checkout with the .NET SDK and
# links it into a console runner (harness/), then runs the program on Kozynax's stock machines. KOZYNAX_BIN
# may name an already built kozynax-harness.dll instead (it must have been built from a Kozynax checkout).
. "$(dirname "$0")/../common/common.sh"
coemu_init kozynax "$@"

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${KOZYNAX_BUILD:-$HERE/build}

if ! command -v dotnet >/dev/null 2>&1; then
	coemu_not_found "needs the .NET SDK (dotnet, 8 or later)"
fi
if [ -n "${KOZYNAX_BIN:-}" ]; then
	[ -f "$KOZYNAX_BIN" ] || coemu_not_found "KOZYNAX_BIN=$KOZYNAX_BIN does not exist"
	HARNESS=$KOZYNAX_BIN
else
	if [ -z "${KOZYNAX_DIR:-}" ] || [ ! -f "$KOZYNAX_DIR/src/Kozynax.Sdl.csproj" ]; then
		coemu_not_found "set KOZYNAX_DIR to a Kozynax checkout (git clone https://github.com/kozynax/kozynax), or KOZYNAX_BIN to a built kozynax-harness.dll"
	fi
	HARNESS=$BUILD/artifacts/bin/kozynax-harness/release/kozynax-harness.dll
	# at most 4 build jobs: the machine is shared
	if ! KOZYNAX_DIR=$(cd "$KOZYNAX_DIR" && pwd) DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 \
		dotnet build "$HERE/harness/kozynax-harness.csproj" -c Release --artifacts-path "$BUILD/artifacts" \
		-maxcpucount:4 > "$OUT/build.log" 2>&1; then
		for m in $MACHINES; do coemu_result "$m" error "build failed, see $OUT/build.log"; done
		coemu_finish
	fi
fi

for m in $MACHINES; do
	# what the program is loaded from; the machine itself is chosen inside the harness (Program.cs)
	case $m in
		48k|128k|plus3) media=$PROGRAM_TAP ;;
		pentagon|scorpion|profscorp|atm710|atm3|profi) media=$PROGRAM_TRD ;;
		plus2) coemu_result "$m" skipped "no stock machine: Kozynax has no grey +2 (no machine uses the +2 ROMs)"; continue ;;
		plus2a) coemu_result "$m" skipped "no stock machine: Kozynax has no +2A"; continue ;;
		*) coemu_result "$m" skipped "not in this runner yet"; continue ;;
	esac
	if [ ! -f "$media" ]; then
		coemu_result "$m" skipped "no $(basename "$media") for this program"
		continue
	fi
	rm -f "$OUT/$m.bin"
	DOTNET_CLI_TELEMETRY_OPTOUT=1 dotnet "$HARNESS" "$m" "$media" "$PROGRAM_SYM" "$OUT/$m" "$MAX_FRAMES" > "$OUT/$m.log" 2>&1
	rc=$?
	case $rc in
		0) coemu_compare "$m" ;;
		1) coemu_result "$m" error "not done in $MAX_FRAMES frames, see $OUT/$m.log" ;;
		3) coemu_result "$m" error "$(grep -m1 'crashed' "$OUT/$m.log")" ;;
		*) coemu_result "$m" error "harness failed (exit $rc), see $OUT/$m.log" ;;
	esac
done
coemu_finish
