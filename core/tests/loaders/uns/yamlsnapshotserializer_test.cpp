#include "loaders/snapshot/uns/serializers/yamlsnapshotserializer.h"

#include <3rdparty/rapidyaml/ryml_all.hpp>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"

// Helper to safely access map fields without inserting fake records
// Returns pointer to value if found, nullptr otherwise
template <typename Map, typename Key>
auto map_get(const Map& m, const Key& k) -> const typename Map::mapped_type* {
    auto it = m.find(k);
    return it != m.end() ? &it->second : nullptr;
}

class YamlSnapshotSerializerTest : public ::testing::Test
{
protected:
    YamlSnapshotSerializerCUT serializer;
    ryml::Tree tree;
    ryml::NodeRef root;

    void SetUp() override
    {
        tree.clear();
        root = tree.rootref();
        root |= ryml::MAP;
    }

    static std::string FindProjectRoot()
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::current_path();
        int max_iters = 5;

        int iters = 0;
        while (iters < max_iters)
        {
            if (fs::exists(dir / "core") && fs::is_directory(dir / "core") &&
                fs::exists(dir / "data") && fs::is_directory(dir / "data") &&
                fs::exists(dir / "testdata") && fs::is_directory(dir / "testdata"))
            {
                return dir.string();
            }

            if (dir.has_parent_path())
            {
                dir = dir.parent_path();
            }
            else
            {
                break;
            }

            ++iters;
        }

        return {};
    }
};

TEST_F(YamlSnapshotSerializerTest, LoadMetadata)
{
    auto metadata_node = root.append_child();
    metadata_node.set_key("metadata");
    metadata_node |= ryml::MAP;
    metadata_node["manifest_version"] << "1.8";
    metadata_node["emulator_id"] << "Test Emulator";
    metadata_node["emulator_version"] << "1.0";
    metadata_node["host_platform"] << "Test Platform";
    metadata_node["emulated_platform"] << "ZX Spectrum 128K";
    metadata_node["save_time"] << "2024-01-01T12:00:00Z";
    metadata_node["description"] << "Test Description";

    MetadataDTO metadata;
    EXPECT_TRUE(serializer.loadMetadata(metadata_node, metadata));

    EXPECT_EQ("1.8", metadata.manifest_version);
    EXPECT_EQ("Test Emulator", metadata.emulator_id);
    EXPECT_EQ("1.0", metadata.emulator_version);
    EXPECT_EQ("Test Platform", metadata.host_platform);
    EXPECT_EQ("ZX Spectrum 128K", metadata.emulated_platform);
    EXPECT_EQ("2024-01-01T12:00:00Z", metadata.save_time);
    EXPECT_EQ("Test Description", metadata.description);
}

TEST_F(YamlSnapshotSerializerTest, SaveMetadata)
{
    MetadataDTO metadata;
    metadata.manifest_version = "1.8";
    metadata.emulator_id = "Test Emulator";
    metadata.emulator_version = "1.0";
    metadata.host_platform = "Test Platform";
    metadata.emulated_platform = "ZX Spectrum 128K";
    metadata.save_time = "2024-01-01T12:00:00Z";
    metadata.description = "Test Description";

    auto metadata_node = root["metadata"];
    metadata_node |= ryml::MAP;

    EXPECT_TRUE(serializer.saveMetadata(metadata_node, metadata));

    EXPECT_EQ("1.8", metadata_node["manifest_version"].val());
    EXPECT_EQ("Test Emulator", metadata_node["emulator_id"].val());
    EXPECT_EQ("1.0", metadata_node["emulator_version"].val());
    EXPECT_EQ("Test Platform", metadata_node["host_platform"].val());
    EXPECT_EQ("ZX Spectrum 128K", metadata_node["emulated_platform"].val());
    EXPECT_EQ("2024-01-01T12:00:00Z", metadata_node["save_time"].val());
    EXPECT_EQ("Test Description", metadata_node["description"].val());
}

