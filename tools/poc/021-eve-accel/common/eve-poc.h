// eve-accel POC hooks: what an experiment's library variant adds to the eve-emu API.
// The pristine variant links eve-poc-stub.cpp (all hooks do nothing).
#pragma once

#include <eve/eve.h>

#include <cstdint>
#include <cstdio>

extern "C" {

/// Name of the library variant ("base", "census", "threads", ...)
const char* EvePocVariant(void);
/// Draw a batch of lines with n threads; returns the count in use (1 when unsupported)
int EvePocSetThreads(EveChip* chip, int threads);
/// Statistics the variant collected (census, line timing, batch fallbacks)
void EvePocPrintStats(EveChip* chip, FILE* out);
/// Write frame snapshots (RAM_G, display list, state, CPU picture) for frames in
/// [fromFrame, toFrame] into dir; only frames drawn in one batch are written
void EvePocSetSnapshot(EveChip* chip, const char* dir, uint64_t fromFrame, uint64_t toFrame);
}
