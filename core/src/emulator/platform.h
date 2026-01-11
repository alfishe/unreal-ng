#pragma once
#include "stdafx.h"

#include "sysdefs.h"
#include "emulator/platforms/tsconf/tsconf.h"

#define EMUL_DEBUG
#define TRASH_PAGE

// region <Notification center events>
constexpr char const* NC_LOGGER_SETTINGS_MODULES_CHANGE = "LOGGER_SETTINGS_MODULES";
constexpr char const* NC_LOGGER_SETTINGS_SUBMODULES_CHANGE = "LOGGER_SETTINGS_SUBMODULES";


// region <GLOBAL MessageCenter Notifications>

// These events are broadcast globally and do not carry emulator instance identification.
// Subscribe ONCE in application/component constructor, not per-emulator-instance.
// Examples: MainWindow constructor, automation listeners.
constexpr char const* NC_EMULATOR_INSTANCE_CREATED = "EMULATOR_INSTANCE_CREATED"; // New emulator instance created (payload: emulator ID)
constexpr char const* NC_EMULATOR_INSTANCE_DESTROYED = "EMULATOR_INSTANCE_DESTROYED"; // Emulator instance destroyed (payload: emulator ID)
constexpr char const* NC_EMULATOR_SELECTION_CHANGED = "EMULATOR_SELECTION_CHANGED";  // Emulator selection changed (payload: EmulatorSelectionPayload)

// endregion </GLOBAL MessageCenter Notifications>

// region <PER-EMULATOR-INSTANCE MessageCenter Notifications>

// These events are broadcast BY specific emulator instances during their lifecycle.
// When multiple emulators exist, all listeners receive events from ALL instances.
// Listeners must filter events by emulator ID (if payload supports it) or by comparing
// against their adopted emulator reference to avoid processing events from wrong instances.
// Subscribe when adopting an emulator, unsubscribe when releasing it.
constexpr char const* NC_EMULATOR_STATE_CHANGE = "LOGGER_EMULATOR_STATE";       // Emulator state change (Initialized/Run/Pause/Resume/Stop)
constexpr char const* NC_SYSTEM_RESET = "RESET";                                // System reset event
constexpr char const* NC_EXECUTION_CPU_STEP = "CPU_STEP";                       // Single CPU step executed (debug mode only)
constexpr char const* NC_EXECUTION_BREAKPOINT = "BREAKPOINT";                   // Breakpoint triggered

constexpr char const* NC_AUDIO_FRAME_REFRESH = "AUDIO_FRAME_REFRESH";           // Audio frame ready (TODO: add emulator ID to payload)
constexpr char const* NC_VIDEO_FRAME_REFRESH = "VIDEO_FRAME_REFRESH";           // Video frame ready (payload: EmulatorFramePayload with ID)
constexpr char const* NC_AUDIO_BUFFER_HALF_FULL = "AUDIO_BUFFER_HALF_FULL";     // Audio buffer < 50% full

constexpr char const* NC_FDD_MOTOR_STARTED = "FDD_MOTOR_START";                 // Floppy drive motor started
constexpr char const* NC_FDD_MOTOR_STOPPED = "FDD_MOTOR_STOP";                  // Floppy drive motor stopped

constexpr char const* NC_FILE_OPEN_REQUEST = "FILE_OPEN_REQUEST";               // File open request from emulator

constexpr char const* NC_BREAKPOINT_CHANGED = "BREAKPOINT_CHANGED";             // Breakpoints added, removed, or modified
constexpr char const* NC_LABEL_CHANGED = "LABEL_CHANGED";                       // Labels added, removed, or modified

// endregion </PER-EMULATOR-INSTANCE MessageCenter Notifications>

// endregion </Notification center events>

// region <Logging / Tracing data>

// High level modules
enum PlatformModulesEnum : uint8_t
{
    MODULE_NONE                 = 0,
    MODULE_CORE                 = 1,
    MODULE_Z80                  = 2,
    MODULE_MEMORY               = 3,
    MODULE_IO                   = 4,
    MODULE_DISK                 = 5,
    MODULE_VIDEO                = 6,
    MODULE_SOUND                = 7,
    MODULE_DMA                  = 8,
    MODULE_LOADER               = 9,
    MODULE_DEBUGGER             = 10,
    MODULE_DISASSEMBLER         = 11,
    MODULE_RECORDING            = 12,

    MODULE_ALL                  = 0xFF
};

enum PlatformCoreSubmodulesEnum : uint16_t
{
    SUBMODULE_CORE_NONE         = 0x0000,
    SUBMODULE_CORE_GENERIC      = 0x0001,
    SUBMODULE_CORE_CONFIG       = 0x0002,
    SUBMODULE_CORE_FILES        = 0x0004,
    SUBMODULE_CORE_COUNTERS     = 0x0008,
    SUBMODULE_CORE_MAINLOOP     = 0x0010,

