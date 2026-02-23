#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/// Synthetic data generator that produces realistic Z80-like memory patterns
/// and call trace data for visualization prototyping.
struct SyntheticData
{
    static constexpr int BANK_SIZE = 16384;

    // --- Memory content (16KB per bank) ---
    std::array<uint8_t, BANK_SIZE> bank0_rom;   // ROM: code + tables
    std::array<uint8_t, BANK_SIZE> bank1_ram;   // RAM: mixed code/data
    std::array<uint8_t, BANK_SIZE> bank2_ram;   // RAM: mostly data
    std::array<uint8_t, BANK_SIZE> bank3_ram;   // RAM: stack + variables

    // --- R/W/X counters (per address within bank) ---
    std::array<uint32_t, BANK_SIZE> readCounters[4];
    std::array<uint32_t, BANK_SIZE> writeCounters[4];
    std::array<uint32_t, BANK_SIZE> execCounters[4];

    // --- Call trace data (per address within bank) ---
    // Combined source + target hits (for Option A: heatmap)
    std::array<uint32_t, BANK_SIZE> cfHeatmap[4];

    // Separate source/target (for Option B: split overlay)
    std::array<uint32_t, BANK_SIZE> cfSources[4];   // m1_pc hits (branch origins)
    std::array<uint32_t, BANK_SIZE> cfTargets[4];   // target_addr hits (jump destinations)

    // Type breakdown (for Option C: type-colored)
    enum CFType : uint8_t { NONE = 0, JP = 1, JR = 2, CALL = 3, RET = 4, RST = 5, DJNZ = 6, RETI = 7 };
    std::array<CFType, BANK_SIZE> cfDominantType[4]; // Most frequent CF type at each address

    // --- Opcode trace (execution frequency per address, from OpcodeProfiler) ---
    std::array<uint32_t, BANK_SIZE> opcodeExecCount[4];  // How many times each address was the M1 of an executed opcode

    // --- Entropy map (local Shannon entropy, 0.0-8.0 per 16-byte window) ---
    std::array<float, BANK_SIZE> entropyMap[4];

    // --- Memory freshness (frame number of last write, 0 = never written) ---
    std::array<uint32_t, BANK_SIZE> freshnessMap[4];  // Higher = more recently written
    uint32_t currentFrame = 5000;  // Simulated "current" frame for freshness calculation

    // --- Memory region classification ---
    enum RegionType : uint8_t {
        REG_UNKNOWN = 0,
        REG_CODE,       // Executable code
        REG_DATA,       // Data tables, constants
        REG_SPRITE,     // Sprite/graphics/character set data
        REG_MUSIC,      // Music/sound data (AY patterns, note tables)
        REG_CALC,       // Calculation buffers, scratch areas
        REG_STACK,      // Stack region
        REG_SYSVARS,    // System variables
        REG_SCREEN,     // Screen pixel/attribute memory
        REG_IO_BUFFER   // I/O buffers (printer, tape, etc.)
    };
    std::array<RegionType, BANK_SIZE> regionMap[4];

    // --- Flow arcs (source → target address connections for vector viz) ---
    struct FlowArc {
        uint16_t srcOffset;  // Source offset within bank
        uint16_t tgtOffset;  // Target offset within bank
        uint32_t hitCount;   // How many times this flow was taken
        CFType type;         // JP/JR/CALL/RET/etc.
    };
    std::vector<FlowArc> flowArcs[4];  // Per-bank flow connections

    // Per-bank summary stats
    struct BankStats
    {
        uint32_t totalReads = 0;
        uint32_t totalWrites = 0;
        uint32_t totalExecs = 0;
        uint32_t totalCFEvents = 0;
        uint32_t callCount = 0;
        uint32_t jpCount = 0;
        uint32_t jrCount = 0;
        uint32_t rstCount = 0;
        uint32_t retCount = 0;
        uint32_t djnzCount = 0;
        uint16_t uniqueTargets = 0;
        uint32_t maxTargetHits = 0;
    };
    BankStats stats[4];

    /// Generate all synthetic data
    void generate();

    /// Load visualization data from a .uzvd binary file dumped by the emulator.
    /// @param filename Path to the .uzvd file
    /// @return true if loaded successfully, false on error (falls back to synthetic)
    bool loadFromFile(const std::string& filename);

    /// True if data was loaded from a file rather than generated synthetically
    bool isLoaded = false;

private:
    void generateMemoryContent();
    void generateRWXCounters();
    void generateCallTraceData();
    void generateOpcodeTrace();
    void generateEntropyMap();
    void generateFreshnessMap();
    void generateRegionMap();
    void generateFlowArcs();
    void deriveFlowArcsFromCFData();
};
