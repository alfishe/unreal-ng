#include <gtest/gtest.h>

// ryml's default error handler
// to throw std::runtime_error instead of calling abort().
#define RYML_DEFAULT_CALLBACK_USES_EXCEPTIONS

#define RYML_SINGLE_HDR_DEFINE_NOW
#include "3rdparty/rapidyaml/ryml_all.hpp"
#include <stdexcept> // Required for std::runtime_error

using namespace ryml;

TEST(RapidyamlTest, ParseSimpleMap)
{
    // `parse_in_place` requires a mutable buffer. Use a char array instead of const char*.
    char yaml[] = "foo: 1\nbar: 2\n";
    Tree tree = parse_in_place(yaml);
    ASSERT_FALSE(tree.empty());
    ASSERT_EQ(tree.rootref().num_children(), 2u);

    auto foo = tree.rootref()["foo"];
    auto bar = tree.rootref()["bar"];
    ASSERT_TRUE(!foo.invalid());
    ASSERT_TRUE(!bar.invalid());
    EXPECT_EQ(foo.val(), "1");
    EXPECT_EQ(bar.val(), "2");
}

TEST(RapidyamlTest, ParseSequence)
{
    // `parse_in_place` requires a mutable buffer.
    char yaml[] = "- apple\n- banana\n- cherry\n";
    Tree tree = parse_in_place(yaml);
    ASSERT_FALSE(tree.empty());
    ASSERT_EQ(tree.rootref().num_children(), 3u);
    EXPECT_EQ(tree.rootref()[0].val(), "apple");
    EXPECT_EQ(tree.rootref()[1].val(), "banana");
    EXPECT_EQ(tree.rootref()[2].val(), "cherry");
}

TEST(RapidyamlTest, EmitSimpleMap)
{
    // `parse_in_place` requires a mutable buffer.
    char yaml[] = "foo: bar\nbaz: qux\n";
    Tree tree = parse_in_place(yaml);
    std::string out;
    // The function call signature was incorrect. It should be emitrs_yaml(tree, &out).
    emitrs_yaml(tree, &out);
    // Should contain both keys and values
    EXPECT_NE(out.find("foo"), std::string::npos);
    EXPECT_NE(out.find("bar"), std::string::npos);
    EXPECT_NE(out.find("baz"), std::string::npos);
    EXPECT_NE(out.find("qux"), std::string::npos);
}

TEST(RapidyamlTest, InvalidYamlThrows)
{
    char yaml[] = "foo: [1, 2\nbar: 3\n";  // missing closing ]

    // Catch the correct exception type, which is std::runtime_error
    // when using the default callbacks with exceptions enabled.
    EXPECT_THROW({
        (void)parse_in_place(yaml);
    }, std::runtime_error);
}