    SUBMODULE_CORE_ALL          = 0xFFFF
};

enum PlatformZ80SubmodulesEnum : uint16_t
{
    SUBMODULE_Z80_NONE          = 0x0000,
    SUBMODULE_Z80_GENERIC       = 0x0001,
    SUBMODULE_Z80_M1            = 0x0002,
    SUBMODULE_Z80_CALLS         = 0x0004,
    SUBMODULE_Z80_JUMPS         = 0x0008,
    SUBMODULE_Z80_INTERRUPTS    = 0x0010,
    SUBMODULE_Z80_BIT           = 0x0020,
    SUBMODULE_Z80_ARITHMETICS   = 0x0040,
    SUBMODULE_Z80_STACK         = 0x0080,
    SUBMODULE_Z80_REGISTERS     = 0x0100,
    SUBMODULE_Z80_IO            = 0x0200,

    SUBMODULE_Z80_ALL           = 0xFFFF,
};

enum PlatformMemorySubmodulesEnum : uint16_t
{
    SUBMODULE_MEM_NONE          = 0x0000,
    SUBMODULE_MEM_GENERIC       = 0x0001,
    SUBMODULE_MEM_ROM           = 0x0002,
    SUBMODULE_MEM_RAM           = 0x0004,

    SUBMODULE_MEM_ALL           = 0xFFFF
};

enum PlatformIOSubmodulesEnum: uint16_t
{
    SUBMODULE_IO_NONE           = 0x0000,
    SUBMODULE_IO_GENERIC        = 0x0001,
    SUBMODULE_IO_IN             = 0x0002,
    SUBMODULE_IO_OUT            = 0x0004,
    SUBMODULE_IO_KEYBOARD       = 0x0008,
    SUBMODULE_IO_TAPE           = 0x0010,
    SUBMODULE_IO_KEMPSTON_JOY   = 0x0020,
    SUBMODULE_IO_KEMPSTON_MOUSE = 0x0040,

    SUBMODULE_IO_ALL            = 0xFFFF
};

enum PlatformDiskSubmodulesEnum : uint16_t
{
    SUBMODULE_DISK_NONE         = 0x0000,
    SUBMODULE_DISK_GENERIC      = 0x0001,
    SUBMODULE_DISK_FDC          = 0x0002,
    SUBMODULE_DISK_HDD          = 0x0004,

    SUBMODULE_DISK_ALL          = 0xFFFF
};

enum PlatformSoundSubmodulesEnum : uint16_t
{
    SUBMODULE_SOUND_NONE            = 0x0000,
    SUBMODULE_SOUND_GENERIC         = 0x0001,
    SUBMODULE_SOUND_BEEPER          = 0x0002,
    SUBMODULE_SOUND_AY              = 0x0004,
    SUBMODULE_SOUND_TURBOSOUND      = 0x0008,
    SUBMODULE_SOUND_TURBOSOUNDFM    = 0x0010,
    SUBMODULE_SOUND_GS              = 0x0028,
    SUBMODULE_SOUND_MOONSOUND       = 0x0040,
    SUBMODULE_SOUND_SAA             = 0x0080,

    SUBMODULE_SOUND_ALL             = 0xFFFF
};

enum PlatformVideoSubmodulesEnum : uint16_t
{
    SUBMODULE_VIDEO_NONE        = 0x0000,
    SUBMODULE_VIDEO_GENERIC     = 0x0001,
    SUBMODULE_VIDEO_ULA         = 0x0002,
    SUBMODULE_VIDEO_ULAPLUS     = 0x0004,
    SUBMODULE_VIDEO_MISC        = 0x0008,
    SUBMODULE_VIDEO_NEXT        = 0x0010,
    SUBMODULE_VIDEO_PROFI       = 0x0020,
    SUBMODULE_VIDEO_ATM         = 0x0040,
    SUBMODULE_VIDEO_TSCONF      = 0x0080,

    SUBMODULE_VIDEO_ALL         = 0xFFFF
};

enum PlatformDMASubmodulesEnum : uint16_t
{
    SUBMODULE_DMA_NONE          = 0x0000,
    SUBMODULE_DMA_NEXT          = 0x0001,
    SUBMODULE_DMA_TSCONF        = 0x0002,

    SUBMODULE_DMA_ALL           = 0xFFFF
};

enum PlatformLoaderSubmodulesEnum : uint16_t
{
    SUBMODULE_LOADER_NONE       = 0x0000,
    SUBMODULE_LOADER_SNA        = 0x0001,
    SUBMODULE_LOADER_Z80        = 0x0002,

