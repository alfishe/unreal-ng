#include "pch.h"
#include "manifest_test.h"
#include "emulator/monitoring/manifest.h"

#include <cstring>

using namespace monitoring;

/// region <SetUp / TearDown>

void Manifest_Test::SetUp()
{
    _shm = std::calloc(1, SHM_SIZE);
    ASSERT_NE(_shm, nullptr);
}

void Manifest_Test::TearDown()
{
    std::free(_shm);
    _shm = nullptr;
}

/// endregion </SetUp / TearDown>

// =============================================================================
// Struct size assertions
// =============================================================================

TEST_F(Manifest_Test, ManifestHeaderSize)
{
    EXPECT_EQ(sizeof(ManifestHeader), 128u);
}

TEST_F(Manifest_Test, SectionDescriptorSize)
{
    EXPECT_EQ(sizeof(SectionDescriptor), 64u);
}

TEST_F(Manifest_Test, Z80StateSnapshotSize)
{
    EXPECT_EQ(sizeof(Z80StateSnapshot), 128u);
}

TEST_F(Manifest_Test, AYStateSnapshotSize)
{
    EXPECT_EQ(sizeof(AYStateSnapshot), 64u);
}

TEST_F(Manifest_Test, FDCStateSnapshotSize)
{
    EXPECT_EQ(sizeof(FDCStateSnapshot), 64u);
}

TEST_F(Manifest_Test, InputStateSnapshotSize)
{
    EXPECT_EQ(sizeof(InputStateSnapshot), 64u);
}

TEST_F(Manifest_Test, FramebufferHeaderSize)
{
    EXPECT_EQ(sizeof(FramebufferSectionHeader), 16u);
}

TEST_F(Manifest_Test, AudioBufferHeaderSize)
{
    EXPECT_EQ(sizeof(AudioBufferSectionHeader), 16u);
}

TEST_F(Manifest_Test, HeatmapZ80HeaderSize)
{
    EXPECT_EQ(sizeof(HeatmapZ80Header), 8u);
}

TEST_F(Manifest_Test, HeatmapPagesHeaderSize)
{
    EXPECT_EQ(sizeof(HeatmapPagesHeader), 8u);
}

TEST_F(Manifest_Test, OpcodeTraceEntryPODSize)
{
    EXPECT_EQ(sizeof(OpcodeTraceEntryPOD), 16u);
}

TEST_F(Manifest_Test, OpcodeTraceHeaderSize)
{
    EXPECT_EQ(sizeof(OpcodeTraceHeader), 24u);
}

TEST_F(Manifest_Test, CallTraceEntryPODSize)
{
    EXPECT_EQ(sizeof(CallTraceEntryPOD), 24u);
}

TEST_F(Manifest_Test, CallTraceHeaderSize)
{
    EXPECT_EQ(sizeof(CallTraceHeader), 32u);
}

TEST_F(Manifest_Test, ControlCommandLayout)
{
    EXPECT_EQ(CONTROL_CMD_SLOTS, 4u);
}

// =============================================================================
// Manifest header initialization
// =============================================================================

TEST_F(Manifest_Test, WriteAndReadManifestHeader)
{
    auto* header = static_cast<ManifestHeader*>(_shm);

    header->magic = MANIFEST_MAGIC;
    header->version = MANIFEST_VERSION;
    header->header_size = sizeof(ManifestHeader);
    header->total_size = SHM_SIZE;
    header->emulator_pid = 12345;
    header->flags = MANIFEST_RUNNING;
    header->section_count = 0;
    header->max_sections = INITIAL_MAX_SECTIONS;
    header->descriptor_size = sizeof(SectionDescriptor);
    header->global_epoch.store(0, std::memory_order_relaxed);
    header->frame_counter.store(0, std::memory_order_relaxed);
    header->layout_epoch.store(0, std::memory_order_relaxed);

    uint32_t dataOffset = sizeof(ManifestHeader) + INITIAL_MAX_SECTIONS * sizeof(SectionDescriptor);
    dataOffset = (dataOffset + 63) & ~63u;
    header->data_region_offset = dataOffset;

    // Verify
    EXPECT_EQ(header->magic, MANIFEST_MAGIC);
    EXPECT_EQ(header->version, MANIFEST_VERSION);
    EXPECT_EQ(header->header_size, sizeof(ManifestHeader));
    EXPECT_EQ(header->emulator_pid, 12345u);
    EXPECT_EQ(header->flags, MANIFEST_RUNNING);
    EXPECT_GT(header->data_region_offset, static_cast<uint32_t>(sizeof(ManifestHeader)));
}

