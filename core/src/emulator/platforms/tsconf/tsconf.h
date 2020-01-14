#pragma once
#include "stdafx.h"

typedef void (* INITIAL_FUNCTION)();
typedef uint32_t (* TASK_FUNCTION)(uint32_t);

// TS ext port #AF registers
enum TSREGS
{
    TSW_VCONF		= 0x00,
    TSW_VPAGE       = 0x01,
    TSW_GXOFFSL     = 0x02,
    TSW_GXOFFSH     = 0x03,
    TSW_GYOFFSL     = 0x04,
    TSW_GYOFFSH     = 0x05,
    TSW_TSCONF      = 0x06,
    TSW_PALSEL      = 0x07,
    TSW_BORDER      = 0x0F,

    TSW_PAGE0       = 0x10,
    TSW_PAGE1       = 0x11,
    TSW_PAGE2       = 0x12,
    TSW_PAGE3       = 0x13,
    TSW_FMADDR      = 0x15,
    TSW_TMPAGE      = 0x16,
    TSW_T0GPAGE     = 0x17,
    TSW_T1GPAGE     = 0x18,
    TSW_SGPAGE      = 0x19,
    TSW_DMASAL      = 0x1A,
    TSW_DMASAH      = 0x1B,
    TSW_DMASAX      = 0x1C,
    TSW_DMADAL      = 0x1D,
    TSW_DMADAH      = 0x1E,
    TSW_DMADAX      = 0x1F,

    TSW_SYSCONF     = 0x20,
    TSW_MEMCONF     = 0x21,
    TSW_HSINT       = 0x22,
    TSW_VSINTL      = 0x23,
    TSW_VSINTH      = 0x24,
    TSW_DMALEN      = 0x26,
    TSW_DMACTR      = 0x27,
    TSW_DMANUM      = 0x28,
    TSW_FDDVIRT     = 0x29,
    TSW_INTMASK     = 0x2A,
    TSW_CACHECONF   = 0x2B,

    TSW_T0XOFFSL    = 0x40,
    TSW_T0XOFFSH    = 0x41,
    TSW_T0YOFFSL    = 0x42,
    TSW_T0YOFFSH    = 0x43,
    TSW_T1XOFFSL    = 0x44,
    TSW_T1XOFFSH    = 0x45,
    TSW_T1YOFFSL    = 0x46,
    TSW_T1YOFFSH    = 0x47,

    TSR_STATUS      = 0x00,
    TSR_PAGE2       = 0x12,
    TSR_PAGE3       = 0x13,
    TSR_DMASTATUS   = 0x27
};

// FMAPS
enum FMAPSDEV
{
    TSF_CRAM    = 0x00, // 000x a[11:9]
    TSF_SFILE   = 0x01, // 001x a[11:9]
    TSF_REGS    = 0x04  // 0100 a[11:8]
};

// INT
enum INTSRC
{
    INT_FRAME    = 0x00,
    INT_LINE,
    INT_DMA
};

// DMA devices
enum DMADEV
{
    DMA_RES1     = 0x00,
    DMA_RAMRAM   = 0x01,
    DMA_SPIRAM   = 0x02,
    DMA_IDERAM   = 0x03,
    DMA_FILLRAM  = 0x04,
    DMA_RES2     = 0x05,
    DMA_BLT2RAM  = 0x06,
    DMA_RES3     = 0x07,
    DMA_RES4     = 0x08,
    DMA_BLT1RAM  = 0x09,
    DMA_RAMSPI   = 0x0A,
    DMA_RAMIDE   = 0x0B,
    DMA_RAMCRAM  = 0x0C,
    DMA_RAMSFILE = 0x0D,
    DMA_RES5     = 0x0E,
    DMA_RES6     = 0x0F
};

enum DMA_STATE
{
  DMA_ST_RAM,
  DMA_ST_BLT1,
  DMA_ST_BLT2,
  DMA_ST_SPI_R,
  DMA_ST_SPI_W,
  DMA_ST_IDE_R,
  DMA_ST_IDE_W,
  DMA_ST_FILL,
  DMA_ST_CRAM,
  DMA_ST_SFILE,
  DMA_ST_NOP,
  DMA_ST_INIT
};

typedef struct
{
  TASK_FUNCTION task;
} DMA_TASK;

enum DMA_DATA_STATE
{
  DMA_DS_READ,
  DMA_DS_BLIT,
  DMA_DS_WRITE
};

enum TS_STATE
{
  TSS_TMAP_READ,
  TSS_TILE_RENDER,
  TSS_SPR_RENDER,
  TSS_INIT,
  TSS_NOP
};

enum TS_PWRUP
{
  TS_PWRUP_ON  = 0x40,
  TS_PWRUP_OFF = 0x00
};