    SUBMODULE_LOADER_ALL        = 0xFFFF
};

enum PlatformDebuggerSubmodulesEnum : uint16_t
{
    SUBMODULE_DEBUG_NONE         = 0x0000,
    SUBMODULE_DEBUG_GENERIC      = 0x0001,
    SUBMODULE_DEBUG_BREAKPOINTS  = 0x0002,
    SUBMODULE_DEBUG_LABELS       = 0x0004,
    SUBMODULE_DEBUG_WATCHES      = 0x0008,
    SUBMODULE_DEBUG_CPUTRACE     = 0x0010,
    SUBMODULE_DEBUG_MEMORYTRACE  = 0x0020,
    SUBMODULE_DEBUG_IOTRACE      = 0x0040,

    SUBMODULE_DEBUGGER_ALL          = 0xFFFF
};

enum PlatformDisassemblerSubmodulesEnum : uint16_t
{
    SUBMODULE_DISASSEMBLER_NONE     = 0x0000,
    SUBMODULE_DISASSEMBLER_CORE     = 0x0001,

    SUBMODULE_DISASSEMBLER_ALL      = 0xFFFF
};

enum PlatformRecordingSubmodulesEnum : uint16_t
{
    SUBMODULE_RECORDING_NONE        = 0x0000,
    SUBMODULE_RECORDING_MANAGER     = 0x0001,
    SUBMODULE_RECORDING_ENCODER     = 0x0002,

    SUBMODULE_RECORDING_ALL         = 0xFFFF
};

// endregion </Logging / Tracing data>

// region <CPU runtime>

// endregion </CPU runtime>

#undef PAGE_SIZE
const uint16_t PAGE_SIZE = 0x4000U;		// Spectrum memory page size is 16Kb (0x4000 or 16384)

const uint16_t MAX_RAM_PAGES = 256;     // 4Mb RAM
const uint16_t MAX_CACHE_PAGES = 2;     // 32K cache
const uint16_t MAX_MISC_PAGES = 1;      // trash page
const uint16_t MAX_ROM_PAGES = 64;      // 1Mb

// TS-conf specific settings
#define TS_CACHE_SIZE 512

#define GS4MB //0.37.0
#ifdef MOD_GSZ80
	#define MAX_GSROM_PAGES  32U      // 512Kb
	#ifdef GS4MB
		#define MAX_GSRAM_PAGES 256U     // for gs4mb
	#else
		#define MAX_GSRAM_PAGES 30U      // for gs512 (last 32k unusable)
	#endif
#else
	#define MAX_GSROM_PAGES 0
	#define MAX_GSRAM_PAGES 0
#endif

constexpr uint16_t MAX_PAGES = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES);
constexpr uint16_t MAX_PERIPHERAL_PAGES = (MAX_GSRAM_PAGES + MAX_GSROM_PAGES);

const size_t RAM_OFFSET = 0;
constexpr size_t CACHE_OFFSET = RAM_OFFSET + MAX_RAM_PAGES * PAGE_SIZE;
constexpr size_t MISC_OFFSET = CACHE_OFFSET + MAX_CACHE_PAGES * PAGE_SIZE;
constexpr size_t ROM_OFFSET = MISC_OFFSET + MAX_MISC_PAGES * PAGE_SIZE;

// Special memory page to collect writes to ROM
constexpr size_t TRASH_MEMORY_OFFSET =  MISC_OFFSET;

// Byte-size for RGBA pixel
#define RGBA_SIZE 4

#ifdef MOD_GSZ80
	#define ROM_GS_M    (ROM_BASE_M + MAX_ROM_PAGES * PAGE)
	#define GSRAM_M     (ROM_GS_M + MAX_GSROM_PAGES * PAGE)
#endif

enum IDE_SCHEME
{
	IDE_NONE = 0,
	IDE_ATM,
	IDE_NEMO, IDE_NEMO_A8, IDE_NEMO_DIVIDE,
	IDE_SMUC,
	IDE_PROFI,
	IDE_DIVIDE,
};

enum MOUSE_WHEEL_MODE { MOUSE_WHEEL_NONE, MOUSE_WHEEL_KEYBOARD, MOUSE_WHEEL_KEMPSTON }; //0.36.6 from 0.35b2

