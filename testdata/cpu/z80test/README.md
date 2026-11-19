# z80test 1.2a snapshots

Patrik Rak's Z80 CPU test suite ([z80test 1.2a](https://github.com/raxoft/z80test), MIT license), one 48K `.sna`
per variant. The reference values come from a real 48K Spectrum with a Zilog Z80.

| File | Checks |
|------|--------|
| `z80full.sna` | all flags and registers |
| `z80doc.sna` | all registers, documented flags only |
| `z80flags.sna` | all flags, no registers |
| `z80docflags.sna` | documented flags only |
| `z80ccf.sna` | all flags after a CCF behind each instruction: the hidden Q latch SCF / CCF read |
| `z80memptr.sna` | all flags after a BIT n,(HL) behind each instruction: MEMPTR |

Made from the release's `.tap` files: the CODE block (loaded at 32768) put into
`testdata/loaders/sna/z80full.sna` in place of its own code. The snapshot stops at the start of the program, so it
runs on load. `z80flags.sna` built this way has the same code bytes as `testdata/loaders/sna/z80flags.sna`.

Used by `Z80TestSuite_Test` (`core/tests/z80/z80test/z80test_program_test.cpp`): each whole program, in the long tier
(`UNREAL_LONG_TESTS=1`).