TEST_F(YamlSnapshotSerializerTest, LoadMachine)
{
    auto machine_node = root.append_child();
    machine_node.set_key("machine");
    machine_node |= ryml::MAP;
    machine_node["model"] << "PENTAGON";
    machine_node["ram_size_kb"] << 512;

    auto timing_node = machine_node["timing"];
    timing_node |= ryml::MAP;
    timing_node["preset"] << "PENTAGON";
    timing_node["frame"] << 71680;
    timing_node["line"] << 224;
    timing_node["int"] << 50;
    timing_node["intstart"] << 13;
    timing_node["intlen"] << 32;
    timing_node["total_t_states"] << 4555555555ULL;

    auto config_node = machine_node["config"];
    config_node |= ryml::MAP;
    config_node["even_m1"] << 0;
    config_node["border_4t"] << 0;
    config_node["floatbus"] << 0;
    config_node["floatdos"] << 0;
    config_node["portff"] << 0;

    auto ula_node = machine_node["ula"];
    ula_node |= ryml::MAP;
    ula_node["border_color"] << 7;
    ula_node["frame_counter"] << 54321U;
    ula_node["flash_state"] << false;
    ula_node["screen_mode"] << "SCREEN_NORMAL";
    ula_node["frame_t_states"] << 1234567U;

    MachineDTO machine;
    EXPECT_TRUE(serializer.loadMachine(machine_node, machine));

    EXPECT_EQ("PENTAGON", machine.model);
    EXPECT_EQ(512, machine.ram_size_kb);
    EXPECT_EQ("PENTAGON", machine.timing.preset);
    EXPECT_EQ(71680, machine.timing.frame);
    EXPECT_EQ(224, machine.timing.line);
    EXPECT_EQ(50, machine.timing.int_period);
    EXPECT_EQ(13, machine.timing.intstart);
    EXPECT_EQ(32, machine.timing.intlen);
    EXPECT_EQ(4555555555ULL, machine.timing.total_t_states);
    EXPECT_EQ(0, machine.config.even_m1);
    EXPECT_EQ(0, machine.config.border_4t);
    EXPECT_EQ(0, machine.config.floatbus);
    EXPECT_EQ(0, machine.config.floatdos);
    EXPECT_EQ(0, machine.config.portff);
    EXPECT_EQ(7, machine.ula.border_color);
    EXPECT_EQ(54321U, machine.ula.frame_counter);
    EXPECT_FALSE(machine.ula.flash_state);
    EXPECT_EQ("SCREEN_NORMAL", machine.ula.screen_mode);
    EXPECT_EQ(1234567U, machine.ula.frame_t_states);
}

