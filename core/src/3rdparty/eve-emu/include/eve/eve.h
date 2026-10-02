/* eve-emu - FT812 emulator library. Chip API.
 *
 * Plain C. All functions take the chip first; none of them call back into the host
 * except the decoder interfaces of decoders.h. The library knows nothing about the
 * host machine: the chip has an SPI port, a clock input, an interrupt pin and a
 * picture output. */
#ifndef EVE_EVE_H
#define EVE_EVE_H

#include <stddef.h>
#include <stdint.h>

#include "eve/decoders.h"
#include "eve/version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Lifetime and configuration ---------------------------------------------------- */

typedef struct EveChip EveChip;

typedef enum EveModel
{
    EVE_MODEL_FT812 = 0 /* the only model */
} EveModel;

typedef struct EveConfig
{
    uint32_t structSize;         /* sizeof(EveConfig), for later extension */
    EveModel model;
    uint32_t externalClockHz;    /* VDAC2: 8 000 000 */
    const uint8_t* romImage;     /* the FT81x ROM 0x1E0000..0x2FFFFF (1152 KB, fonts 16-34).
                                    A shorter image is taken as the end of that range:
                                    (0x300000 - romImageSize) .. 0x2FFFFF. NULL = ROM reads
                                    return 0 */
    size_t romImageSize;         /* the library keeps the pointer; the host keeps the memory */
    const EveDecoders* decoders; /* NULL = built-ins only */
} EveConfig;

typedef struct EveBuildInfo
{
    uint32_t version; /* (major << 16) | (minor << 8) | patch */
    uint8_t builtinInflate, builtinPng, builtinJpeg;
} EveBuildInfo;

void EveGetBuildInfo(EveBuildInfo* out);
EveChip* EveCreate(const EveConfig* config); /* NULL if a required decoder is missing */
void EveDestroy(EveChip* chip);
void EveReset(EveChip* chip);                /* power-on */
/* Reason of the last failed EveCreate / EveLoadState on the calling thread
   (thread-local; "" when there was none). */
const char* EveLastError(void);

/* --- Host bus ---------------------------------------------------------------------- */

void EveSelect(EveChip* chip, int selected); /* CS_N low (1) / high (0) */
uint8_t EveExchange(EveChip* chip, uint8_t mosi);

/* --- Time -------------------------------------------------------------------------- */

void EveAdvance(EveChip* chip, uint64_t systemClocks);
/* Clocks until the next INT_N change, frame end, coprocessor completion or timer;
   UINT64_MAX when nothing is scheduled. */
uint64_t EveClocksToNextEvent(const EveChip* chip);
uint32_t EveSystemClockHz(const EveChip* chip); /* 0 while stopped */
uint64_t EveTotalClocks(const EveChip* chip);   /* since creation; not REG_CLOCK */

/* --- Outputs ----------------------------------------------------------------------- */

int EveIntAsserted(const EveChip* chip); /* INT_N low */

typedef struct EveTiming
{
    uint16_t hcycle, hoffset, hsize, hsync0, hsync1;
    uint16_t vcycle, voffset, vsize, vsync0, vsync1;
    uint16_t pclkDivider;
    uint32_t pixelClockHz;
    uint64_t framePeriodClocks;
} EveTiming;
void EveGetTiming(const EveChip* chip, EveTiming* out);

/* The host owns the frame buffer (ARGB8888). drawing = 0 keeps all timing and draws
   nothing. A mode larger than the capacity is clipped. */
void EveSetOutput(EveChip* chip, uint32_t* framebuffer, uint32_t stridePixels,
                  uint32_t widthCapacity, uint32_t heightCapacity, int drawing);
uint64_t EveCompletedFrames(const EveChip* chip); /* frames fully scanned out */

/* --- State ------------------------------------------------------------------------- */

size_t EveStateSize(const EveChip* chip);          /* fixed for the chip's lifetime */
void EveSaveState(const EveChip* chip, void* out); /* control state + in-flight header */
int EveLoadState(EveChip* chip, const void* in, size_t size); /* 0 = ok */

