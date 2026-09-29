#!/usr/bin/env bash
# Co-emulation runner: ZXMAK2, headless (see ../README.md for the contract).
#   ZXMAK2_DIR=<ZXMAK2 checkout> ./run.sh [machine...]
# Compiles ZXMAK2's engine, unmodified, from the checkout with the .NET SDK into a console runner
# (harness/), and runs the program on ZXMAK2's stock machines. ZXMAK2_BIN may name an already built
# zxmak2-harness.dll instead (it must have been built from a ZXMAK2 checkout).
. "$(dirname "$0")/../common/common.sh"
coemu_init zxmak2 "$@"

HERE=$(cd "$(dirname "$0")" && pwd)
BUILD=${ZXMAK2_BUILD:-$HERE/build}

if ! command -v dotnet >/dev/null 2>&1; then
	coemu_not_found "needs the .NET SDK (dotnet, 8 or later)"
fi
if [ -n "${ZXMAK2_BIN:-}" ]; then
	[ -f "$ZXMAK2_BIN" ] || coemu_not_found "ZXMAK2_BIN=$ZXMAK2_BIN does not exist"
	HARNESS=$ZXMAK2_BIN
else
	if [ -z "${ZXMAK2_DIR:-}" ] || [ ! -f "$ZXMAK2_DIR/src/ZXMAK2.Engine/Spectrum.cs" ]; then
		coemu_not_found "set ZXMAK2_DIR to a ZXMAK2 checkout (or ZXMAK2_BIN to a built zxmak2-harness.dll)"
	fi
	HARNESS=$BUILD/artifacts/bin/zxmak2-harness/release/zxmak2-harness.dll
	if ! ZXMAK2_DIR=$(cd "$ZXMAK2_DIR" && pwd) DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 \
		dotnet build "$HERE/harness/zxmak2-harness.csproj" -c Release --artifacts-path "$BUILD/artifacts" \
		> "$OUT/build.log" 2>&1; then
		for m in $MACHINES; do coemu_result "$m" error "build failed, see $OUT/build.log"; done
		coemu_finish
	fi
fi

for m in $MACHINES; do
	# what the program is loaded from; the machine itself is chosen inside the harness (Program.cs)
	case $m in
		48k|128k|plus3) media=$PROGRAM_TAP ;;
		pentagon|scorpion|profscorp|atm710|atm3|profi) media=$PROGRAM_TRD ;;
		plus2) coemu_result "$m" skipped "no stock machine: ZXMAK2 has no grey +2 (no machine uses the +2 ROMs)"; continue ;;
		plus2a) coemu_result "$m" skipped "no stock machine: ZXMAK2 has no +2A"; continue ;;
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