TEST_F(YamlSnapshotSerializerTest, SaveMachine)
{
    MachineDTO machine;
    machine.model = "PENTAGON";
    machine.ram_size_kb = 512;
    machine.timing.preset = "PENTAGON";
    machine.timing.frame = 71680;
    machine.timing.line = 224;
    machine.timing.int_period = 50;
    machine.timing.intstart = 13;
    machine.timing.intlen = 32;
    machine.timing.total_t_states = 4555555555ULL;
    machine.config.even_m1 = 0;
    machine.config.border_4t = 0;
    machine.config.floatbus = 0;
    machine.config.floatdos = 0;
    machine.config.portff = 0;
    machine.ula.border_color = 7;
    machine.ula.frame_counter = 54321U;
    machine.ula.flash_state = false;
    machine.ula.screen_mode = "SCREEN_NORMAL";
    machine.ula.frame_t_states = 1234567U;

    auto machine_node = root["machine"];
    machine_node |= ryml::MAP;

    EXPECT_TRUE(serializer.saveMachine(machine_node, machine));

    EXPECT_EQ("PENTAGON", machine_node["model"].val());
    EXPECT_EQ("512", machine_node["ram_size_kb"].val());
    EXPECT_EQ("PENTAGON", machine_node["timing"]["preset"].val());
    EXPECT_EQ("71680", machine_node["timing"]["frame"].val());
    EXPECT_EQ("224", machine_node["timing"]["line"].val());
    EXPECT_EQ("50", machine_node["timing"]["int"].val());
    EXPECT_EQ("13", machine_node["timing"]["intstart"].val());
    EXPECT_EQ("32", machine_node["timing"]["intlen"].val());
    EXPECT_EQ("4555555555", machine_node["timing"]["total_t_states"].val());
    EXPECT_EQ("0", machine_node["config"]["even_m1"].val());
    EXPECT_EQ("0", machine_node["config"]["border_4t"].val());
    EXPECT_EQ("0", machine_node["config"]["floatbus"].val());
    EXPECT_EQ("0", machine_node["config"]["floatdos"].val());
    EXPECT_EQ("0", machine_node["config"]["portff"].val());
    EXPECT_EQ("7", machine_node["ula"]["border_color"].val());
    EXPECT_EQ("54321", machine_node["ula"]["frame_counter"].val());
    EXPECT_EQ("false", machine_node["ula"]["flash_state"].val());
    EXPECT_EQ("SCREEN_NORMAL", machine_node["ula"]["screen_mode"].val());
    EXPECT_EQ("1234567", machine_node["ula"]["frame_t_states"].val());
}

TEST_F(YamlSnapshotSerializerTest, LoadMemory)
{
    auto memory_node = root.append_child();
    memory_node.set_key("memory");
    memory_node |= ryml::MAP;

    auto pages_node = memory_node["pages"];
    pages_node |= ryml::MAP;

    auto page1_node = pages_node["ram_0"];
    page1_node |= ryml::MAP;
    page1_node["file"] << "ram_0.bin";
    auto checksum1_node = page1_node["checksum"];
    checksum1_node |= ryml::MAP;
    checksum1_node["algorithm"] << "crc32";
    checksum1_node["value"] << "0x12345678";

    auto page2_node = pages_node["rom_0"];
    page2_node |= ryml::MAP;
    page2_node["file"] << "rom_0.bin";
    auto checksum2_node = page2_node["checksum"];
    checksum2_node |= ryml::MAP;
    checksum2_node["algorithm"] << "crc32";
    checksum2_node["value"] << "0x87654321";

    auto memory_map_node = memory_node["memory_map"];
    memory_map_node |= ryml::MAP;

    auto map1_node = memory_map_node["0x0000"];
    map1_node |= ryml::MAP;
    map1_node["type"] << "ROM";
    map1_node["bank"] << 0;

    auto map2_node = memory_map_node["0x4000"];
    map2_node |= ryml::MAP;
    map2_node["type"] << "RAM";
    map2_node["bank"] << 5;

    auto ports_node = memory_node["ports"];
    ports_node |= ryml::MAP;
    ports_node["0xFE"] << 0x7F;
    ports_node["0xFF"] << 0xFF;

    MemoryDTO memory;
    EXPECT_TRUE(serializer.loadMemory(memory_node, memory));

    EXPECT_EQ(2, memory.pages.size());
    EXPECT_EQ("ram_0.bin", memory.pages["ram_0"].file);
    EXPECT_EQ("crc32", memory.pages["ram_0"].checksum.algorithm);
    EXPECT_EQ("0x12345678", memory.pages["ram_0"].checksum.value);
    EXPECT_EQ("rom_0.bin", memory.pages["rom_0"].file);
    EXPECT_EQ("crc32", memory.pages["rom_0"].checksum.algorithm);
    EXPECT_EQ("0x87654321", memory.pages["rom_0"].checksum.value);

    EXPECT_EQ(2, memory.memory_map.size());
    EXPECT_EQ("ROM", memory.memory_map["0x0000"].type);
    EXPECT_EQ(0, memory.memory_map["0x0000"].bank);
    EXPECT_EQ("RAM", memory.memory_map["0x4000"].type);
    EXPECT_EQ(5, memory.memory_map["0x4000"].bank);

    EXPECT_EQ(2, memory.ports.size());
    EXPECT_EQ(0x7F, memory.ports["0xFE"]);
    EXPECT_EQ(0xFF, memory.ports["0xFF"]);
}

