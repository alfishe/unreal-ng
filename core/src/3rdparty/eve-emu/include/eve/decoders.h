/* eve-emu - FT812 emulator library. Decoder interfaces.
 *
 * The same interfaces serve the built-in (vendored) decoders and decoders supplied by
 * the host. The library validates everything the chip validates (sizes, formats,
 * limits) itself; decoders only decode. */
#ifndef EVE_DECODERS_H
#define EVE_DECODERS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum EveDecodeStatus
{
    EVE_DECODE_OK = 0,          /* finished */
    EVE_DECODE_NEED_INPUT = 1,  /* streaming: give more input */
    EVE_DECODE_OUTPUT_FULL = 2, /* streaming: give more output space */
    EVE_DECODE_ERROR = 3        /* invalid data: the chip faults */
} EveDecodeStatus;

/* --- zlib inflate, streaming (CMD_INFLATE data arrives in ring-sized pieces) ------- */
typedef struct EveInflateDecoder
{
    size_t stateSize;   /* bytes of decoder state the library allocates */
    int stateIsPlain;   /* 1: the state (incl. its dictionary) is a plain blob without
                           pointers; it is saved and reloaded for TTD. 0: the library
                           keeps the consumed input instead */
    void (*Begin)(void* state, void* user); /* start a zlib stream */
    EveDecodeStatus (*Run)(void* state, void* user,
                           const uint8_t* in, size_t inSize, size_t* inUsed,
                           uint8_t* out, size_t outSize, size_t* outWritten);
    void* user;
} EveInflateDecoder;

/* --- still images: the whole file at once ------------------------------------------ */
typedef enum EveImageLayout
{
    EVE_IMAGE_GRAY8,    /* PNG gray, JPEG gray */
    EVE_IMAGE_RGB888,   /* PNG truecolor, JPEG color */
    EVE_IMAGE_RGBA8888, /* PNG truecolor + alpha */
    EVE_IMAGE_INDEXED8  /* PNG indexed; palette as RGBA8888, up to 256 entries */
} EveImageLayout;

typedef struct EveImageInfo
{
    uint32_t width, height;
    EveImageLayout layout;
    uint8_t interlaced, progressive, cmyk, bitDepth, paletteHasAlpha;
} EveImageInfo;

typedef struct EveImageDecoder
{
    /* Read the header only: the library applies the chip's rules before decoding. */
    EveDecodeStatus (*Probe)(void* user, const uint8_t* data, size_t size, EveImageInfo* info);
    /* Decode into the canonical layout reported by Probe; palette only for INDEXED8
       (RGBA8888 entries packed as 0xAARRGGBB). */
    EveDecodeStatus (*Decode)(void* user, const uint8_t* data, size_t size,
                              uint8_t* pixels, size_t pixelsSize,
                              uint32_t* palette, size_t paletteEntries);
    void* user;
} EveImageDecoder;

typedef struct EveDecoders
{
    uint32_t structSize;
    const EveInflateDecoder* inflate; /* NULL = built-in (if compiled in) */
    const EveImageDecoder* png;       /* NULL = built-in */
    const EveImageDecoder* jpeg;      /* NULL = built-in; also used for M-JPEG video frames */
} EveDecoders;

/* The built-ins, so a host can wrap or test them; NULL when not compiled in. */
const EveInflateDecoder* EveBuiltinInflate(void);
const EveImageDecoder* EveBuiltinPng(void);
const EveImageDecoder* EveBuiltinJpeg(void);

#ifdef __cplusplus
}
#endif

#endif