enum MEM_MODEL : uint8_t
{
	MM_PENTAGON = 0,    	// Pentagon 128/256/512/1024K
	MM_SPECTRUM48,          // Spectrum 48K
	MM_SPECTRUM128,         // Spectrum 128K/+2A
    MM_PLUS3,   			// ZX Spectrum +2B/+3
	MM_TSL,		    		// TSConf
	MM_ATM3,    			// ATM Turbo 3.0
	MM_ATM710,  			// ATM Turbo 7.1.0
	MM_ATM450,  			// ATM Turbo 4.5.0 (512/1024)
	MM_PROFI,   			// Profi 1024K
	MM_SCORP,   			// Scorpion ZS256
	MM_PROFSCORP,   		// Scorpion ZS256 + ProfROM
	MM_GMX, 				// GMX
	MM_KAY,     			// Kay 1024
	MM_QUORUM,  			// Quorum
	MM_LSY256,  			// LSY256
	MM_PHOENIX,             // Phoenix
	MM_NEXT,                // ZX Next
	N_MM_MODELS             // <End of enumeration>
};

const int RAM_48 = 48, RAM_128 = 128, RAM_256 = 256, RAM_512 = 512, RAM_1024 = 1024, RAM_2048 = 2048, RAM_4096 = 4096;

struct TMemModel
{
	const char *FullName;
	const char *ShortName;
	MEM_MODEL Model;
	unsigned defaultRAM;
	unsigned AvailRAMs;
};

typedef void(*VOID_FUNC)(void);

struct BORDSIZE
{
	const char *name;
	const uint32_t x;
	const uint32_t y;
	const uint32_t xsize;
	const uint32_t ysize;
};

typedef void(*RENDER_FUNC)(uint8_t *, uint32_t);
struct RENDER
{
	const char *name;	// displayed on config GUI
	RENDER_FUNC func;	// called function
	const char *nick;	// used in *.ini
	unsigned flags;		// used 
};

typedef void(*DRIVER_FUNC)(void);
struct DRIVER
{
	const char *name;	// displayed on config GUI
	DRIVER_FUNC func;	// called function
	const char *nick;	// used in *.ini
	unsigned flags;		// used 
};

typedef void(*DRAWER_FUNC)(int);
struct DRAWER
{
	DRAWER_FUNC func;
};

struct IDE_CONFIG
{
	char image[512];
	unsigned c, h, s, lba;
	uint8_t readonly;
	uint8_t cd;
};

enum RSM_MODE
{
	RSM_SIMPLE, RSM_FIR0, RSM_FIR1, RSM_FIR2
};

enum SSHOT_FORMAT
{
	SS_SCR = 0, SS_BMP = 1, SS_PNG = 2, SS_GIF = 3, SS_LAST
};

enum ULAPLUS
{
	UPLS_TYPE1 = 0,
	UPLS_TYPE2,
	UPLS_NONE
};

struct zxkeymap;

struct CONFIG
{
	unsigned t_line;	// t-states per line
	unsigned frame;		// t-states per frame
	uint8_t intfq;		// INT interrupt frequency (in Hz). Typically 50Hz
	unsigned intstart;	// INT pulse start position (delay in clock cycles)
	unsigned intlen;	// Duration of INT signal (for Z80) in clock cycles. Should be no less than 23, since some IX/IY instructions take so long to execute and interrupt is handled only at the end of such instruction execution
	unsigned nopaper;	// Hide paper

	unsigned render, driver, fontsize;

	unsigned soundbuffer, refresh;

	uint8_t flashcolor, noflic, fast_sl, alt_nf;
	uint8_t frameskip, frameskipmax;
	uint8_t flip, fullscr;

	uint8_t lockmouse;
	uint8_t detect_video;
	uint8_t tape_traps;
	uint8_t ulaplus;
	uint8_t tape_autostart;
	SSHOT_FORMAT scrshot;
	char scrshot_path[FILENAME_MAX];

	uint8_t ch_size;
	uint8_t EFF7_mask;
	uint8_t reset_rom;
	uint8_t use_romset = false;
	uint8_t spg_mem_init;

	uint8_t updateb, bordersize;
	unsigned framex;
	unsigned framexsize;
	unsigned framey;
	unsigned frameysize;
	uint8_t even_M1, border_4T;

	uint8_t floatbus, floatdos;
	bool portff;

	/// Speed multiplier: 1x (default), 2x, 4x, 8x, or 16x
	/// Multiplies the number of t-states executed per frame
	uint8_t speed_multiplier = 1;

	/// Turbo/Max speed mode - runs emulation as fast as possible
	/// When enabled, disables audio synchronization for maximum performance
	bool turbo_mode = false;

	/// Generate audio samples even in turbo mode
	/// Useful for video capture or when recording while in turbo mode
	/// Audio will play at increased pitch due to faster execution
	bool turbo_mode_audio = false;

	int modem_port; //, modem_scheme;
	int zifi_port;
	uint8_t fdd_noise;				// Mix FDD disk rotation and positioning noise to sound channel(s)