TEST_F(YamlSnapshotSerializerTest, SaveMemory)
{
    MemoryDTO memory;

    MemoryPageDTO page1;
    page1.file = "ram_0.bin";
    page1.checksum.algorithm = "crc32";
    page1.checksum.value = "0x12345678";
    memory.pages["ram_0"] = page1;

    MemoryPageDTO page2;
    page2.file = "rom_0.bin";
    page2.checksum.algorithm = "crc32";
    page2.checksum.value = "0x87654321";
    memory.pages["rom_0"] = page2;

    MemoryMapEntryDTO map1;
    map1.type = "ROM";
    map1.bank = 0;
    memory.memory_map["0x0000"] = map1;

    MemoryMapEntryDTO map2;
    map2.type = "RAM";
    map2.bank = 5;
    memory.memory_map["0x4000"] = map2;

    memory.ports["0xFE"] = 0x7F;
    memory.ports["0xFF"] = 0xFF;

    auto memory_node = root["memory"];
    memory_node |= ryml::MAP;

    EXPECT_TRUE(serializer.saveMemory(memory_node, memory));

    EXPECT_EQ("ram_0.bin", memory_node["pages"]["ram_0"]["file"].val());
    EXPECT_EQ("crc32", memory_node["pages"]["ram_0"]["checksum"]["algorithm"].val());
    EXPECT_EQ("0x12345678", memory_node["pages"]["ram_0"]["checksum"]["value"].val());
    EXPECT_EQ("rom_0.bin", memory_node["pages"]["rom_0"]["file"].val());
    EXPECT_EQ("crc32", memory_node["pages"]["rom_0"]["checksum"]["algorithm"].val());
    EXPECT_EQ("0x87654321", memory_node["pages"]["rom_0"]["checksum"]["value"].val());

    EXPECT_EQ("ROM", memory_node["memory_map"]["0x0000"]["type"].val());
    EXPECT_EQ("0", memory_node["memory_map"]["0x0000"]["bank"].val());
    EXPECT_EQ("RAM", memory_node["memory_map"]["0x4000"]["type"].val());
    EXPECT_EQ("5", memory_node["memory_map"]["0x4000"]["bank"].val());

    EXPECT_EQ("127", memory_node["ports"]["0xFE"].val());
    EXPECT_EQ("255", memory_node["ports"]["0xFF"].val());
}

