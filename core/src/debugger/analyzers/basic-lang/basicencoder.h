#pragma once

#include "stdafx.h"
#include <vector>
#include <string>
#include <map>

class Memory;
class Emulator;

/// BasicEncoder: Converts plain text ZX Spectrum BASIC programs to tokenized format
/// and injects them into emulator memory with proper system variable setup.
/// This is the complementary class to BasicExtractor.
class BasicEncoder
{
    /// region <Constants>
public:
    /// ZX Spectrum BASIC Keywords mapped to their token values
    /// Tokens range from 0xA3 to 0xFF
    /// Note: Multi-word keywords like "GO TO" must be checked before single-word variants
    static const std::map<std::string, uint8_t> BasicKeywords;
    
    /// Standard BASIC program start address for 48K Spectrum
    static constexpr uint16_t DEFAULT_PROG_START = 0x5CCB;  // Just after system variables
    
    /// Line terminator
    static constexpr uint8_t LINE_END = 0x0D;
    
    /// Hidden number marker (followed by 5-byte binary representation)
    static constexpr uint8_t NUMBER_MARKER = 0x0E;
    
    /// 128K ROM0 System Variables (in RAM bank 7)
    /// These are used to detect 128K menu vs BASIC editor state
    static constexpr uint16_t FLAGS3 = 0x5B66;       // Bit 0: 1=BASIC mode, 0=Menu mode
    static constexpr uint16_t EC0D_FLAGS = 0xEC0D;   // Bit 1: 1=Menu displayed
    static constexpr uint16_t EC0E_MODE = 0xEC0E;    // Mode: $00=Edit Menu, $FF=Tape Loader
    
    /// 128K Main Menu Entry Points (ROM0)
    static constexpr uint16_t ROM0_BASIC128_ENTRY = 0x286C;  // 128K BASIC option handler
    /// endregion </Constants>
    
    /// region <Types>
public:
    /// Current BASIC environment state
    enum class BasicState
    {
        Menu128K,       ///< On 128K main menu (needs navigation to BASIC)
        Basic128K,      ///< In 128K BASIC editor (ready for commands)
        Basic48K,       ///< In 48K BASIC mode (ready for commands)
        TRDOS_Active,   ///< TR-DOS is actively executing (DOS ROM paged)
        TRDOS_SOS_Call, ///< TR-DOS temporarily using SOS ROM (e.g., for printing)
        Unknown         ///< Unable to determine state
    };
    /// endregion </Types>

    /// region <Public Methods>
public:
    BasicEncoder() = default;
    ~BasicEncoder() = default;

    /// Tokenize a plain text BASIC program into ZX Spectrum binary format
    /// @param basicText Plain text BASIC program (e.g., "10 PRINT \"HELLO\"\\n20 GOTO 10\\n")
    /// @return Tokenized program as byte vector, empty on error
    std::vector<uint8_t> tokenize(const std::string& basicText);

    /// Inject a tokenized BASIC program into emulator memory
    /// Updates all necessary system variables (PROG, VARS, E_LINE, STKEND, etc.)
    /// @param memory Pointer to emulator memory instance
    /// @param tokenizedProgram Tokenized program bytes
    /// @param progStart Optional custom start address (default: DEFAULT_PROG_START)
    /// @return true on success, false on failure
    bool injectIntoMemory(Memory* memory, 
                          const std::vector<uint8_t>& tokenizedProgram,
                          uint16_t progStart = DEFAULT_PROG_START);

    /// Convenience method: tokenize and inject in one call
    /// @param memory Pointer to emulator memory instance
    /// @param basicText Plain text BASIC program
    /// @param progStart Optional custom start address (default: DEFAULT_PROG_START)
    /// @return true on success, false on failure
    bool loadProgram(Memory* memory, 
                     const std::string& basicText,
                     uint16_t progStart = DEFAULT_PROG_START);

    /// Inject a keypress via ROM system variables (LAST_K, FLAGS)
    /// This is an instant injection that bypasses keyboard matrix scanning.
    /// @param memory Pointer to emulator memory instance
    /// @param keyCode ASCII key code (e.g., 'R'=0x52, ENTER=0x0D)
    static void injectKeypress(Memory* memory, uint8_t keyCode);

    /// Inject ENTER keypress via ROM system variables
    /// Convenience wrapper for injectKeypress(memory, 0x0D)
    /// @param memory Pointer to emulator memory instance
    static void injectEnter(Memory* memory);