	bool trdos_present;			// Enable Beta128 disk interface (TR-DOS)
	uint8_t trdos_interleave;
	bool trdos_traps;			// Use TR-DOS traps
	bool wd93_nodelay;			// Don't emulate WD1793 / VG93 controller delays
	uint8_t trdos_wp[4];

	uint8_t cache;
	uint8_t cmos;
	uint8_t smuc;
	uint8_t ula_preset;

	uint8_t gs_type;
	uint8_t pixelscroll;
	uint8_t sleepidle;
	uint8_t rsrvd1_;
	uint8_t ConfirmExit;

	uint8_t highpriority;
	uint8_t videoscale;

	MEM_MODEL mem_model;
	uint32_t ramsize;
	uint32_t romsize;

	IDE_SCHEME ide_scheme;
	IDE_CONFIG ide[2];
	uint8_t ide_skip_real;
	uint8_t cd_aspi;

	uint32_t sd_delay;

	uint8_t soundfilter; //Alone Coder (IDC_SOUNDFILTER)
	uint8_t RejectDC;

	struct
	{
		unsigned fq, ayfq, saa1099fq;
		int covoxFB, covoxDD, sd, saa1099, moonsound;
		int beeper_vol, micout_vol, micin_vol, ay_vol, aydig_vol, saa1099_vol;
		int covoxFB_vol, covoxDD_vol, sd_vol, covoxProfi_vol;
		int gs_vol, bass_vol, moonsound_vol;
		VOID_FUNC do_sound;
		uint8_t enabled, gsreset, dsprimary;
		uint8_t ay_chip, ay_scheme, ay_stereo, ay_vols, ay_samples;
		unsigned ay_stereo_tab[6], ay_voltab[32];
	} sound;

	struct
	{
		unsigned firenum;
		uint8_t altlock, fire, firedelay;
		uint8_t paste_hold, paste_release, paste_newline;
		uint8_t mouse, mouseswap, kjoy, keymatrix, joymouse;
		uint8_t keybpcmode;
		char mousescale;
		uint8_t mousewheel; // enum MOUSE_WHEEL_MODE //0.36.6 from 0.35b2
		zxkeymap *active_zxk;
		unsigned JoyId;
	} input;

	struct
	{
		uint8_t enabled;
		uint8_t status;
		uint8_t flash_ay_kbd;
		uint8_t perf_t;
		uint8_t reserved1;
		unsigned bandBpp;
#define NUM_LEDS 7
		unsigned ay;
		unsigned perf;
		unsigned load;
		unsigned input;
		unsigned time;
		unsigned osw;
		unsigned memband;
	} led;

	struct
	{
		uint8_t mem_swap;
		uint8_t xt_kbd;
		uint8_t reserved1;
	} atm;

	uint8_t use_comp_pal;
	unsigned pal, num_pals;      // selected palette and total number of pals
	unsigned minres;             // min. screen x-resolution
	unsigned scanbright;         // scanlines intensity

	struct
	{
		uint8_t mix_frames;
		uint8_t mode; // RSM_MODE
	} rsm;

    std::string romSetName;
    std::string romSet128Path;
    std::string romSetSOSPath;
    std::string romSetDOSPath;
    std::string romSetSYSPath;

	char sos_rom_path[FILENAME_MAX];
	char dos_rom_path[FILENAME_MAX];
    char sys_rom_path[FILENAME_MAX];
    char zx48_rom_path[FILENAME_MAX];
	char zx128_rom_path[FILENAME_MAX];
    char plus3_rom_path[FILENAME_MAX];
	char pent_rom_path[FILENAME_MAX];
	char atm1_rom_path[FILENAME_MAX];
	char atm2_rom_path[FILENAME_MAX];
	char atm3_rom_path[FILENAME_MAX];
	char scorp_rom_path[FILENAME_MAX];
	char prof_rom_path[FILENAME_MAX];
	char gmx_rom_path[FILENAME_MAX];
	char profi_rom_path[FILENAME_MAX];
	char kay_rom_path[FILENAME_MAX];
	char quorum_rom_path[FILENAME_MAX];
	char tsl_rom_path[FILENAME_MAX];
	char lsy_rom_path[FILENAME_MAX];
	char phoenix_rom_path[FILENAME_MAX];

#ifdef MOD_GSZ80
	unsigned gs_ramsize;
	char gs_rom_path[FILENAME_MAX];
#endif

	char moonsound_rom_path[FILENAME_MAX];

#ifdef MOD_MONITOR
	char sos_labels_path[FILENAME_MAX];
#endif

	char ngs_sd_card_path[FILENAME_MAX];