TEST_F(YamlSnapshotSerializerTest, LoadZ80)
{
    auto z80_node = root.append_child();
    z80_node.set_key("z80");
    z80_node |= ryml::MAP;

    auto registers_node = z80_node["registers"];
    registers_node |= ryml::MAP;
    registers_node["af"] << 0x1234;
    registers_node["bc"] << 0x5678;
    registers_node["de"] << 0x9ABC;
    registers_node["hl"] << 0xDEF0;
    registers_node["af_"] << 0xA1B2;
    registers_node["bc_"] << 0xC3D4;
    registers_node["de_"] << 0xE5F6;
    registers_node["hl_"] << 0x1357;
    registers_node["ix"] << 0x2468;
    registers_node["iy"] << 0x369A;
    registers_node["pc"] << 0x482B;
    registers_node["sp"] << 0x5ACE;
    registers_node["i"] << 0x99;
    registers_node["r"] << 0xAA;

    auto interrupts_node = z80_node["interrupts"];
    interrupts_node |= ryml::MAP;
    interrupts_node["im"] << 1;
    interrupts_node["iff1"] << true;
    interrupts_node["iff2"] << false;
    interrupts_node["halted"] << false;

    Z80DTO z80;
    EXPECT_TRUE(serializer.loadZ80(z80_node, z80));

    EXPECT_EQ(0x1234, z80.registers.af);
    EXPECT_EQ(0x5678, z80.registers.bc);
    EXPECT_EQ(0x9ABC, z80.registers.de);
    EXPECT_EQ(0xDEF0, z80.registers.hl);
    EXPECT_EQ(0xA1B2, z80.registers.af_);
    EXPECT_EQ(0xC3D4, z80.registers.bc_);
    EXPECT_EQ(0xE5F6, z80.registers.de_);
    EXPECT_EQ(0x1357, z80.registers.hl_);
    EXPECT_EQ(0x2468, z80.registers.ix);
    EXPECT_EQ(0x369A, z80.registers.iy);
    EXPECT_EQ(0x482B, z80.registers.pc);
    EXPECT_EQ(0x5ACE, z80.registers.sp);
    EXPECT_EQ(0x99, z80.registers.i);
    EXPECT_EQ(0xAA, z80.registers.r);

    EXPECT_EQ(1, z80.interrupts.im);
    EXPECT_TRUE(z80.interrupts.iff1);
    EXPECT_FALSE(z80.interrupts.iff2);
    EXPECT_FALSE(z80.interrupts.halted);
}

TEST_F(YamlSnapshotSerializerTest, SaveZ80)
{
    Z80DTO z80;
    z80.registers.af = 0x1234;
    z80.registers.bc = 0x5678;
    z80.registers.de = 0x9ABC;
    z80.registers.hl = 0xDEF0;
    z80.registers.af_ = 0xA1B2;
    z80.registers.bc_ = 0xC3D4;
    z80.registers.de_ = 0xE5F6;
    z80.registers.hl_ = 0x1357;
    z80.registers.ix = 0x2468;
    z80.registers.iy = 0x369A;
    z80.registers.pc = 0x482B;
    z80.registers.sp = 0x5ACE;
    z80.registers.i = 0x99;
    z80.registers.r = 0xAA;
    z80.interrupts.im = 1;
    z80.interrupts.iff1 = true;
    z80.interrupts.iff2 = false;
    z80.interrupts.halted = false;

    auto z80_node = root["z80"];
    z80_node |= ryml::MAP;

    EXPECT_TRUE(serializer.saveZ80(z80_node, z80));

    EXPECT_EQ("0x1234", z80_node["registers"]["af"].val());
    EXPECT_EQ("0x5678", z80_node["registers"]["bc"].val());
    EXPECT_EQ("0x9ABC", z80_node["registers"]["de"].val());
    EXPECT_EQ("0xDEF0", z80_node["registers"]["hl"].val());
    EXPECT_EQ("0xA1B2", z80_node["registers"]["af_"].val());
    EXPECT_EQ("0xC3D4", z80_node["registers"]["bc_"].val());
    EXPECT_EQ("0xE5F6", z80_node["registers"]["de_"].val());
    EXPECT_EQ("0x1357", z80_node["registers"]["hl_"].val());
    EXPECT_EQ("0x2468", z80_node["registers"]["ix"].val());
    EXPECT_EQ("0x369A", z80_node["registers"]["iy"].val());
    EXPECT_EQ("0x482B", z80_node["registers"]["pc"].val());
    EXPECT_EQ("0x5ACE", z80_node["registers"]["sp"].val());
    EXPECT_EQ("0x99", z80_node["registers"]["i"].val());
    EXPECT_EQ("0xAA", z80_node["registers"]["r"].val());

    EXPECT_EQ("1", z80_node["interrupts"]["im"].val());
    EXPECT_EQ("true", z80_node["interrupts"]["iff1"].val());
    EXPECT_EQ("false", z80_node["interrupts"]["iff2"].val());
    EXPECT_EQ("false", z80_node["interrupts"]["halted"].val());
}

