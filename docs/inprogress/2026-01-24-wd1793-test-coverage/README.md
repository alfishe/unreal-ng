# Work Item: WD1793 Full Test Coverage

> **Date:** 2026-01-24  
> **Status:** Planning  
> **Effort:** 15-21 days estimated  

## Summary

Bring the WD1793 FDC emulation to a fully featured, fully tested state with comprehensive coverage for ZX-Spectrum disk operations including copy protection support.

## Priority Structure

| Priority | Focus | Description |
|----------|-------|-------------|
| **P1** | ZX-Spectrum Emulation | Core features used by games and software |
| **P2** | Full Testability | 100% coverage for implemented features |
| **P3** | Extended Features | Full datasheet compliance |

## Documents

| Document | Description |
|----------|-------------|
| [implementation-plan.md](implementation-plan.md) | 8-phase roadmap with deliverables |
| [test-matrix.md](test-matrix.md) | 92 tests with status and priority |
| [diskimage-modernization.md](diskimage-modernization.md) | DiskImage/MFM refactoring spec |

## Quick Stats

| Metric | Current | Target |
|--------|---------|--------|
| Tests | ~93 | 150+ |
| Coverage | 29% | 95%+ |
| Sector sizes | 1/4 | 4/4 |
| Commands | 60% | 100% |

## Phase Summary

1. **Core Operations** - READ/WRITE SECTOR tests, error handling
2. **Non-Standard Layouts** - Copy protection, variable geometry
3. **DiskImage Modernization** - Variable sector sizes, MFM refactor
4. **Test Completeness** - All commands, all error conditions
5. **Positioning Commands** - SEEK, STEP, verify flag
6. **READ ADDRESS/TRACK** - Type 3 commands
7. **Advanced Features** - Multi-sector, deleted marks
8. **Timing Accuracy** - Precise timing, stepping rates

## Recent Fixes (2026-01-24)

- ✅ Off-by-one sector indexing in `diskimage.h`
- ✅ WRITE SECTOR DRQ timing in `wd1793.cpp`
- ✅ FORMAT bypass initialization for `$5CE6/$5CE8`

## How to Run Tests

```bash
# All WD1793 tests
./cmake-build-debug/bin/core-tests --gtest_filter="WD1793*"

# MFM parser tests
./cmake-build-debug/bin/core-tests --gtest_filter="MFMParserTest*"

# Integration tests only
./cmake-build-debug/bin/core-tests --gtest_filter="WD1793_Integration*"
```

## Related

- [Gap Analysis](../../reviews/wd1793-test-gap-analysis.md)
- [TR-DOS Track Structure](../../design/io/fdc/trdos-format-track-structure.md)