// =============================================================================
// Section descriptor creation and lookup
// =============================================================================

TEST_F(Manifest_Test, RegisterAndRetrieveSectionDescriptor)
{
    auto* header = static_cast<ManifestHeader*>(_shm);
    header->magic = MANIFEST_MAGIC;
    header->header_size = sizeof(ManifestHeader);
    header->total_size = SHM_SIZE;
    header->section_count = 0;
    header->max_sections = INITIAL_MAX_SECTIONS;
    header->descriptor_size = sizeof(SectionDescriptor);

    uint32_t dataOffset = sizeof(ManifestHeader) + INITIAL_MAX_SECTIONS * sizeof(SectionDescriptor);
    dataOffset = (dataOffset + 63) & ~63u;
    header->data_region_offset = dataOffset;

    // Manually register a section
    header->section_count = 1;
    auto* desc = getDescriptor(_shm, header, 0);
    ASSERT_NE(desc, nullptr);

    desc->type = SectionType::CHIP_STATE_Z80;
    desc->flags = SECTION_ENABLED;
    desc->offset = dataOffset;
    desc->length = sizeof(Z80StateSnapshot);
    std::strncpy(desc->name, "z80_regs", sizeof(desc->name) - 1);
    desc->epoch.store(0, std::memory_order_relaxed);

    // Retrieve
    auto* retrieved = getDescriptor(_shm, header, 0);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->type, SectionType::CHIP_STATE_Z80);
    EXPECT_EQ(retrieved->flags, SECTION_ENABLED);
    EXPECT_EQ(retrieved->length, sizeof(Z80StateSnapshot));
    EXPECT_STREQ(retrieved->name, "z80_regs");

    // Out of bounds
    auto* oob = getDescriptor(_shm, header, 1);
    EXPECT_EQ(oob, nullptr);
}

// =============================================================================
// Epoch-safe read protocol
// =============================================================================

TEST_F(Manifest_Test, EpochSafeRead_CoherentRead)
{
    auto* header = static_cast<ManifestHeader*>(_shm);
    header->header_size = sizeof(ManifestHeader);
    header->section_count = 1;
    header->max_sections = INITIAL_MAX_SECTIONS;
    header->descriptor_size = sizeof(SectionDescriptor);

    uint32_t dataOffset = sizeof(ManifestHeader) + INITIAL_MAX_SECTIONS * sizeof(SectionDescriptor);
    dataOffset = (dataOffset + 63) & ~63u;
    header->data_region_offset = dataOffset;

    auto* desc = getDescriptor(_shm, header, 0);
    desc->type = SectionType::CHIP_STATE_Z80;
    desc->offset = dataOffset;
    desc->length = sizeof(Z80StateSnapshot);
    desc->epoch.store(42, std::memory_order_relaxed);

    // Write known data
    auto* z80Data = static_cast<Z80StateSnapshot*>(getSectionData(_shm, desc));
    z80Data->af = 0xDEAD;
    z80Data->bc = 0xBEEF;
    z80Data->pc = 0x1234;

    // Read with epoch-safe protocol
    Z80StateSnapshot readBuf{};
    bool coherent = epochSafeRead(desc, _shm, &readBuf, sizeof(readBuf));
    EXPECT_TRUE(coherent);
    EXPECT_EQ(readBuf.af, 0xDEAD);
    EXPECT_EQ(readBuf.bc, 0xBEEF);
    EXPECT_EQ(readBuf.pc, 0x1234u);
}

