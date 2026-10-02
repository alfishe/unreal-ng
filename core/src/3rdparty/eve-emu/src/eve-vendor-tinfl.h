/* eve-emu - interface of the built-in inflate (eve-vendor-tinfl.c). */
#ifndef EVE_VENDOR_TINFL_H
#define EVE_VENDOR_TINFL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    EVE_LIB_TINFL_DONE = 0,
    EVE_LIB_TINFL_NEED_INPUT = 1,
    EVE_LIB_TINFL_OUTPUT_FULL = 2,
    EVE_LIB_TINFL_ERROR = 3
};

size_t EveLibTinflStateSize(void);
void EveLibTinflBegin(void* state, int zlibHeader);
int EveLibTinflRun(void* state, const unsigned char* in, size_t inSize, size_t* inUsed,
                   unsigned char* out, size_t outSize, size_t* outWritten);

#ifdef __cplusplus
}
#endif

#endif