    /// Inject a string of keypresses one by one via ROM system variables
    /// Each character is written to LAST_K with FLAGS bit 5 set
    /// @param memory Pointer to emulator memory instance
    /// @param text Text to inject as keypresses
    static void injectText(Memory* memory, const std::string& text);
    
    /// Detect current BASIC environment state
    /// Uses three-tier detection: hardware ROM state, stack context, system variables
    /// Properly detects TR-DOS activity even during temporary SOS ROM calls
    /// @param memory Pointer to emulator memory instance
    /// @param z80SP Current Z80 stack pointer (for stack scanning)
    /// @return Current BasicState
    static BasicState detectState(Memory* memory, uint16_t z80SP = 0);
    
    /// Check if TR-DOS is logically active (even during temporary SOS calls)
    /// Uses three-tier detection algorithm:
    /// - Tier 1: DOS ROM hardware paging state
    /// - Tier 2: Stack context (scan for $3D2F return addresses)
    /// - Tier 3: System variable initialization ($5CC2 == $C9)
    /// @param memory Pointer to emulator memory instance
    /// @param z80SP Current Z80 stack pointer
    /// @return true if TR-DOS is in control (actively or via SOS call)
    static bool isTRDOSLogicallyActive(Memory* memory, uint16_t z80SP);
    
    /// Check if TR-DOS system variables have been initialized
    /// @param memory Pointer to emulator memory instance
    /// @return true if TR-DOS has been entered at least once this session
    static bool isTRDOSInitialized(Memory* memory);
    
    /// Scan stack for TR-DOS trap return addresses ($3D00-$3DFF)
    /// Indicates DOS is temporarily borrowing SOS ROM for I/O
    /// @param memory Pointer to emulator memory instance
    /// @param z80SP Current Z80 stack pointer
    /// @param maxDepth Maximum stack entries to scan (default 16)
    /// @return true if stack contains DOS return address
    static bool stackContainsDOSReturnAddress(Memory* memory, uint16_t z80SP, int maxDepth = 16);
    
    /// Verify stack contains plausible return addresses (sanity check)
    /// Checks that first few stack entries look like valid ROM/RAM addresses
    /// rather than garbage data. Uses known ROM vectors and RAM trampolines.
    /// @param memory Pointer to emulator memory instance
    /// @param z80SP Current Z80 stack pointer
    /// @param checkDepth Number of entries to check (default 4)
    /// @return true if stack appears valid, false if garbage detected
    static bool isStackSane(Memory* memory, uint16_t z80SP, int checkDepth = 4);
    
    /// Check if in ready-for-BASIC state (either 128K or 48K editor)
    /// @param memory Pointer to emulator memory instance
    /// @return true if in BASIC editor mode (ready for commands)
    static bool isInBasicEditor(Memory* memory);
    
    /// Navigate from 128K menu directly to 128K BASIC editor
    /// Sets the menu selection to 128 BASIC and triggers the handler
    /// Pauses emulator, runs frames to allow menu transition, then resumes
    /// @param emulator Pointer to emulator instance
    static void navigateToBasic128K(Emulator* emulator);
    
    /// Navigate from 128K menu directly to 48K BASIC mode (SOS ROM)
    /// Sets the menu selection to 48 BASIC and triggers the handler
    /// Pauses emulator, runs frames to allow menu transition, then resumes
    /// @param emulator Pointer to emulator instance
    static void navigateToBasic48K(Emulator* emulator);
    
    /// Navigate from 128K menu to Tape Loader
    /// Menu item 0: Tape Loader
    /// @param memory Pointer to emulator memory instance
    static void navigateToTapeLoader(Memory* memory);
    
    /// Navigate from 128K menu to Calculator
    /// Menu item 2: Calculator
    /// @param memory Pointer to emulator memory instance
    static void navigateToCalculator(Memory* memory);
    
    /// Navigate from 128K menu to Tape Tester (TR-DOS on Pentagon)
    /// Menu item 4: Tape Tester / TR-DOS
    /// @param memory Pointer to emulator memory instance
    static void navigateToTRDOS(Memory* memory);
    
    /// Result struct for injection operations
    struct InjectionResult {
        bool success;           ///< true if injection succeeded
        BasicState state;       ///< Detected BASIC state at time of injection
        std::string message;    ///< Human-readable result or error message
    };
    
