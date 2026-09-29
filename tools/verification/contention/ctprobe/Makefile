# ctprobe - memory contention probe (see README.md)
#
#   make files                  rebuild ctprobe.tap / .trd / .sym from the .asm sources
#   make check                  the quick checks: the files match the sources, one 48K check
#   make test                   every check on every machine, and the files loaded as a user loads them
#   make compare DUMP=file.bin  compare a memory dump of a finished run with the expected values
#   make xpeccy-plus XPECCY_DIR=<checkout>   run the probe on the stock xpeccy-plus machines, headless
#
# The .tap / .trd / .sym are built by unreal-ng's test suite (it has the assembler and the oracle that fills
# in the expected values): BUILD is an unreal-ng build directory configured with -DTESTS=ON.

ROOT       := $(abspath ../../../..)
BUILD      ?= $(ROOT)/cmake-build-agent-release
CORE_TESTS := $(BUILD)/bin/core-tests

.PHONY: all files check test compare xpeccy-plus core-tests clean

all: files

core-tests:
	ninja -C $(BUILD) core-tests

files: core-tests
	UNREAL_CTPROBE_EXPORT=1 $(CORE_TESTS) --gtest_filter='CtProbeFiles_Test.Export'

check: core-tests
	$(CORE_TESTS) --gtest_filter='CtProbeFiles_Test.*:CtProbe_Test.*'

test: core-tests
	UNREAL_TIMING_SUITES=1 $(CORE_TESTS) --gtest_filter='CtProbe*:Opt/CtProbe*'

compare:
	@test -n "$(DUMP)" || (echo "usage: make compare DUMP=file.bin" && exit 2)
	python3 ctprobe-compare.py $(DUMP) ctprobe.sym

xpeccy-plus:
	@test -n "$(XPECCY_DIR)" || (echo "usage: make xpeccy-plus XPECCY_DIR=<xpeccy-plus checkout>" && exit 2)
	XPECCY_DIR=$(XPECCY_DIR) ../../xpeccy-plus/run.sh

clean:
	rm -rf ../../xpeccy-plus/build ../../xpeccy-plus/out
