#pragma once

#include <cstdint>

/// @brief ZX Spectrum 48K System Variables
/// 
/// These are memory addresses used by the ZX Spectrum ROM to store system state.
/// The system variables area occupies addresses 23552-23733 (0x5C00-0x5CB5).
/// 
/// All addresses are in decimal and correspond to the original ZX Spectrum 48K memory map.
/// Many of these variables are preserved in 128K and later models.
/// 
/// References:
/// - ZX Spectrum ROM Disassembly by Ian Logan and Frank O'Hara
/// - The Complete Spectrum ROM Disassembly by Dr. Ian Logan
namespace SystemVariables48k
{
// Keyboard and Input System (23552-23566)
constexpr uint16_t KSTATE = 23552;   // 8 bytes: Keyboard state. Used in reading the keyboard matrix.
constexpr uint16_t LAST_K = 23560;   // 1 byte: Stores newly pressed key code (0-255).
constexpr uint16_t REPDEL = 23561;   // 1 byte: Time (frames) that a key must be held down before it repeats (default 35).
constexpr uint16_t REPPER = 23562;   // 1 byte: Delay (frames) between successive key repeats (default 5).
constexpr uint16_t DEFADD = 23563;   // 2 bytes: Address of arguments of user defined function (FN).
constexpr uint16_t K_DATA = 23565;   // 1 byte: Stores 2nd byte of colour controls entered from keyboard.
constexpr uint16_t TVDATA = 23566;   // 2 bytes: Stores bytes of colour, AT and TAB controls going to TV.

// I/O Streams and Character Set (23568-23609)
constexpr uint16_t STRMS = 23568;    // 38 bytes: Addresses of channels attached to streams 0-15 (2 bytes each, 19 streams total).
constexpr uint16_t CHARS = 23606;    // 2 bytes: 256 less than address of character set (normally points to ROM at 15616/0x3D00).
constexpr uint16_t RASP = 23608;     // 1 byte: Length of warning buzz in T-states/2048.
constexpr uint16_t PIP = 23609;      // 1 byte: Length of keyboard click in T-states/2048.

// Error Handling and System Flags (23610-23612)
constexpr uint16_t ERR_NR = 23610;   // 1 byte: 1 less than the report code (0xFF = no error, 0 = "OK", 1-25 = error codes A-Z).
constexpr uint16_t FLAGS = 23611;    // 1 byte: Various flags to control the BASIC system (bit 0=suppress leading space, bit 2=K mode, bit 3=L mode, etc.).
constexpr uint16_t TV_FLAG = 23612;  // 1 byte: Flags associated with the television (bit 0=lower screen in use, bit 3=mode, bit 5=automatic listing).

// Program Execution Control (23613-23624)
constexpr uint16_t ERR_SP = 23613;   // 2 bytes: Address of item on machine stack to be used as error return.
constexpr uint16_t LIST_SP = 23615;  // 2 bytes: Address of return address from automatic listing.
constexpr uint16_t MODE = 23617;     // 1 byte: Specifies cursor mode: K=Keyword, L=Letter, C=Caps, E=Extended, G=Graphics.
constexpr uint16_t NEWPPC = 23618;   // 2 bytes: Line number to be jumped to (0-9999).
constexpr uint16_t NSPPC = 23620;    // 1 byte: Statement number in line to be jumped to (0-127).
constexpr uint16_t PPC = 23621;      // 2 bytes: Line number of statement currently being executed.
constexpr uint16_t SUBPPC = 23623;   // 1 byte: Number within line of statement being executed (0-127).
constexpr uint16_t BORDCR = 23624;   // 1 byte: Border colour (bits 3-5) * 8, also contains border colour for 'cls' (bits 0-2).

// BASIC Program Memory Layout (23625-23653)
constexpr uint16_t E_PPC = 23625;    // 2 bytes: Number of current line (with program cursor) being edited.
constexpr uint16_t VARS = 23627;     // 2 bytes: Address of variables area (follows BASIC program).
constexpr uint16_t DEST = 23629;     // 2 bytes: Address of variable in assignment (used during LET statements).
constexpr uint16_t CHANS = 23631;    // 2 bytes: Address of channel data (I/O streams information).
constexpr uint16_t CURCHL = 23633;   // 2 bytes: Address of information currently being used for I/O (points into CHANS area).
constexpr uint16_t PROG = 23635;     // 2 bytes: Address of BASIC program (start of program area). Critical for BASIC extraction.
constexpr uint16_t NXTLIN = 23637;   // 2 bytes: Address of next line in program (used during execution).
constexpr uint16_t DATADD = 23639;   // 2 bytes: Address of terminator of last DATA item (used by READ/DATA).
constexpr uint16_t E_LINE = 23641;   // 2 bytes: Address of command being typed in (editor buffer).
constexpr uint16_t K_CUR = 23643;    // 2 bytes: Address of cursor position in E_LINE.
constexpr uint16_t CH_ADD = 23645;   // 2 bytes: Address of the next character to be interpreted by the BASIC interpreter.
constexpr uint16_t X_PTR = 23647;    // 2 bytes: Address of the character after the ? marker (INPUT statement).
constexpr uint16_t WORKSP = 23649;   // 2 bytes: Address of temporary work space (used during expression evaluation).
constexpr uint16_t STKBOT = 23651;   // 2 bytes: Address of bottom of calculator stack (grows upward).
constexpr uint16_t STKEND = 23653;   // 2 bytes: Address of start of spare space (end of used memory). Critical for memory management.

// Calculator and Memory (23655-23658)
constexpr uint16_t BREG = 23655;     // 1 byte: Calculator's b register (used in floating-point arithmetic).
constexpr uint16_t MEM = 23656;      // 2 bytes: Address of area used for calculator's memory (5 values * 5 bytes each).
constexpr uint16_t FLAGS2 = 23658;   // 1 byte: More flags (bit 3=CAPS LOCK on, bit 4=channel K in use).

// Display Control (23659-23696)
constexpr uint16_t DF_SZ = 23659;    // 1 byte: Number of lines (0-24) in lower part of screen (below upper display area).
constexpr uint16_t S_TOP = 23660;    // 2 bytes: Number of top program line in automatic listings (scroll counter).
constexpr uint16_t OLDPPC = 23662;   // 2 bytes: Line number to which CONTINUE jumps (last executed line before break).
constexpr uint16_t OSPCC = 23664;    // 1 byte: Number within line of statement to which CONTINUE jumps.
constexpr uint16_t FLAGX = 23665;    // 1 byte: Various flags (bit 0=simple/compound string, bit 5=INPUT mode, bit 6=numeric result).
constexpr uint16_t STRLEN = 23666;   // 2 bytes: Length of string type destination in assignment.
constexpr uint16_t T_ADDR = 23668;   // 2 bytes: Address of next item in syntax table (used by ROM parser).
constexpr uint16_t SEED = 23670;     // 2 bytes: Seed for RND (random number generator, values 0-65535).
constexpr uint16_t FRAMES = 23672;   // 3 bytes: Frame counter (incremented every 1/50th or 1/60th second, wraps at 16777216).
constexpr uint16_t UDG = 23675;      // 2 bytes: Address of 1st user defined graphic (USR "a" to USR "u", 21 chars * 8 bytes).
constexpr uint16_t COORDSX = 23677;  // 1 byte: x-coordinate of last point plotted (0-255).
constexpr uint16_t COORDSY = 23678;  // 1 byte: y-coordinate of last point plotted (0-175).
constexpr uint16_t P_POSN = 23679;   // 1 byte: Printer position column (33 columns, 0-32).
constexpr uint16_t PR_CC = 23680;    // 2 bytes: LPRINT position - less significant byte of address in printer buffer.
constexpr uint16_t UNUSED1 = 23681;  // 1 byte: Not used (part of PR_CC).
constexpr uint16_t ECHO_E = 23682;   // 2 bytes: End of input buffer (33 columns for lower screen).
constexpr uint16_t DF_CC = 23684;    // 2 bytes: Address in display file of PRINT position (upper screen).
constexpr uint16_t DF_CCL = 23686;   // 2 bytes: Like DF_CC but for lower part of screen.
constexpr uint16_t S_POSN = 23688;   // 2 bytes: PRINT position: column (0-31) in low byte, line (0-21) in high byte.
constexpr uint16_t PRPOSN = 23689;   // 1 byte: Same as S_POSN high byte (line number 0-21).
constexpr uint16_t SPOSNL = 23690;   // 2 bytes: Like S_POSN for lower part of screen.
constexpr uint16_t SCR_CT = 23692;   // 1 byte: Scroll counter (counts down from initial value for scroll prompts).
constexpr uint16_t ATTR_P = 23693;   // 1 byte: Permanent current colours (INK, PAPER, FLASH, BRIGHT).
constexpr uint16_t MASP_P = 23694;   // 1 byte: Permanent transparent colours mask (0=transparent, 1=opaque).
constexpr uint16_t ATTR_T = 23695;   // 1 byte: Temporary current colours (like ATTR_P but temporary for current operation).
constexpr uint16_t MASK_T = 23696;   // 1 byte: Like MASP_P, but temporary.
constexpr uint16_t P_FLAG = 23697;   // 1 byte: Printer flags (bit 0=OVER status, bit 2=inverse video).

// Calculator Memory and System Limits (23698-23733)
constexpr uint16_t MEMBOT = 23698;   // 30 bytes: Calculator's memory area (5 memory slots * 6 bytes: 1 byte flag + 5 bytes value).
constexpr uint16_t UNUSED2 = 23728;  // 2 bytes: Not used (reserved for future expansion).
constexpr uint16_t RAMTOP = 23730;   // 2 bytes: Address of last byte of BASIC system area (normally 32767 for 48K, can be lowered by CLEAR).
constexpr uint16_t P_RAMT = 23732;   // 2 bytes: Address of last byte of physical RAM (normally 32767/0x7FFF for 48K, 65535/0xFFFF for 128K).
}  // namespace SystemVariables48k