enum TS_VDAC
{
  TS_VDAC_OFF = 0x00,
  TS_VDAC_3   = 0x01,
  TS_VDAC_4   = 0x02,
  TS_VDAC_5   = 0x03,
  TS_VDAC2    = 0x07,
  TS_VDAC_MAX
};

typedef struct
{
  const char *name;    // displayed on config GUI
  int value;           // value from TS_VDAC:: enum
  const char *nick;    // used in *.ini
} TS_VDAC_NAME;

typedef struct
{
  INITIAL_FUNCTION init_task;
  TASK_FUNCTION task;
} TSU_TASK;

// Sprite Descriptor
typedef struct
{
	uint16_t y:9;
	uint16_t ys:3;
	uint16_t _0:1;
	uint16_t act:1;
	uint16_t leap:1;
	uint16_t yflp:1;
	uint16_t x:9;
	uint16_t xs:3;
	uint16_t _1:3;
	uint16_t xflp:1;
	uint16_t tnum:12;
	uint16_t pal:4;
} SPRITE_t;

// Tile Descriptor
typedef struct
{
	uint16_t tnum:12;
	uint16_t pal:2;
	uint16_t xflp:1;
	uint16_t yflp:1;
} TILE_t;

// TileMap description
typedef struct
{
	uint8_t line;
	uint8_t offset;
	int8_t pos_dir;
	uint8_t pal;
	TILE_t data;
} TMAP_t;

typedef union
{
	uint16_t w;
	struct
	{
		uint8_t b0;
		uint8_t b1;
	};
	struct
	{
		uint8_t n0:4;
		uint8_t n1:4;
		uint8_t n2:4;
		uint8_t n3:4;
	};
} BLT16;