TEST_F(YamlSnapshotSerializerTest, SafeGet)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::VAL;
    test_node << "test_value";

    std::string value;
    serializer.safeGet(test_node, value, std::string("default"));
    EXPECT_EQ("test_value", value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetWithDefault)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::MAP;

    std::string value;
    serializer.safeGet(test_node["nonexistent"], value, std::string("default"));
    EXPECT_EQ("default", value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetBool)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::VAL;
    test_node << true;

    bool value;
    serializer.safeGet(test_node, value, false);
    EXPECT_TRUE(value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetBoolWithDefault)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::MAP;

    bool value;
    serializer.safeGet(test_node["nonexistent"], value, true);
    EXPECT_TRUE(value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetInt)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::VAL;
    test_node << 42;

    int value;
    serializer.safeGet(test_node, value, 0);
    EXPECT_EQ(42, value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetIntWithDefault)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::MAP;

    int value;
    serializer.safeGet(test_node["nonexistent"], value, 100);
    EXPECT_EQ(100, value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetUint16)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::VAL;
    test_node << 0x1234;

    uint16_t value;
    serializer.safeGet(test_node, value, static_cast<uint16_t>(0));
    EXPECT_EQ(0x1234, value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetUint16WithDefault)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::MAP;

    uint16_t value;
    serializer.safeGet(test_node["nonexistent"], value, static_cast<uint16_t>(0xFFFF));
    EXPECT_EQ(0xFFFF, value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetUint8)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::VAL;
    test_node << 0xAB;

    uint8_t value;
    serializer.safeGet(test_node, value, static_cast<uint8_t>(0));
    EXPECT_EQ(0xAB, value);
}

TEST_F(YamlSnapshotSerializerTest, SafeGetUint8WithDefault)
{
    auto test_node = root.append_child();
    test_node.set_key("test");
    test_node |= ryml::MAP;

    uint8_t value;
    serializer.safeGet(test_node["nonexistent"], value, static_cast<uint8_t>(0xFF));
    EXPECT_EQ(0xFF, value);
}