typedef struct EveRegion
{
    const char* name; /* "RAM_G", "DL0", "DL1", "REG", "CMD", "SPECIAL", "INFLIGHT" */
    uint8_t* base;
    size_t size;
    const uint64_t* dirty; /* one bit per 4 KB page */
    size_t pageCount;
} EveRegion;
size_t EveRegionCount(const EveChip* chip);
void EveGetRegion(const EveChip* chip, size_t index, EveRegion* out);
void EveClearDirty(EveChip* chip);
void EveMemoryRestored(EveChip* chip); /* the host wrote region contents back */

/* --- Inspection (never changes state) ---------------------------------------------- */

typedef enum EveCoproPhase
{
    EVE_COPRO_IDLE = 0,         /* ring empty */
    EVE_COPRO_EXECUTING = 1,    /* a command is running, costLeft clocks to its next step */
    EVE_COPRO_WAITING_DATA = 2, /* a command waits for ring or media FIFO data */
    EVE_COPRO_WAITING_SWAP = 3, /* CMD_DLSTART waits for a pending swap */
    EVE_COPRO_FAULT = 4,        /* faulted; REG_CMD_READ = 0xFFF */
    EVE_COPRO_RESET = 5         /* held in reset by REG_CPURESET bit 0 */
} EveCoproPhase;

typedef struct EveCoproView
{
    uint32_t readPtr, writePtr; /* REG_CMD_READ, REG_CMD_WRITE */
    uint32_t cmdDl;             /* REG_CMD_DL */
    EveCoproPhase phase;
    uint32_t command;           /* code of the command in progress, 0 when none */
    uint32_t commandAddress;    /* ring offset of that command */
    uint64_t costLeft;          /* system clocks until its next step */
    uint32_t faultCommand;      /* the command that faulted, 0 when none */
    const char* faultReason;    /* static text, "" when none */
    int32_t matrix[6];          /* current matrix a..f, 16.16 */
    uint32_t fontPointers[32];  /* font metric block per handle */
    uint32_t scratchHandle, numberBase;
    uint32_t mediaFifoBase, mediaFifoSize;
    uint32_t bgColor, fgColor, gradColor;
} EveCoproView;

typedef struct EveLineCost
{
    uint32_t line;        /* visible line, 0 = first */
    uint32_t valid;       /* 0: the line has not been drawn since the last state change */
    uint32_t commands;    /* display list commands executed (subroutines counted per call) */
    uint32_t fillClocks;  /* clocks of pixel filling */
    uint32_t totalClocks; /* commands + fillClocks */
    uint32_t budget;      /* clocks available for the line */
    uint32_t overflow;    /* 1 when totalClocks > budget */
} EveLineCost;

typedef struct EvePixelSource
{
    uint32_t color;         /* final output ARGB8888 */
    uint32_t written;       /* 0: no primitive touched the pixel */
    uint32_t commandIndex;  /* display list index of the vertex that last wrote it */
    uint32_t command;       /* the display list word at that index */
    uint8_t primitive;      /* BEGIN value, 0 = CLEAR */
    uint8_t handle, cell;   /* bitmap handle and cell, for BITMAPS */
    uint8_t tag, stencil;   /* tag and stencil buffer values */
    uint8_t reserved[3];
    uint32_t sampleAddress; /* RAM_G address of the bitmap sample, for BITMAPS */
} EvePixelSource;

uint8_t EvePeek(const EveChip* chip, uint32_t address); /* no clear-on-read */
size_t EveGetDisplayList(const EveChip* chip, int active, uint32_t* words, size_t max);
int EveDisassemble(uint32_t word, char* text, size_t size); /* length written */
void EveGetCoprocessor(const EveChip* chip, EveCoproView* out);
void EveGetLineCost(const EveChip* chip, uint32_t line, EveLineCost* out);
int EveProbePixel(const EveChip* chip, uint32_t x, uint32_t y, EvePixelSource* out);

#ifdef __cplusplus
}
#endif

#endif