/// @brief TR-DOS System Variables (Beta Disk Interface)
/// 
/// TR-DOS is the disk operating system used with the Beta Disk Interface (Beta 128)
/// on ZX Spectrum. These variables occupy memory addresses 23750-23805 (0x5CC6-0x5CFD).
/// 
/// The Beta Disk interface was the de facto standard disk interface in Eastern Europe
/// and Russia, supporting the WD1793/VG93 floppy disk controller.
/// 
/// Different TR-DOS versions (v5.03, v5.04T, v6.xx) may have minor differences in
/// variable usage. The addresses below represent the most common usage across versions.
/// 
/// References:
/// - TR-DOS for professionals and amateurs by Alessandro Grussu
/// - Beta Disk Interface Technical Documentation
/// - WD1793 FDC Programming Guide
namespace TRDOS
{
// Disk Geometry Parameters (23750-23755)
constexpr uint16_t SECTORS_PER_TRACK = 23750;  // 1 byte (0x5CC6): Number of sectors per track (typically 16).
constexpr uint16_t TRACKS_PER_SIDE = 23751;    // 1 byte (0x5CC7): Number of tracks per side (40 or 80).
constexpr uint16_t SIDES_PER_DISK = 23752;     // 1 byte (0x5CC8): Number of sides (1 or 2). Also: Disk mode for drive "A" (#FF on init).
constexpr uint16_t SECTORS_PER_GRANULE = 23753; // 1 byte (0x5CC9): Number of sectors per granule (allocation unit).
constexpr uint16_t GRANULES_PER_TRACK = 23754; // 1 byte (0x5CCA): Number of granules per track.
constexpr uint16_t GRANULES_PER_DISK = 23755;  // 1 byte (0x5CCB): Total number of granules on disk.

// Disk Operation Status (23756-23758)
constexpr uint16_t CAT_SECTOR = 23756;         // 1 byte (0x5CCC): Sector number displayed by CAT command.
constexpr uint16_t DRIVE_STATUS = 23757;       // 1 byte (0x5CCD): Drive readiness status (WD1793 status register value).
constexpr uint16_t OPERATION_TYPE = 23758;     // 1 byte (0x5CCE): Operation type: 0x00 = read, 0xFF = write.

// Workspace and Operation Buffers (23759-23760)
constexpr uint16_t WORKSP_ADDR = 23759;        // 2 bytes (0x5CCF-0x5CD0): Address of workspace used during MOVE, COPY, LIST operations.

// BASIC Auto-start Line (23761-23763)
constexpr uint16_t AUTOSTART_LINE = 23761;     // 2 bytes (0x5CD1-0x5CD2): Line number for auto-start during SAVE BASIC.
constexpr uint16_t AUTOSTART_HIGH = 23763;     // 1 byte (0x5CD3): High byte of autostart line (used in 3-byte line number storage).

// File Operations During MOVE (23764-23766)
constexpr uint16_t DELETED_FILE_NUM = 23764;   // 1 byte (0x5CD4): Number of the deleted file during MOVE operation.
constexpr uint16_t DELETED_FILE_SECTOR = 23765; // 1 byte (0x5CD5): Sector number of deleted file during MOVE.
constexpr uint16_t DELETED_FILE_TRACK = 23766; // 1 byte (0x5CD6): Track number of deleted file during MOVE.

// Formatting and Error Handling (23767-23768)
constexpr uint16_t SC_0B = 23767;              // 1 byte (0x5CD7): Multi-purpose counter. During format: tracks to format. After CODE save: start address. After drive check: track count. Defective sectors found during format/check.
constexpr uint16_t SC_0D = 23768;              // 1 byte (0x5CD8): Multi-purpose. During format: VERIFY flag (non-zero = skip verification). Error code for defective sectors.

// Current Operation State (23769-23770)
constexpr uint16_t CURRENT_SIDE = 23769;       // 1 byte (0x5CD9): Current drive side: 0x00 = side 0 (upper), 0x01 = side 1 (lower). Also: after CODE save = file length. For type 0x0E files: load address.
constexpr uint16_t DISK_TYPE = 23770;          // 1 byte (0x5CDA): Disk type identifier during format (0x80 = double-sided). Also: disk format type byte.

// Advanced Granule Management (23771-23773)
constexpr uint16_t SECTORS_PER_BLOCK = 23771;  // 1 byte (0x5CDB): Number of sectors per block.
constexpr uint16_t BLOCKS_PER_GRANULE = 23772; // 1 byte (0x5CDC): Number of blocks per granule.
constexpr uint16_t GRANULES_TOTAL = 23773;     // 1 byte (0x5CDD): Total granules per disk (redundant with 23755, used in some operations).

// File Operations Buffer - First File Descriptor (23774-23781)
constexpr uint16_t FILE_NAME = 23774;          // 8 bytes (0x5CDE-0x5CE5): First file name buffer (used during file operations like LOAD, SAVE, MOVE).
constexpr uint16_t TMP_DRIVE = 23774;          // 1 byte (0x5CDE): Drive number for first file (overlaps with FILE_NAME).
constexpr uint16_t PATH_LENGTH_1 = 23775;      // 1 byte (0x5CDF): Path length for first file.
constexpr uint16_t DEVICE_TYPE_1 = 23776;      // 1 byte (0x5CE0): Device type for first file.
constexpr uint16_t NAME_LENGTH_1 = 23777;      // 1 byte (0x5CE1): Filename length for first file.
constexpr uint16_t FILENAME_START_1 = 23778;   // 4 bytes (0x5CE2-0x5CE5): Actual filename characters (max 8 chars, continues from FILE_NAME).

// File Operations Buffer - Second File Descriptor (23782-23789)
constexpr uint16_t FILE_NAME_2 = 23782;        // 8 bytes (0x5CE6-0x5CED): Second file name buffer (used during COPY, MOVE operations).
constexpr uint16_t DRV_N2 = 23782;             // 1 byte (0x5CE6): Drive number for second file.
constexpr uint16_t PTH_L2 = 23783;             // 1 byte (0x5CE7): Path length for second file.
constexpr uint16_t DEV_T2 = 23784;             // 1 byte (0x5CE8): Device type for second file.
constexpr uint16_t NAM_L2 = 23785;             // 1 byte (0x5CE9): Name length for second file.
constexpr uint16_t FILENAME_START_2 = 23786;   // 4 bytes (0x5CEA-0x5CED): Actual filename characters for second file.

// File Header Workspace (23790-23797)
constexpr uint16_t FILE_TYPE = 23790;          // 1 byte (0x5CEE): File type from catalog (BASIC, CODE, arrays, etc.).
constexpr uint16_t FILE_LENGTH = 23791;        // 2 bytes (0x5CEF-0x5CF0): File length in bytes.
constexpr uint16_t FILE_START_ADDR = 23793;    // 2 bytes (0x5CF1-0x5CF2): Start address for CODE files, or BASIC program length.
constexpr uint16_t FILE_PARAM_2 = 23795;       // 2 bytes (0x5CF3-0x5CF4): Program length without variables (BASIC), or array name.
constexpr uint16_t FILE_AUTOSTART = 23797;     // 2 bytes (0x5CF5-0x5CF6): Autostart line number for BASIC, or execute address for CODE.

// Operation Control (23799)
constexpr uint16_t NUM_COPIES = 23799;         // 1 byte (0x5CF7): Number of copies to make during SAVE (reset to 1 after operation).

// Additional TR-DOS Working Variables (23800-23805+)
constexpr uint16_t TRDOS_FLAGS = 23800;        // 1 byte (0x5CF8): TR-DOS operation flags (version-dependent).
constexpr uint16_t CATALOG_BUFFER = 23801;     // N bytes (0x5CF9+): Start of catalog sector buffer (typically 256 bytes at 0x5D00-0x5DFF).

// TR-DOS ROM Entry Points (for reference, actual ROM addresses vary by version)
// Note: These are typical entry points for TR-DOS v5.04T
namespace EntryPoints
{
    constexpr uint16_t INIT_TRDOS = 0x0000;    // TR-DOS initialization
    constexpr uint16_t CATALOG = 0x0A37;       // CAT command
    constexpr uint16_t LOAD_FILE = 0x0000;     // LOAD command (address varies)
    constexpr uint16_t SAVE_FILE = 0x0000;     // SAVE command (address varies)
    constexpr uint16_t FORMAT_DISK = 0x1EC2;   // FORMAT command entry point (v5.04T)
} // namespace EntryPoints

// Disk Type Identifiers (used in DISK_TYPE variable)
namespace DiskTypes
{
    constexpr uint8_t DISK_40T_SS = 0x17;      // 40 tracks, single-sided (624 free sectors)
    constexpr uint8_t DISK_40T_DS = 0x16;      // 40 tracks, double-sided (1264 free sectors) 
    constexpr uint8_t DISK_80T_SS = 0x18;      // 80 tracks, single-sided (1264 free sectors)
    constexpr uint8_t DISK_80T_DS = 0x19;      // 80 tracks, double-sided (2544 free sectors)
} // namespace DiskTypes

// ROM Switching Mechanism (Beta 128 Hardware)
// Based on analysis of TR-DOS 5.04T ROM interaction with 48K ROM
namespace ROMSwitch
{
    // Trap address range - when PC fetches opcode from this range:
    // - If SOS ROM paged -> hardware switches to DOS ROM
    constexpr uint16_t TRAP_START = 0x3D00;
    constexpr uint16_t TRAP_END = 0x3DFF;
    