	uint8_t zc;
	char zc_sd_card_path[FILENAME_MAX];

	char atariset[64]; // preset for atari
	char keymap_name[64]; // name of ZX keys map
	char keyset[64]; // short name of keyboard layout
	char appendboot[FILENAME_MAX];
	char workdir[FILENAME_MAX];
	uint8_t profi_monochrome;

	/*
	struct
	{
		char exec[VS_MAX_FFPATH];  // ffmpeg path/name
		char parm[VS_MAX_FFPARM];  // enc. parameters for ffmpeg
		char vout[VS_MAX_FFVOUT];  // output video file name
		uint8_t newcons;                // open new console for ffmpeg
	} ffmpeg;
	*/
};

struct TEMP
{
	unsigned rflags;    // render_func flags
	unsigned border_add, border_and;   // for scorpion 4T border update
	uint8_t *base, *base_2;  // pointers to Spectrum screen memory
	uint8_t rom_mask, ram_mask;
	uint8_t evenM1_C0; // C0 for scorpion, 00 for pentagon
	uint8_t hi15; // 0 - 16bit color, 1 - 15bit color, 2 - YUY2 colorspace
	unsigned snd_frame_samples;  // samples / frame
	unsigned snd_frame_ticks;    // sound ticks / frame
	unsigned cpu_t_at_frame_start;

	unsigned gx, gy, gdx, gdy; // updating rectangle (used by GDI renderer)
	//RECT client;               // updating rectangle (used by DD_blt renderer)
	bool Minimized;            // window is minimized
	//HDC gdidc;
	unsigned ox, oy, obpp, ofq; // destination video format
	unsigned scx, scy; // multicolor area (320x240 or 384x300), used in MCR renderer
	unsigned odx, ody; // offset to border in surface, used only in flip()
	unsigned rsx, rsy; // screen resolution
	unsigned b_top, b_left, b_right, b_bottom; // border frame used to setup MCR
	unsigned scale; // window scale (x1, x2, x3, x4)
	unsigned mon_scale; // window scale in monitor mode(debugger)

	uint8_t fm_tmp;			// temporary reg for FMAPS writes

	unsigned ataricolors[0x100];
	unsigned shift_mask; // for 16/32 bit modes masks low bits of color components

	struct
	{
		// led coords
		uint32_t *ay;
		uint32_t *perf;
		uint32_t *load;
		uint32_t *input;
		uint32_t *time;
		uint32_t *osw;
		uint32_t *memband;
		uint32_t *fdd;

		int64_t tape_started;
	} led;

	uint8_t profrom_mask;
	uint8_t comp_pal_changed;

	uint8_t vidblock, sndblock, inputblock, frameskip;
	bool Gdiplus;
	unsigned gsdmaaddr;
	uint8_t gsdmaon;
	uint8_t gs_ram_mask;

	uint8_t offset_vscroll;
	uint8_t offset_vscroll_prev;
	uint8_t offset_hscroll;
	uint8_t offset_hscroll_prev;

	char RomDir[FILENAME_MAX];
	char SnapDir[FILENAME_MAX];
	char HddDir[FILENAME_MAX];
};

#ifndef _WIN32
typedef struct tagRECT
{
	uint16_t left;
	uint16_t top;
	uint16_t right;
	uint16_t bottom;
} RECT;
#endif

struct HOST
{
    char cpu_model[64];          // CPU model unique name

	// Host CPU features
	uint8_t mmx, sse, sse2;
	uint64_t cpufq;		     // x86 t-states per second
	unsigned ticks_frame;	 // x86 t-states in frame
};

extern unsigned sb_start_frame;

enum AY_SCHEME
{
	AY_SCHEME_NONE = 0,
	AY_SCHEME_SINGLE,
	AY_SCHEME_PSEUDO,
	AY_SCHEME_QUADRO,
	AY_SCHEME_POS,
	AY_SCHEME_CHRV,
	AY_SCHEME_AYX32,
	AY_SCHEME_MAX
};

/*
#include "io/wd93/wd93.h"
#include "io/hdd/hddio.h"
#include "io/hdd/hdd.h"
#include "input.h"
#include "io/zf232/zf232.h"

#if defined(MOD_GSZ80) || defined(MOD_GSBASS)
#include "sound\bass.h"
#include "sound\snd_bass.h"
#endif

#ifdef MOD_GSBASS
#include "sound\gs\gshlbass.h"
#include "sound\gs\gshle.h"
#endif
*/

#define EFF7_4BPP       0x01
#define EFF7_512        0x02
#define EFF7_LOCKMEM    0x04
#define EFF7_ROCACHE    0x08
#define EFF7_GIGASCREEN 0x10	// It is also Turbo bit0 at ATM3 (tsl)
#define EFF7_HWMC       0x20
#define EFF7_384        0x40
#define EFF7_CMOS       0x80