typedef struct
{
// -- system --
	union
	{
		uint8_t sysconf;
		struct
		{
			uint8_t zclk:2;
			uint8_t cache:1;
			uint8_t ayclk:2;
			uint8_t _01:3;
		};
	};

	union
	{
		uint8_t cacheconf;
		struct
		{
			uint8_t cache_win0:1;
			uint8_t cache_win1:1;
			uint8_t cache_win2:1;
			uint8_t cache_win3:1;
		};
	};
	bool cache_miss;

	uint8_t hsint;
	union
	{
		uint16_t vsint:9;
		struct
		{
			uint8_t vsintl:8;
			uint8_t vsinth:1;
		};
	};

	union
	{
		uint8_t intmask;
		struct
		{
			uint8_t intframe:1;
			uint8_t intline:1;
			uint8_t intdma:1;
		};
	};

	uint8_t im2vect[8];

	struct
	{
		bool new_dma;			// generate new DMA INT
		uint32_t last_cput;		// last cpu tacts (used by frame INT)
		uint32_t frame_cnt;		// frame INT counter
		uint32_t frame_t;		// frame INT position int tacts
		uint32_t frame_len;		// frame INT len
		uint32_t line_t;		// line INT position int tacts
    
		union
		{
			uint8_t pend;
			struct
			{
				uint8_t frame_pend:1;
				uint8_t line_pend:1;
				uint8_t dma_pend:1;
			};
		};
	} intctrl;

	uint8_t fddvirt;
	uint8_t vdos;
	uint8_t vdos_m1;
	uint8_t pwr_up;
	uint8_t vdac;
	bool vdac2;

	// -- video --
	uint8_t vpage;
	uint8_t vpage_d;    // *_d - line-delayed
	uint8_t tmpage;
	uint8_t t0gpage[3];
	uint8_t t1gpage[3];
	uint8_t sgpage;
	uint8_t border;
	uint8_t g_yoffs_updated;

	union
	{
		uint8_t vconf;
		struct
		{
			uint8_t vmode:2;
			uint8_t ft_en:1;
			uint8_t gfxovr:1;
			uint8_t notsu:1;
			uint8_t nogfx:1;
			uint8_t rres:2;
		};
	};
	uint8_t vconf_d;

	union
	{
		uint8_t tsconf;
		struct
		{
			uint8_t t0ys_en:1;
			uint8_t t1ys_en:1;
			uint8_t t0z_en:1;
			uint8_t t1z_en:1;
			uint8_t z80_lp:1;
			uint8_t t0_en:1;
			uint8_t t1_en:1;
			uint8_t s_en:1;
		};
	};
	uint8_t tsconf_d;

	union
	{
		uint8_t palsel;
		struct
		{
			uint8_t gpal:4;
			uint8_t t0pal:2;
			uint8_t t1pal:2;
		};
	} ;
	uint8_t palsel_d;

	union
	{
		uint16_t g_xoffs:9;
		struct
		{
			uint8_t g_xoffsl:8;
			uint8_t g_xoffsh:1;
		};
	};

	union
	{
		uint16_t g_xoffs_d:9;
		struct
		{
			uint8_t g_xoffsl_d:8;
			uint8_t g_xoffsh_d:1;
		};
	};

	union
	{
		uint16_t g_yoffs:9;
		struct
		{
			uint8_t g_yoffsl:8;
			uint8_t g_yoffsh:1;
		};
	};

	union
	{
		uint16_t t0_xoffs:9;
		struct
		{
			uint8_t t0_xoffsl:8;
			uint8_t t0_xoffsh:1;
		};
	};
	uint16_t t0_xoffs_d;

	union
	{
		uint16_t t0_yoffs:9;
		struct
		{
			uint8_t t0_yoffsl:8;
			uint8_t t0_yoffsh:1;
		};
	};

	union
	{
		uint16_t t1_xoffs:9;
		struct
		{
			uint8_t t1_xoffsl:8;
			uint8_t t1_xoffsh:1;
		};
	};
	uint16_t t1_xoffs_d;

	union
	{
		uint16_t t1_yoffs:9;
		struct
		{
			uint8_t t1_yoffsl:8;
			uint8_t t1_yoffsh:1;
		};
	};

	// -- memory --
	uint8_t page[4];

	union
	{
		uint8_t memconf;
		struct
		{
			uint8_t rom128:1;  // unused - taken from the #7FFD
			uint8_t w0_we:1;
			uint8_t w0_map_n:1;
			uint8_t w0_ram:1;
			uint8_t _03:2;
			uint8_t lck128:2;  // 00 - lock512, 01 - lock128, 10 - auto (!a13), 11 - lock1024
		};
	};

	union
	{
		uint8_t fmaddr;
		struct
		{
			uint8_t fm_addr:4;
			uint8_t fm_en:1;
		};
	};

	// -- dma --
	uint8_t dmalen;
	uint8_t dmanum;

	union
	{
		// source address of dma transaction
		uint32_t saddr;
		struct
		{
			uint32_t saddrl:8;
			uint32_t saddrh:6;
			uint32_t saddrx:8;
		};
	};

	union
	{
		// destination address of dma transaction
		uint32_t daddr;
		struct
		{
			uint32_t daddrl:8;
			uint32_t daddrh:6;
			uint32_t daddrx:8;
		};
	};

	// dma controller state
	struct
	{
		union
		{
			uint8_t ctrl; // dma ctrl value
			struct
			{
				uint8_t dev:3;
				uint8_t asz:1;
				uint8_t d_algn:1;
				uint8_t s_algn:1;
				uint8_t opt:1;
				uint8_t rw:1;
			};
		};
		uint16_t len;
		uint16_t num;
		uint32_t saddr;      // source address of dma transaction
		uint32_t daddr;      // destination address of dma transaction
		uint32_t m1;         // mask 1 (used for address arithmetic)
		uint32_t m2;         // mask 2 (used for address arithmetic)
		uint32_t asize;      // align size
		uint16_t data;       // data (used by transactions based on state)
		uint8_t dstate;  // state of data (0 - empty, 1 - have data, 2 - have blitted data)
		uint8_t state;       // state of dma transaction
	} dma;

	// saved state
	struct
	{
		uint32_t saddr;      // source address of dma transaction
		uint32_t daddr;      // destination address of dma transaction
	} dma_saved;

	struct
	{
		uint32_t y;                // Y coordinate of graphic layers
		uint8_t tnum;              // Tile number for render
		uint8_t tmax;              // Max Tile number for render
		uint8_t tpal;              // Prepared Tile palette selector
		uint8_t pal;               // Prepared Tile palette
		uint16_t pos;              // Position in line buffer
		uint16_t next_pos;         // Next position in line buffer
		uint16_t pos_dir;          // Direction of rendering to buffer
		uint8_t line;              // Number of rendering line in tiles
		uint8_t gpage;             // Graphic Page
		uint8_t gsize;             // Size of graphic iterations
		uint8_t tz_en;             // Enable rendering Tile with number 0
		uint8_t *gptr;             // Pointer to the Tile graphic
		TMAP_t *tmbptr;       // Pointer to the TileMap buffer
		bool leap;            // Flag of last sprite in current layer
		uint8_t snum;              // Number of Sprite
		TMAP_t tm;            // TileMap data for render
		SPRITE_t spr;         // Sprite data for render

		TILE_t *tmap[2];      // TileMap pointers for layers
		uint8_t tmsize;            // Number of elements which need read from TileMap for current layer
		uint16_t tmbpos[2];        // Position in TileMap buffer for each layer
		TMAP_t tmbuf[512];    // TileMap buffer

		uint8_t state;             // TSU state
		uint8_t prev_state;        // Previous TSU state
		uint8_t layer;             // Active layer (0, 1, 2, etc)
		bool tmap_read;       // Flag for read TileMap in current line
		bool render;          // Flag for render graphic in current line

		struct
		{
			uint8_t t0;
			uint8_t t1;
			uint8_t s;
			uint8_t gfx;
		} toggle;         // TSU layers toggle
	} tsu;
} TSPORTS_t;