    // Key entry points within trap range
    constexpr uint16_t ENTRY_MAIN = 0x3D00;       // Main TR-DOS entry point
    constexpr uint16_t ENTRY_COMMAND = 0x3D03;   // Execute TR-DOS command from BASIC
    constexpr uint16_t ENTRY_FILE_IN = 0x3D06;   // Data file input routine  
    constexpr uint16_t ENTRY_FILE_OUT = 0x3D0E;  // Data file output routine
    constexpr uint16_t ENTRY_MCODE = 0x3D13;     // Machine code calls
    constexpr uint16_t ROM_TRAMPOLINE = 0x3D2F;  // ROM switch trampoline (return from SOS)
    constexpr uint16_t ENTRY_FULL = 0x3D31;      // Full DOS entry with sys vars init
    
    // RAM stub address - when PC fetches opcode from RAM (>= 0x4000):
    // - If DOS ROM paged -> hardware switches to SOS ROM
    // TR-DOS places RET ($C9) at this address during initialization
    constexpr uint16_t RAM_STUB = 0x5CC2;
    constexpr uint8_t RAM_STUB_OPCODE = 0xC9;    // RET instruction
    
    // System variables for detecting TR-DOS initialization
    constexpr uint16_t CHANS_TRDOS_VALUE = 0x5D25; // CHANS value when TR-DOS sys vars present
    constexpr uint16_t SYS_REG_MIRROR = 0x5D16;    // Mirror of last value written to port $FF
    constexpr uint16_t SPLASH_FLAG = 0x5D17;       // $AA = skip splash screen
    constexpr uint16_t DEFAULT_DRIVE = 0x5D19;     // Default drive number (0-3)
    constexpr uint16_t MCODE_FLAG = 0x5D1F;        // Non-zero if called from machine code
} // namespace ROMSwitch

} // namespace TRDOS