#define aFE_16          0x00
#define aFE_MC          0x01

#define FF77_16         0x00
#define FF77_MC         0x02
#define FF77_ZX         0x03
#define FF77_TX         0x06
#define FF77_TL         0x07

// ���� ����� 00 ��� �������
static const uint8_t Q_F_RAM = 0x01;
static const uint8_t Q_RAM_8 = 0x08;
static const uint8_t Q_B_ROM = 0x20;
static const uint8_t Q_BLK_WR = 0x40;
static const uint8_t Q_TR_DOS = 0x80;

enum SNAP
{
	snNOFILE, snUNKNOWN, snTOOLARGE,
	snSP, snZ80, snSNA_48, snSNA_128, snSPG,
	snTAP, snTZX, snCSW,
	snHOB, snSCL, snTRD, snFDI, snTD0, snUDI, snISD, snPRO
};

struct NVRAM
{
	enum EEPROM_STATE { IDLE = 0, RCV_CMD, RCV_ADDR, RCV_DATA, SEND_DATA, RD_ACK };
	unsigned address;
	uint8_t datain, dataout, bitsin, bitsout;
	uint8_t state;
	uint8_t prev;
	uint8_t out;
	uint8_t out_z;

	void MemoryWrite(uint8_t val);
};

struct EmulatorState
{
    /// region <Counters>

    uint64_t t_states;      // Cumulative T-State counter. Updated after each frame!!! Used for peripheral synchronization
    uint64_t frame_counter; // Counting each video frame displayed

    /// endregion </Counters>

    /// region <Runtime CPU parameters>

    // Example:
    // base_z80_frequency = 3'500'000;          - Base Z80 frequency is 3.5MHz
    // current_z80_frequency = 14'000'000;      - Current Z80 frequency is 14Mhz
    // current_z80_frequency_multiplier = 4;    - Current frequency multiplier is x4
    uint32_t base_z80_frequency;                // x1 base CPU clock generator (in Hz)
    uint32_t current_z80_frequency;             // xN CPU clock generator (in Hz)
    uint8_t current_z80_frequency_multiplier;   // Frequency multiplier comparing to CPU base
    uint8_t next_z80_frequency_multiplier;      // Queued multiplier to apply at next frame start (prevents mid-frame changes)

    /// endregion </Runtime CPU parameters


    /// region <Port state>
	uint8_t p7FFD, pFE, pEFF7, pXXXX;   // Common ports
	uint8_t pBFFD, pFFFD;               // AY sound-specific
	uint8_t pDFFD, pFDFD, p1FFD, pFF77; // Models with extended memory-specific
	uint8_t p7EFD, p78FD, p7AFD, p7CFD, gmx_config, gmx_magic_shift; // GMX specific
	uint8_t p00, p80FD; // Quorum specific
	/// endregion </Port state>

	/// region <Access flags>
	bool video_memory_changed;  // [Debug mode only] Indicates if video memory was changed
	/// endregion </Access flags>

	bool nmi_in_progress = false;
	
	TSPORTS_t ts;
	
	uint8_t pLSY256;
	uint16_t cram[256];
	uint16_t sfile[256];

	uint8_t aFE, aFB; // ATM 4.50 system ports
	unsigned pFFF7[8]; // ATM 7.10 / ATM3(4Mb) memory map
	// |7ffd|rom|b7b6|b5..b0| b7b6 = 0 for atm2

	uint8_t wd_shadow[4]; // 2F, 4F, 6F, 8F

	unsigned aFF77;
	unsigned active_ay;
	// ATM3
	union
	{
		uint16_t pBD;
		struct
		{
			uint8_t pBDl;
			uint8_t pBDh;
		};
	};
	uint8_t pBE, pBF;

	uint8_t flags = 0x00; // Stores execution flags
	uint8_t border_attr;
	uint8_t cmos_addr;
	uint8_t pVD;

#ifdef MOD_VID_VD
	uint8_t *vdbase;
#endif

	uint8_t pFFBA, p7FBA; // SMUC
	uint8_t res1, res2;

	uint8_t p0F, p1F, p4F, p5F; // soundrive
	
	//struct WD1793 wd;
	
	struct NVRAM nvram;
	struct
	{
		uint64_t edge_change;
		uint8_t *play_pointer; // or NULL if tape stopped
		uint8_t *end_of_tape;  // where to stop tape
		unsigned index;    // current tape block
		unsigned tape_bit;
		//      SNDRENDER sound; //Alone Coder
	} tape;
	