/*
// ============================================================================
// RAPIDYAML USAGE EXAMPLES - EMULATOR SNAPSHOT YAML MANIFEST
// ============================================================================

// Example YAML manifest for an emulator snapshot:
char snapshot_yaml[] = R"(
metadata:
  version: "1.0"
  created: "2024-01-15T14:30:00Z"
  emulator: "unreal-ng"
  platform: "ZX Spectrum 128K"
  description: "Manic Miner save state"

memory:
  ram_file: "memory_0000_FFFF.bin"
  ram_size: 65536
  rom_file: "128k_rom.bin"
  rom_size: 16384
  memory_map:
    - start: 0x0000
      end: 0x3FFF
      type: "ROM"
      file: "128k_rom.bin"
    - start: 0x4000
      end: 0x7FFF
      type: "RAM"
      bank: 5
    - start: 0x8000
      end: 0xBFFF
      type: "RAM"
      bank: 2
    - start: 0xC000
      end: 0xFFFF
      type: "RAM"
      bank: 0

registers:
  z80:
    af: 0x1234
    bc: 0x5678
    de: 0x9ABC
    hl: 0xDEF0
    af_prime: 0x1111
    bc_prime: 0x2222
    de_prime: 0x3333
    hl_prime: 0x4444
    ix: 0x5555
    iy: 0x6666
    sp: 0x8000
    pc: 0x4000
    i: 0x3F
    r: 0x7F
    iff1: true
    iff2: true
    im: 1

peripherals:
  ula:
    border_color: 2
    screen_mode: 0
    flash_state: false
    frame_counter: 12345
  ay_8910:
    enabled: true
    register_file: "ay_registers.bin"
    volume: 0.8
  keyboard:
    matrix: [0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF]
  joystick:
    type: "kempston"
    state: 0x00

storage:
  disk_images:
    - name: "ManicMiner.dsk"
      type: "TRD"
      file: "disks/ManicMiner.trd"
      drive: "A"
      write_protected: false
    - name: "System.dsk"
      type: "TRD"
      file: "disks/System.trd"
      drive: "B"
      write_protected: true
  tape:
    name: "ManicMiner.tap"
    file: "tapes/ManicMiner.tap"
    position: 1234
    playing: false
    motor: false

screenshots:
  thumbnail:
    file: "screenshots/thumbnail.png"
    width: 320
    height: 240
    format: "PNG"
  fullscreen:
    file: "screenshots/fullscreen.png"
    width: 256
    height: 192
    format: "PNG"
    timestamp: "2024-01-15T14:30:00Z"

debug:
  breakpoints:
    - address: 0x4000
      enabled: true
      condition: "A == 0x12"
    - address: 0x8000
      enabled: false
      condition: ""
  watchpoints:
    - address: 0xC000
      size: 1
      read: true
      write: false
  call_stack:
    - pc: 0x4000
      sp: 0x8000
    - pc: 0x1234
      sp: 0x7FFE
)";

// ============================================================================
// PARSING EXAMPLES
// ============================================================================

// 1. Basic parsing and metadata access:
Tree tree = parse_in_place(snapshot_yaml);
auto metadata = tree["metadata"];
std::string version = metadata["version"].val();
std::string platform = metadata["platform"].val();

// 2. Accessing nested memory map:
auto memory_map = tree["memory"]["memory_map"];
for (auto region : memory_map) {
    uint16_t start = region["start"].val<uint16_t>();
    uint16_t end = region["end"].val<uint16_t>();
    std::string type = region["type"].val();
    // Process each memory region...
}

// 3. Parsing register values:
auto z80_regs = tree["registers"]["z80"];
uint16_t af = z80_regs["af"].val<uint16_t>();
uint16_t pc = z80_regs["pc"].val<uint16_t>();
bool iff1 = z80_regs["iff1"].val<bool>();

// 4. Accessing peripheral state:
auto ula = tree["peripherals"]["ula"];
uint8_t border = ula["border_color"].val<uint8_t>();
bool flash = ula["flash_state"].val<bool>();

// 5. Iterating through disk images:
auto disk_images = tree["storage"]["disk_images"];
for (auto disk : disk_images) {
    std::string name = disk["name"].val();
    std::string file = disk["file"].val();
    std::string drive = disk["drive"].val();
    bool write_protected = disk["write_protected"].val<bool>();
    // Load disk image...
}

// 6. Accessing keyboard matrix:
auto keyboard = tree["peripherals"]["keyboard"];
auto matrix = keyboard["matrix"];
std::vector<uint8_t> key_matrix;
for (auto key : matrix) {
    key_matrix.push_back(key.val<uint8_t>());
}

// 7. Getting screenshot info:
auto thumbnail = tree["screenshots"]["thumbnail"];
std::string thumb_file = thumbnail["file"].val();
uint32_t width = thumbnail["width"].val<uint32_t>();
uint32_t height = thumbnail["height"].val<uint32_t>();

// 8. Parsing breakpoints:
auto breakpoints = tree["debug"]["breakpoints"];
for (auto bp : breakpoints) {
    uint16_t addr = bp["address"].val<uint16_t>();
    bool enabled = bp["enabled"].val<bool>();
    std::string condition = bp["condition"].val();
    // Set breakpoint...
}

// ============================================================================
// CREATING/MODIFYING EXAMPLES
// ============================================================================

// 9. Creating a new snapshot tree:
Tree new_snapshot;
auto root = new_snapshot.rootref();

// Add metadata
root |= ryml::MAP;
root["metadata"] |= ryml::MAP;
root["metadata"]["version"] = "1.0";
root["metadata"]["created"] = "2024-01-15T14:30:00Z";
root["metadata"]["emulator"] = "unreal-ng";

// Add registers
root["registers"] |= ryml::MAP;
root["registers"]["z80"] |= ryml::MAP;
root["registers"]["z80"]["pc"] = 0x4000;
root["registers"]["z80"]["sp"] = 0x8000;
root["registers"]["z80"]["af"] = 0x1234;

// Add memory map
root["memory"] |= ryml::MAP;
root["memory"]["memory_map"] |= ryml::SEQ;
auto mem_region = root["memory"]["memory_map"].append_child();
mem_region |= ryml::MAP;
mem_region["start"] = 0x0000;
mem_region["end"] = 0x3FFF;
mem_region["type"] = "ROM";

// 10. Emitting the modified tree:
std::string output;
emitrs_yaml(new_snapshot, &output);
// output now contains the YAML representation

// ============================================================================
// ERROR HANDLING EXAMPLES
// ============================================================================

// 11. Safe access with validation:
auto safe_get_reg = [&tree](const char* reg_name) -> uint16_t {
    auto regs = tree["registers"]["z80"];
    if (!regs.valid()) {
        throw std::runtime_error("Z80 registers not found");
    }
    auto reg = regs[reg_name];
    if (!reg.valid()) {
        throw std::runtime_error(std::string("Register ") + reg_name + " not found");
    }
    return reg.val<uint16_t>();
};

uint16_t pc = safe_get_reg("pc");

// 12. Iterating with bounds checking:
auto disk_images = tree["storage"]["disk_images"];
if (disk_images.valid() && disk_images.is_seq()) {
    for (size_t i = 0; i < disk_images.num_children(); ++i) {
        auto disk = disk_images[i];
        if (disk.valid()) {
            std::string name = disk["name"].val();
            // Process disk...
        }
    }
}

// ============================================================================
// TYPE CONVERSION EXAMPLES
// ============================================================================

// 13. Converting string values to different types:
auto value = tree["some"]["nested"]["value"];
int int_val = value.val<int>();
double double_val = value.val<double>();
bool bool_val = value.val<bool>();

// 14. Handling optional values:
auto optional_reg = tree["registers"]["z80"]["optional_reg"];
if (optional_reg.valid()) {
    uint16_t val = optional_reg.val<uint16_t>();
    // Use the value...
} else {
    // Handle missing value...
}

// ============================================================================
// PERFORMANCE TIPS
// ============================================================================

// 15. Reserve capacity for large trees:
Tree large_tree;
large_tree.reserve(1000);  // Reserve space for 1000 nodes
large_tree.reserve_arena(1024 * 1024);  // Reserve 1MB for string storage

// 16. Parse in place for better performance:
char mutable_yaml[] = "key: value\n";
Tree tree = parse_in_place(mutable_yaml);  // Modifies the input buffer

// 17. Use string views to avoid copies:
csubstr yaml_view = "key: value";
Tree tree = parse_in_place(yaml_view);  // No copying of the input string
*/