    /// Inject a command into the current BASIC edit buffer (48K or 128K)
    /// Does NOT execute the command - just places it in the editor
    /// Dispatches to injectTo48K or injectTo128K based on detected state
    /// Fails if on 128K menu - use autoNavigateAndInject for automatic navigation
    /// @param emulator Pointer to emulator instance
    /// @param command Command string to inject
    /// @return InjectionResult with success flag, detected state, and message
    static InjectionResult injectCommand(Emulator* emulator, const std::string& command);
    
    /// Automatically navigate to BASIC (if needed) and inject command
    /// Handles 128K menu navigation automatically before injection
    /// This is the recommended high-level API for external callers
    /// @param emulator Pointer to emulator instance
    /// @param command Command string to inject
    /// @return InjectionResult with success flag, detected state, and message
    static InjectionResult autoNavigateAndInject(Emulator* emulator, const std::string& command);
    
    /// Inject a command into BASIC edit buffer AND execute it
    /// Combines autoNavigateAndInject + injectEnter for convenience
    /// Use this when you want immediate execution (like typing RUN + ENTER)
    /// Handles 128K menu navigation automatically
    /// @param emulator Pointer to emulator instance
    /// @param command Command string to inject and execute
    /// @return InjectionResult with success flag, detected state, and message
    static InjectionResult runCommand(Emulator* emulator, const std::string& command);
    
    /// ROM-specific: Inject command into 48K BASIC E_LINE buffer
    /// Writes tokenized command to E_LINE, updates K_CUR, WORKSP, CH_ADD
    /// @param memory Pointer to emulator memory instance
    /// @param command Command string to inject
    /// @return InjectionResult (state will be Basic48K on success)
    static InjectionResult injectTo48K(Memory* memory, const std::string& command);
    
    /// ROM-specific: Inject command into 128K BASIC SLEB buffer
    /// Writes command to Screen Line Edit Buffer at $EC16 in RAM bank 7
    /// Sets cursor position and "line altered" flag for ENTER handler
    /// @param emulator Pointer to emulator instance (provides memory and frame stepping)
    /// @param command Command string to inject
    /// @return InjectionResult (state will be Basic128K on success)
    static InjectionResult injectTo128K(Emulator* emulator, const std::string& command);
    
    /// ROM-specific: Inject command into TR-DOS E_LINE buffer
    /// TR-DOS uses SOS ROM for keyboard input, so E_LINE buffer works
    /// Commands are raw ASCII (no BASIC tokenization)
    /// @param memory Pointer to emulator memory instance
    /// @param command Command string to inject
    /// @return InjectionResult (state will be TRDOS_Active on success)
    static InjectionResult injectToTRDOS(Memory* memory, const std::string& command);
    
    /// Replace BASIC keywords with their token equivalents
    /// Handles multi-word keywords and preserves string literals
    /// @param text Line text to process
    /// @return Tokenized line data (without line number header)
    static std::vector<uint8_t> replaceKeywords(const std::string& text);
    
    /// Tokenize immediate command (no space prepending needed)
    /// Handles keywords at any position with boundary checking
    /// @param command Command string to tokenize
    /// @return Tokenized bytes with keywords replaced by tokens
    static std::vector<uint8_t> tokenizeImmediate(const std::string& command);
    /// endregion </Public Methods>

    /// region <Private Methods>
private:
    /// Parse and tokenize a single line of BASIC text
    /// @param lineNumber Line number (1-9999)
    /// @param lineText Line content without line number
    /// @return Tokenized line bytes including header and terminator
    std::vector<uint8_t> tokenizeLine(uint16_t lineNumber, const std::string& lineText);

    /// Update ZX Spectrum system variables after program load
    /// Sets PROG, VARS, E_LINE, WORKSP, STKBOT, STKEND, NXTLIN, CH_ADD
    /// @param memory Pointer to emulator memory instance
    /// @param progStart Start address of BASIC program
    /// @param progEnd End address of BASIC program (exclusive)
    void updateSystemVariables(Memory* memory, uint16_t progStart, uint16_t progEnd);

    /// Helper: Write a 16-bit value to memory (little-endian)
    /// @param memory Pointer to emulator memory instance
    /// @param address Address to write to
    /// @param value 16-bit value to write
    static void writeWord(Memory* memory, uint16_t address, uint16_t value);

    /// Helper: Trim whitespace from string
    /// @param str String to trim
    /// @return Trimmed string
    static std::string trim(const std::string& str);

    /// Helper: Convert string to uppercase
    /// @param str String to convert
    /// @return Uppercase string
    static std::string toUpper(const std::string& str);
    /// endregion </Private Methods>
};