	uint8_t comp_pal[0x10];
	uint8_t ulaplus_cram[64];
	uint8_t ulaplus_mode;
	uint8_t ulaplus_reg;
	uint8_t ide_hi_byte_r, ide_hi_byte_w, ide_hi_byte_w1, ide_read, ide_write; // high byte in IDE i/o
	uint8_t profrom_bank;
};

// bits for State::flags
#define CF_DOSPORTS     0x01    // tr-dos ports are accessible
#define CF_TRDOS        0x02    // DOSEN trigger
#define CF_SETDOSROM    0x04    // tr-dos ROM become active at #3Dxx
#define CF_LEAVEDOSRAM  0x08    // DOS ROM will be closed at executing RAM
#define CF_LEAVEDOSADR  0x10    // DOS ROM will be closed at pc>#4000
#define CF_CACHEON      0x20    // cache active
#define CF_Z80FBUS      0x40    // unstable data bus
#define CF_PROFROM      0x80    // PROF-ROM active

// LSY256 - BarmaleyM's Orel' extension
#define PF_DV0			0x01	// RAM r/w at #0000 page selector
#define PF_BLKROM		0x02	// RAM/!ROM at #0000
#define PF_EMUL			0x08	// page mode at #0000
#define PF_PA3			0x10	// page bit3 at #C000

#define TAPE_QUANTUM 64
struct tzx_block
{
	uint8_t *data;
	unsigned datasize;    // data size, in bytes
	unsigned pause;
	union
	{
		struct
		{
			unsigned pilot_t, s1_t, s2_t, zero_t, one_t;
			unsigned pilot_len;
		};
		struct
		{
			unsigned numblocks, numpulses;
		};
		unsigned param;
	};

	uint8_t type; // 0-playable, 1-pulses, 10-20 - info, etc...
	uint8_t crc; // xor of all bytes
	char desc[128];
};

struct SNDVAL
{
	union
	{
		unsigned data;
		struct
		{
			short left, right;
		};
	};
};

struct virtkeyt
{
	const char *name;
	uint16_t di_key, virtkey;
};

struct keyports
{
	volatile uint8_t *port1, *port2;
	uint8_t mask1, mask2;
};

struct zxkey
{
	const char *name;
	volatile uint8_t * /*const*/ port; //Alone Coder
	/*const*/ uint8_t mask; //Alone Coder
};

struct zxkeymap1
{
	const char *name;
	zxkey *zxk;
	unsigned zxk_size;
};

struct action
{
	const char *name;
	void(*func)();
	uint16_t k1, k2, k3, k4;
};

// video overlay 
typedef union
{
	uint32_t p;
	struct
	{
		uint8_t b;
		uint8_t g;
		uint8_t r;
		uint8_t a;
	};
} RGB32;

// flags for video filters
								// misc options
#define RF_BORDER   0x00000002   // no multicolor painter, MemoryRead directly from spectrum memory
#define RF_MON      0x00000004   // not-flippable surface, show mouse cursor
#define RF_DRIVER   0x00000008   // use options from driver
//#define RF_VSYNC    0x00000010   // force VSYNC
#define RF_D3D      0x00000010   // use d3d for windowed mode
#define RF_GDI      0x00000020   // output to window
#define RF_CLIP     0x00000040   // use DirectDraw clipper for windowed mode
#define RF_OVR      0x00000080   // output to overlay

								 // main area size
#define RF_1X       0x00000000   // 256x192,320x240 with border (default)
#define RF_2X       0x00000100   // default x2
#define RF_3X       0x00000001   // default x3
#define RF_4X       0x00000200   // default x4
#define RF_64x48    0x00000400   // 64x48  (for chunky 4x4)
#define RF_128x96   0x00000800   // 128x96 (for chunky 2x2)

#define RF_8        0x00000000   // 8 bit (default)
#define RF_8BPCH    0x00001000   // 8-bit per color channel. GDI mode => 32-bit surface. 8-bit mode => grayscale palette
#define RF_YUY2     0x00002000   // pixel format: 16bit, YUY2
#define RF_16       0x00004000   // hicolor mode (RGB555/RGB565)
#define RF_32       0x00008000   // true color

#define RF_USEC32   0x00010000  // use c32tab
#define RF_USE32AS16 0x0020000  // c32tab contain hi-color WORDS
#define RF_USEFONT  0x00040000  // use font_tables
#define RF_PALB     0x00080000  // palette for bilinear filter
#define RF_COMPPAL  0x00100000  // use palette from ATM palette registers
#define RF_GRAY     0x00200000  // grayscale palette

#define RF_MONITOR (RF_MON | RF_GDI | RF_2X)
