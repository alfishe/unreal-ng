// eve-accel: the POC hooks of the pristine library (nothing added).
#include "eve-poc.h"

extern "C" {

const char* EvePocVariant(void)
{
    return "base";
}

int EvePocSetThreads(EveChip*, int)
{
    return 1;
}

void EvePocPrintStats(EveChip*, FILE*)
{
}

void EvePocSetSnapshot(EveChip*, const char*, uint64_t, uint64_t)
{
}
}