TEST_F(Manifest_Test, EpochSafeRead_RejectsUpdating)
{
    auto* header = static_cast<ManifestHeader*>(_shm);
    header->header_size = sizeof(ManifestHeader);
    header->section_count = 1;
    header->max_sections = INITIAL_MAX_SECTIONS;
    header->descriptor_size = sizeof(SectionDescriptor);

    uint32_t dataOffset = sizeof(ManifestHeader) + INITIAL_MAX_SECTIONS * sizeof(SectionDescriptor);
    dataOffset = (dataOffset + 63) & ~63u;
    header->data_region_offset = dataOffset;

    auto* desc = getDescriptor(_shm, header, 0);
    desc->type = SectionType::CHIP_STATE_Z80;
    desc->offset = dataOffset;
    desc->length = sizeof(Z80StateSnapshot);

    // Set UPDATING sentinel
    desc->epoch.store(EPOCH_UPDATING, std::memory_order_relaxed);

    Z80StateSnapshot readBuf{};
    bool coherent = epochSafeRead(desc, _shm, &readBuf, sizeof(readBuf));
    EXPECT_FALSE(coherent);
}

// =============================================================================
// getSectionData helper
// =============================================================================

TEST_F(Manifest_Test, GetSectionData_ValidDescriptor)
{
    auto* header = static_cast<ManifestHeader*>(_shm);
    header->header_size = sizeof(ManifestHeader);

    SectionDescriptor desc{};
    desc.offset = 2048;
    desc.length = 128;

    auto* data = getSectionData(_shm, &desc);
    EXPECT_EQ(data, static_cast<char*>(_shm) + 2048);
}

TEST_F(Manifest_Test, GetSectionData_NullDescriptor)
{
    auto* data = getSectionData(_shm, nullptr);
    EXPECT_EQ(data, nullptr);
}

TEST_F(Manifest_Test, GetSectionData_ZeroOffset)
{
    SectionDescriptor desc{};
    desc.offset = 0;
    desc.length = 128;

    auto* data = getSectionData(_shm, &desc);
    EXPECT_EQ(data, nullptr);
}

// =============================================================================
// Section type constants
// =============================================================================

TEST_F(Manifest_Test, SectionTypeValues)
{
    EXPECT_EQ(static_cast<uint16_t>(SectionType::CHIP_STATE_Z80), 0x0010u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::CHIP_STATE_AY), 0x0011u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::CHIP_STATE_FDC), 0x0012u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::INPUT_STATE), 0x0020u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::LOG_STREAM), 0x0100u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::HEATMAP_Z80), 0x0101u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::HEATMAP_PAGES), 0x0103u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::OPCODE_TRACE), 0x0110u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::CALL_TRACE), 0x0111u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::FRAMEBUFFER), 0x0200u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::AUDIO_BUFFER), 0x0201u);
    EXPECT_EQ(static_cast<uint16_t>(SectionType::CONTROL_RING), 0xFF00u);
}

// =============================================================================
// Heatmap section data size constants
// =============================================================================

TEST_F(Manifest_Test, HeatmapZ80DataSize)
{
    // Header (8) + 3 arrays × 65536 × 4
    uint32_t expected = 8u + 3u * 65536u * 4u;
    EXPECT_EQ(HEATMAP_Z80_DATA_SIZE, expected);
}

TEST_F(Manifest_Test, HeatmapPagesDataSize)
{
    // Header (8) + 3 arrays × 323 × 4
    uint32_t expected = 8u + 3u * 323u * 4u;
    EXPECT_EQ(HEATMAP_PAGES_DATA_SIZE, expected);
}

TEST_F(Manifest_Test, OpcodeTraceDataSize)
{
    // Header (24) + 1024 entries × 16
    uint32_t expected = 24u + 1024u * 16u;
    EXPECT_EQ(OPCODE_TRACE_DATA_SIZE, expected);
}

TEST_F(Manifest_Test, CallTraceDataSize)
{
    // Header (32) + 512 entries × 24
    uint32_t expected = 32u + 512u * 24u;
    EXPECT_EQ(CALL_TRACE_DATA_SIZE, expected);
}
