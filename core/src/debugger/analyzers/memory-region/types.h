#pragma once

#include <cstdint>
#include <string>

/// @brief Primary type classification for a memory address based on R/W/X access patterns.
enum class BlockType : uint8_t
{
    UNKNOWN = 0,      // No access recorded
    CODE = 1,         // Execute > 0, Write = 0
    DATA = 2,         // Read > 0 or Write > 0, Execute = 0, not both R and W
    VARIABLE = 3,     // Read > 0 AND Write > 0, Execute = 0
    ROM_CODE_IN_RAM = 4,  // Code copied to RAM then executed (bridge routines, RAM relocations)
    SMC = 5           // True self-modifying code (code modifies itself during execution)
};

/// @brief Semantic tags for memory regions (bitmask, combinable).
enum class MemoryTag : uint32_t
{
    None = 0,

    // === Code Category Tags ===
    GenericCode = 1 << 0,        // Executable code, no further classification
    MusicPlayerCode = 1 << 1,    // AY/beeper/COVOX music playback routine
    DiskLoaderCode = 1 << 2,     // TR-DOS / Beta 128 disk loader
    TapeLoaderCode = 1 << 3,     // Tape loading routine (edge timing)
    GraphicsCode = 1 << 4,       // Screen blitting, clearing, scrolling
    SpriteEngineCode = 1 << 5,   // Sprite rendering (masked blit)
    SMCProcedure = 1 << 6,       // Self-modifying code routine
    DecompressorCode = 1 << 7,   // Compression unpacker (LZ, RLE, etc.)
    InputHandlerCode = 1 << 8,   // Keyboard/joystick polling
    ISRCode = 1 << 9,            // Interrupt service routine
    BootSequenceCode = 1 << 10,  // One-shot initialization code

    // === Data Category Tags ===
    GenericData = 1 << 11,       // Read-only data, no further classification
    Variables = 1 << 12,         // Read-write variables
    ImageData = 1 << 13,         // Unpacked slideshow images (6912-byte SCREEN$)
    SpriteGraphics = 1 << 14,    // Sprite pixel data
    SpriteMasks = 1 << 15,       // Sprite transparency masks
    VectorGeometry = 1 << 16,    // Vector/geometry helper data
    TrigData = 1 << 17,          // Trigonometry lookup tables (sin/cos)
    MusicData = 1 << 18,         // AY pattern/note data
    BeeperMusicData = 1 << 19,   // Beeper music data
    CompressedData = 1 << 20,    // Input to decompressor
    ScreenBuffer = 1 << 21,      // VRAM pixel+attribute area
    SystemVariables = 1 << 22,   // ZX Spectrum system variables (0x5B00–0x5CBF)
    LevelData = 1 << 23,         // Game level/map data
    FontData = 1 << 24,          // Character set / font

    // === Reserved for future ===
    CopyProtection = 1 << 25,    // Anti-tamper code
    IM2VectorTable = 1 << 26,    // IM2 interrupt vector table (257 bytes)
    StackArea = 1 << 27,         // Stack region

    // === Alternate names for compatibility ===
    ScreenBitmap = ScreenBuffer,       // VRAM bitmap (0x4000-0x57FF)
    ScreenAttributes = 1 << 28,        // VRAM attributes (0x5800-0x5AFF)
    BeeperCode = MusicPlayerCode,      // Beeper music player code
    CovoxCode = MusicPlayerCode,       // COVOX output code
    LoaderCode = DiskLoaderCode,       // Generic loader code
};

using MemoryTags = uint32_t;

/// @brief A contiguous memory region with uniform type and tags.
struct MemoryRegion
{
    uint16_t startAddress = 0;
    uint16_t endAddress = 0;
    BlockType type = BlockType::UNKNOWN;
    MemoryTags tags = 0;

    // Aggregated statistics
    uint32_t totalReads = 0;
    uint32_t totalWrites = 0;
    uint32_t totalExecutes = 0;

    // Classification metadata
    float confidence = 0.0f;
    std::string evidence;

    // Convenience
    uint16_t length() const { return endAddress - startAddress + 1; }
    bool hasTag(MemoryTag tag) const { return (tags & static_cast<uint32_t>(tag)) != 0; }
    bool isCode() const { return type == BlockType::CODE || type == BlockType::SMC; }
    bool isData() const { return type == BlockType::DATA || type == BlockType::VARIABLE; }
};

/// @brief Summary statistics for the segmentation.
struct SegmentationStats
{
    uint32_t codeBytes = 0;
    uint32_t dataBytes = 0;
    uint32_t variableBytes = 0;
    uint32_t copiedCodeBytes = 0;  // Code copied to RAM (bridge routines)
    uint32_t smcBytes = 0;          // True self-modifying code
    uint32_t unknownBytes = 0;
    uint32_t totalRegions = 0;
    uint32_t taggedRegions = 0;
};