/// @brief Spectrum 128K Editor System Variables
/// 
/// These occupy addresses $EC00-$FFFF in physical RAM bank 7.
/// Used by the 128K BASIC Editor, Calculator, and Menu system.
/// 
/// References:
/// - ZX Spectrum 128K ROM0 Disassembly
namespace Editor128K
{
// New 128K System Variables in Printer Buffer ($5B00-$5BFF)
constexpr uint16_t SWAP = 0x5B00;        // 20 bytes: Swap paging subroutine
constexpr uint16_t YOUNGER = 0x5B14;     // 9 bytes: Return paging subroutine
constexpr uint16_t ONERR = 0x5B1D;       // 18 bytes: Error handler paging subroutine
constexpr uint16_t BANK_M = 0x5B5C;      // 1 byte: Copy of last byte output to port $7FFD
constexpr uint16_t FLAGS3 = 0x5B66;      // 1 byte: Flags - Bit 0: 1=BASIC mode, 0=Menu mode

// Editor Workspace Variables (in RAM bank 7 at $EC00+)
constexpr uint16_t EDIT_FLAGS = 0xEC00;  // 3 bytes: Flags when inserting line
constexpr uint16_t EDIT_ERR = 0xEC03;    // 3 bytes: Flags upon error
constexpr uint16_t EDIT_COUNT = 0xEC06;  // 2 bytes: Count of editable characters
constexpr uint16_t EDIT_EPPC = 0xEC08;   // 2 bytes: Editor's E_PPC (last line entered)
constexpr uint16_t MENU_INDEX = 0xEC0C;  // 1 byte: Current menu index (0=Tape, 1=128 BASIC, 2=Calc, 3=48 BASIC, 4=Tester)
constexpr uint16_t EDITOR_FLAGS = 0xEC0D; // 1 byte: Editor flags - Bit 1=Menu displayed, Bit 4=Return to calc
constexpr uint16_t EDITOR_MODE = 0xEC0E; // 1 byte: Mode - $00=Edit Menu, $04=Calculator, $FF=Tape Loader

// Screen Line Edit Buffer (SLEB) - 128K full-screen editor buffer at $EC16
// This is where typed commands are stored in 128K BASIC mode, NOT E_LINE
constexpr uint16_t SLEB = 0xEC16;         // Screen Line Edit Buffer (up to 32 chars)
constexpr uint16_t SLEB_LEN = 0xEC36;     // 1 byte: Current length of text in SLEB

// Main Menu Item Indices
namespace MenuItems
{
    constexpr uint8_t TAPE_LOADER = 0;   // Tape Loader
    constexpr uint8_t BASIC_128K = 1;    // 128 BASIC
    constexpr uint8_t CALCULATOR = 2;    // Calculator
    constexpr uint8_t BASIC_48K = 3;     // 48 BASIC
    constexpr uint8_t TAPE_TESTER = 4;   // Tape Tester
} // namespace MenuItems

} // namespace Editor128K