TEST_F(YamlSnapshotSerializerTest, LoadFullSnapshot)
{
    std::string root = YamlSnapshotSerializerTest::FindProjectRoot();
    ASSERT_FALSE(root.empty()) << "Could not find project root (missing core/data/testdata folders)";
    std::string fixture = root + "/core/tests/loaders/uns/full_snapshot_fixture.yaml";
    
    SnapshotDTO snapshot;
    YamlSnapshotSerializerCUT serializer;
    ASSERT_TRUE(serializer.load(fixture, snapshot)) << serializer.lastError();

    // Spot-check key fields from each section
    EXPECT_EQ(snapshot.metadata.manifest_version, "2.0");
    EXPECT_EQ(snapshot.machine.model, "PENTAGON");
    auto page = map_get(snapshot.memory.pages, "ram_0");
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->file, "ram_0.bin");
    EXPECT_EQ(snapshot.z80.registers.af, 0x1234);
    EXPECT_EQ(snapshot.peripherals.psg0.chip_type, "AY-3-8910");
    EXPECT_EQ(snapshot.media.floppy_drives[0].file, "disk_a.trd");
    EXPECT_EQ(snapshot.debug.label_files[0], "labels1.map");
    EXPECT_EQ(snapshot.emulator_config.features.turbo_mode, true);

    // Full check for memory pages
    // ROM pages: rom_0-rom_3, RAM pages: ram_0-ram_7
    int rom_count = 0, ram_count = 0;
    for (const auto& [name, page] : snapshot.memory.pages)
    {
        if (name.find("rom_") == 0) ++rom_count;
        if (name.find("ram_") == 0) ++ram_count;
    }
    EXPECT_EQ(4, rom_count) << "Should have 4 ROM pages";
    EXPECT_EQ(8, ram_count) << "Should have 8 RAM pages";
    EXPECT_EQ(12, snapshot.memory.pages.size()) << "Should have 12 total memory pages";

    // Check each page's file, algorithm, and value
    struct PageExpect
    {
        const char* name;
        const char* file;
        const char* algorithm;
        const char* value;
    };

    PageExpect page_expects[] =
    {
        {"rom_0", "rom_0.bin", "crc32", "0x01020304"},
        {"rom_1", "rom_1.bin", "crc32", "0x11223344"},
        {"rom_2", "rom_2.bin", "crc32", "0xA1B2C3D4"},
        {"rom_3", "rom_3.bin", "crc32", "0x55667788"},
        {"ram_0", "ram_0.bin", "crc32", "0x89ABCDEF"},
        {"ram_1", "ram_1.bin", "crc32", "0x76543210"},
        {"ram_2", "ram_2.bin", "crc32", "0xFEDCBA98"},
        {"ram_3", "ram_3.bin", "crc32", "0x13579BDF"},
        {"ram_4", "ram_4.bin", "crc32", "0x2468ACE0"},
        {"ram_5", "ram_5.bin", "crc32", "0x10293847"},
        {"ram_6", "ram_6.bin", "crc32", "0x56473829"},
        {"ram_7", "ram_7.bin", "crc32", "0xABCDEF01"},
    };

    for (const auto& exp : page_expects)
    {
        auto page = map_get(snapshot.memory.pages, exp.name);
        ASSERT_NE(page, nullptr) << "Missing page: " << exp.name;
        EXPECT_EQ(page->file, exp.file) << exp.name << ": file mismatch";
        EXPECT_EQ(page->checksum.algorithm, exp.algorithm) << exp.name << ": algo mismatch";
        EXPECT_EQ(page->checksum.value, exp.value) << exp.name << ": value mismatch";
    }
}

TEST_F(YamlSnapshotSerializerTest, RoundtripFullSnapshot)
{
    std::string root = YamlSnapshotSerializerTest::FindProjectRoot();
    ASSERT_FALSE(root.empty()) << "Could not find project root (missing core/data/testdata folders)";
    std::string fixture = root + "/core/tests/loaders/uns/full_snapshot_fixture.yaml";
    std::string outFile = root + "/core/tests/loaders/uns/roundtrip_output.yaml";
    SnapshotDTO snapshot1, snapshot2;
    YamlSnapshotSerializerCUT serializer;
    // Load from fixture
    ASSERT_TRUE(serializer.load(fixture, snapshot1)) << serializer.lastError();
    // Save to temp file
    ASSERT_TRUE(serializer.save(outFile, snapshot1)) << serializer.lastError();
    // Reload from temp file
    ASSERT_TRUE(serializer.load(outFile, snapshot2)) << serializer.lastError();
    // Spot-check equivalence
    EXPECT_EQ(snapshot2.metadata.manifest_version, snapshot1.metadata.manifest_version);
    EXPECT_EQ(snapshot2.machine.model, snapshot1.machine.model);
    auto page = map_get(snapshot2.memory.pages, "ram_0");
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->file, snapshot1.memory.pages["ram_0"].file);
    EXPECT_EQ(snapshot2.z80.registers.af, snapshot1.z80.registers.af);
    EXPECT_EQ(snapshot2.peripherals.psg0.chip_type, snapshot1.peripherals.psg0.chip_type);
    EXPECT_EQ(snapshot2.media.floppy_drives[0].file, snapshot1.media.floppy_drives[0].file);
    EXPECT_EQ(snapshot2.debug.label_files[0], snapshot1.debug.label_files[0]);
    EXPECT_EQ(snapshot2.emulator_config.features.turbo_mode, snapshot1.emulator_config.features.turbo_mode);
    // Optionally, remove the temp file
    std::remove(outFile.c_str());
}