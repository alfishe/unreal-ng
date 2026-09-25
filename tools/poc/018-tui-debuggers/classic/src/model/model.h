// model.h - debugger data model (TDD-DBG-01 §13 "Data contract").
//
// These structs are the complete emulator-side state the screen is a
// function of. A remote debugger protocol that serves this UI MUST provide
// at least these fields, per emulator instance and per CPU. The transport
// seam (backend/debugger-backend.h) exposes exactly this model; mock and
// remote backends fill the same structs.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dbg {

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------

struct Z80Regs {
    uint8_t a = 0;
    uint8_t f = 0;
    uint16_t bc = 0;
    uint16_t de = 0;
    uint16_t hl = 0;
    uint16_t af2 = 0;  // AF'
    uint16_t bc2 = 0;  // BC'
    uint16_t de2 = 0;  // DE'
    uint16_t hl2 = 0;  // HL'
    uint16_t sp = 0;
    uint16_t pc = 0;
    uint16_t ix = 0;
    uint16_t iy = 0;
    uint8_t i = 0;
    uint8_t r = 0;  // full 8-bit R (R7 merged)
    uint8_t im = 0;  // 0, 1, 2
    uint8_t iff1 = 0;
    uint8_t iff2 = 0;
    bool halted = false;
    uint32_t t = 0;             // T-states since the start of the current frame
    uint16_t lastBranch = 0;    // last executed control transfer
    uint16_t intVector = 0x38;  // IM0/IM2 vector base (I<<8 | data bus)

    bool operator==(const Z80Regs&) const = default;
};

// Branch analysis of the instruction at PC (§4.2.4).
enum BranchFlag : unsigned {
    kBranchNone = 0,
    kBranchTaken = 1u << 0,   // BRANCH
    kBranchAddr = 1u << 1,    // BRADDR: target is computed (RET/JP (HL)/HALT-IM2)
    kBranchCall = 1u << 2,    // CALLCMD
    kBranchLoop = 1u << 3,    // LOOPCMD
    kBranchBlk = 1u << 4,     // BLKCMD (LDIR.. and, in the fork, HALT)
    kBranchHalt = 1u << 5,    // HALTCMD (classic only)
};

struct BranchInfo {
    unsigned flags = kBranchNone;
    uint16_t target = 0;  // valid when taken
    uint16_t nextPc = 0;  // HALTCMD: interrupt target; else PC + len
};

// One disassembled line as served by the backend (§13 disasm()).
struct DisasmLine {
    uint16_t addr = 0;
    int len = 0;
    std::vector<uint8_t> bytes;
    std::string mnemonic;                 // Unreal format, operands not label-substituted
    std::optional<std::string> label;     // when labels are on and one is attached
    std::string MnemonicText() const {    // operands with label substitution applied
        return mnemonic;
    }
};

// ---------------------------------------------------------------------------
// Peripherals (§13 ports/beta128/ay/pages)
// ---------------------------------------------------------------------------

struct PortsState {
    uint8_t fe = 0;        // last value written to #FE
    uint8_t p7ffd = 0;     // last value written to #7FFD
    bool lock48 = false;   // 7FFD bit 5 (48K lock) display condition
    bool hasExtPort = false;
    uint16_t extPort = 0;
    uint8_t extValue = 0;
    uint8_t cmosAddr = 0;  // CMOS address register (no ext port line)
    uint8_t eff7 = 0;      // last value written to #EFF7
    bool dosPortsActive = false;  // TR-DOS ROM paged / DOS ports enabled
};

struct Beta128State {
    bool present = false;
    uint8_t cmd = 0;        // WD command register
    uint8_t data = 0;       // WD data register
    uint8_t statusRead = 0; // status as read by the CPU (HLD merged, classic)
    uint8_t statusRaw = 0;  // raw WD status register (fork variant)
    uint8_t sector = 0;
    uint8_t headTrack = 0;  // selected drive's physical head track
    uint8_t track = 0;      // WD track register
    uint8_t system = 0;     // #FF write: drive select/side/HLT/reset/density
    uint8_t rqs = 0;        // #FF read: bit7 INTRQ, bit6 DRQ
};

enum class AyScheme { None, Single, TurboSound };

struct AyState {
    AyScheme scheme = AyScheme::None;
    int activeChip = 0;          // TurboSound/quadro selection (Alt+Y)
    std::array<std::array<uint8_t, 16>, 2> regs{};
    std::array<int, 2> latchedReg{0, 0};  // last write to #FFFD per chip

    const std::string& ChipLabel() const {
        static const std::string kAy = "AY:";
        static const std::string kAy0 = "AY0";
        static const std::string kAy1 = "AY1";
        if (scheme == AyScheme::Single) return kAy;
        return activeChip == 0 ? kAy0 : kAy1;
    }
};

struct PageInfo {
    std::string name;   // 5 chars, e.g. "BASIC", "RAM 5", "TRDOS", "SVM  "
    bool readOnly = false;  // read != write mapping (ROM / write-protected)
};

// ---------------------------------------------------------------------------
// Machine-wide info
// ---------------------------------------------------------------------------

enum class MemoryModelHint { Pentagon, Standard128, Scorpion, Profi, Atm450, Atm710, Kay, Quorum, Gmx, Plus3, ProfiScorpion, Phoenix, TsConf };

struct MachineInfo {
    int cpuCount = 1;             // main Z80 (+ GS Z80 = 2 when compiled in)
    int cpuIndex = 0;             // current CPU
    MemoryModelHint model = MemoryModelHint::Pentagon;
    AyScheme ayScheme = AyScheme::Single;
    bool trdosPresent = false;
    bool gsPresent = false;       // General Sound Z80
    uint32_t gsDmaAddr = 0;       // shown in memory titles even when absent
    std::array<PageInfo, 4> pages{};
    uint32_t frameLength = 69888; // T-states per frame
    uint32_t intStart = 0;        // INT window start within frame
    uint32_t intLength = 0;
};

// ---------------------------------------------------------------------------
// Editors (§4.4.1)
// ---------------------------------------------------------------------------

enum class EditorSpace {
    Mem,       // 0x10000, live 16-bit map
    DiskPhys,  // raw MFM track image
    DiskLog,   // concatenated sector data of a track
    Cmos,      // 256
    Nvram,     // 2048
    CompPal,   // 64 (classic only; the fork has no comppal)
};

struct DiskSectorData {
    uint8_t id = 0;      // sector ID R
    uint16_t length = 0;
    std::vector<uint8_t> data;
};

struct DiskTrackData {
    bool present = false;  // false -> "track not found"
    std::vector<uint8_t> physical;             // raw MFM bytes (DiskPhys)
    std::vector<DiskSectorData> sectors;       // logical view (DiskLog)
};

// ---------------------------------------------------------------------------
// Snapshot-level aggregate used by the UI repaint
// ---------------------------------------------------------------------------

struct ScreenPixels {
    // 296x208 centre crop of the rendered frame, ZX palette indices.
    int width = 0;
    int height = 0;
    std::vector<uint8_t> index;  // palette index per pixel
};

struct DebuggerSnapshot {
    MachineInfo machine;
    Z80Regs regs;         // current CPU
    Z80Regs prevRegs;     // previous snapshot (§9.5) for change highlighting
    int64_t timeDelta = 0;
    BranchInfo branchAtPc;
    PortsState ports;
    Beta128State beta128;
    AyState ay;
};

}  // namespace dbg
