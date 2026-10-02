/* eve-emu - the built-in inflate: tinfl from miniz, compiled with every symbol renamed
   into the Eve namespace so nothing collides with a host that links its own miniz
   (arch §6.3). Warnings are off for this translation unit: the code is vendored. */

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_MALLOC

#define tinfl_decompress EveLibTinflDecompress
#define tinfl_decompress_mem_to_heap EveLibTinflMemToHeap
#define tinfl_decompress_mem_to_mem EveLibTinflMemToMem
#define tinfl_decompress_mem_to_callback EveLibTinflMemToCallback
#define tinfl_decompressor_alloc EveLibTinflAlloc
#define tinfl_decompressor_free EveLibTinflFree
#define miniz_def_alloc_func EveLibMinizAlloc
#define miniz_def_free_func EveLibMinizFree
#define miniz_def_realloc_func EveLibMinizRealloc

#include "miniz/miniz_tinfl.c"

#include "eve-vendor-tinfl.h"

/* Decoder state: the decompressor plus its 32 KB circular dictionary. No pointers, so
   the whole struct is a plain blob that can be saved and reloaded. */
typedef struct EveLibTinflState
{
    tinfl_decompressor decompressor;
    mz_uint8 dictionary[TINFL_LZ_DICT_SIZE];
    size_t dictionaryPos;  /* where the next output goes */
    size_t pendingStart;   /* produced but not yet handed out: [pendingStart, +pendingSize) */
    size_t pendingSize;
    int flags;
    int done;
} EveLibTinflState;

size_t EveLibTinflStateSize(void)
{
    return sizeof(EveLibTinflState);
}

void EveLibTinflBegin(void* state, int zlibHeader)
{
    EveLibTinflState* s = (EveLibTinflState*)state;
    memset(s, 0, sizeof(*s));
    tinfl_init(&s->decompressor);
    s->flags = TINFL_FLAG_HAS_MORE_INPUT | (zlibHeader ? TINFL_FLAG_PARSE_ZLIB_HEADER : 0);
}

int EveLibTinflRun(void* state, const unsigned char* in, size_t inSize, size_t* inUsed,
                   unsigned char* out, size_t outSize, size_t* outWritten)
{
    EveLibTinflState* s = (EveLibTinflState*)state;
    *inUsed = 0;
    *outWritten = 0;
    for (;;)
    {
        /* Hand out what is already decoded. */
        while (s->pendingSize > 0 && *outWritten < outSize)
        {
            size_t n = s->pendingSize;
            size_t room = outSize - *outWritten;
            size_t untilWrap = TINFL_LZ_DICT_SIZE - s->pendingStart;
            if (n > room)
                n = room;
            if (n > untilWrap)
                n = untilWrap;
            memcpy(out + *outWritten, s->dictionary + s->pendingStart, n);
            *outWritten += n;
            s->pendingStart = (s->pendingStart + n) & (TINFL_LZ_DICT_SIZE - 1);
            s->pendingSize -= n;
        }
        if (s->pendingSize > 0)
            return EVE_LIB_TINFL_OUTPUT_FULL;
        if (s->done)
            return EVE_LIB_TINFL_DONE;

        {
            size_t inBytes = inSize - *inUsed;
            size_t outBytes = TINFL_LZ_DICT_SIZE - s->dictionaryPos;
            tinfl_status status = tinfl_decompress(&s->decompressor, in + *inUsed, &inBytes,
                                                   s->dictionary, s->dictionary + s->dictionaryPos,
                                                   &outBytes, (mz_uint32)s->flags);
            *inUsed += inBytes;
            s->pendingStart = s->dictionaryPos;
            s->pendingSize = outBytes;
            s->dictionaryPos = (s->dictionaryPos + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
            if (status < TINFL_STATUS_DONE)
                return EVE_LIB_TINFL_ERROR;
            if (status == TINFL_STATUS_DONE)
                s->done = 1;
            else if (status == TINFL_STATUS_NEEDS_MORE_INPUT && outBytes == 0)
                return EVE_LIB_TINFL_NEED_INPUT;
        }
    }
}
