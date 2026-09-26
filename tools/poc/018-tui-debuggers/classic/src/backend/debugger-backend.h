// debugger-backend.h - THE TRANSPORT SEAM.
//
// IDebuggerBackend is the semantic interface defined by the TDD-DBG-01 §13
// "Data contract": every value the screen can show and every command the UI
// can issue flows through these methods. The UI never talks to an emulator
// directly; it talks to a backend. Backends:
//
//   MockBackend      - vendored z80ex machine + the sample test set (default;
//                      also the executable oracle for the golden dumps)
//   RestBackend      - unreal-ng WebAPI REST client  (--backend rest)
//   WebSocketBackend - push/event-capable client      (planned, stub)
//   IpcBackend       - shared-memory + pipes transport (planned, stub)
//
// Swapping the transport means constructing a different backend instance;
// nothing in the UI layer changes. Remote clients SHOULD request a full
// snapshot after every stop event, every mutating command, and any cursor
// change that needs new memory windows (§9.3) - the UI layer follows that
// policy for every backend, so local and remote behave identically.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/model.h"

namespace dbg {

// Result of an Assemble() call: bytes or a human-readable error.
struct AssembleResult {
    bool ok = false;
    std::vector<uint8_t> bytes;
    std::string error;  // empty when ok
};

// Result of Find(): address or "not found".
struct FindResult {
    bool found = false;
    uint16_t address = 0;
};

// Expression compile result (§10.2 conditional breakpoints).
struct ExprCompileResult {
    bool ok = false;
    std::string error;  // "Error in expression\nPlease do RTFM" style text
};

// Byte-code buffer returned for find dialogs (CP866 bytes for text search).
using BytePattern = std::vector<uint8_t>;

class IDebuggerBackend {
public:
    virtual ~IDebuggerBackend() = default;

    virtual std::string Name() const = 0;

    // -- machine / snapshot -------------------------------------------------
    virtual MachineInfo GetMachineInfo() = 0;
    virtual Z80Regs GetRegs(int cpu) = 0;
    virtual BranchInfo GetBranchAtPc(int cpu) = 0;  // §4.2.4 at paint time
    virtual int64_t GetTimeDelta(int cpu) = 0;      // (frame_start_T+t)-mark

    // -- memory (side-effect-free reads through the live 16-bit map) --------
    virtual std::vector<uint8_t> ReadMemory(int cpu, uint16_t addr, uint16_t len) = 0;
    virtual void WriteMemory(int cpu, uint16_t addr, const std::vector<uint8_t>& bytes) = 0;

    // -- disassembly ---------------------------------------------------------
    // Serve n lines starting at addr; each line follows the previous + len.
    virtual std::vector<DisasmLine> Disassemble(int cpu, uint16_t addr, int nLines,
                                                bool withLabels) = 0;
    // Backward heuristic (§4.2.5): disassemble forward from max(addr-16,0)
    // and return the start of the last instruction that begins before addr.
    virtual uint16_t PrevInstruction(int cpu, uint16_t addr) = 0;

    // -- peripherals ----------------------------------------------------------
    virtual PortsState GetPorts() = 0;
    virtual Beta128State GetBeta128() = 0;
    virtual AyState GetAy() = 0;
    // Alt+Y: which TurboSound chip the panel shows and BFFD writes target.
    virtual void SelectAyChip(int chip) = 0;

    // -- editor spaces (§4.4.1) ----------------------------------------------
    virtual std::vector<uint8_t> ReadCmos() = 0;     // 256
    virtual std::vector<uint8_t> ReadNvram() = 0;    // 2048
    virtual std::vector<uint8_t> ReadCompPal() = 0;  // 64 (classic only)
    virtual DiskTrackData ReadDiskTrack(int drive, int track) = 0;  // A-D, 0..9F
    virtual void WriteDiskByte(int drive, int track, bool logical, int offset,
                               uint8_t value) = 0;  // marks image modified (+CRC in logical)

    // -- screen preview (§4.3.2) ---------------------------------------------
    // mode 1: "screen memory" render; mode 2: "ray-painted" up to current beam.
    virtual ScreenPixels GetScreenPreview(int mode) = 0;

    // -- execution control (§9.4) ---------------------------------------------
    // Stop conditions the machine checks while running (§9.1).
    struct StopRequest {
        bool hasStopHere = false;      // PC == dbg_stophere (run-to, step-over)
        uint16_t stopHere = 0;
        bool hasStopSp = false;        // SP == dbg_stopsp and PC outside loop range,
        uint32_t stopSp = 0;           // or PC within 256 bytes after stop address
        uint16_t loopR1 = 0;           // (step-over-call safety net)
        uint16_t loopR2 = 0;
        uint64_t maxInstructions = 50'000'000;  // mock safety cap
    };
    enum class StopReason {
        Breakpoint,      // execution BPX / memory R/W BP / conditional true
        StopHere,
        StopSp,
        MaxInstructions,
    };

    // Single instruction + pending INT delivery + end-of-frame handling.
    // Returns the stop reason if a condition fired at the new PC boundary
    // (breakpoint, condition), else nullopt (debugger may continue stepping).
    virtual std::optional<StopReason> Step(int cpu) = 0;

    // Resume emulation until any §9.1 trigger fires. Remote backends map
    // this to a run command plus the pushed "stopped" event; the mock runs
    // the vendored z80ex in-process. Emulator-side breakpoints (BPX/BPR/
    // BPW/conditions) are always checked; StopRequest adds the UI targets.
    virtual StopReason RunUntilBreak(int cpu, const StopRequest& request) = 0;

    // Absolute T-states since machine reset (t within frame = abs % frameLength).
    virtual uint64_t GetAbsoluteT(int cpu) = 0;
    virtual void SetDebugMark(int cpu, uint64_t absT) = 0;  // time-delta mark

    // -- register/memory writes ------------------------------------------------
    enum class RegField {
        A, F, Bc, De, Hl, Af2, Bc2, De2, Hl2, Sp, Pc, Ix, Iy, I, R, Im, Iff1, Iff2
    };
    virtual void WriteReg(int cpu, RegField field, unsigned value) = 0;

    // -- ports ------------------------------------------------------------------
    virtual void Out(uint16_t port, uint8_t value) = 0;  // full side effects
    // Fast paths used by Alt+B / Alt+M (remap the banks afterwards).
    virtual void Set7ffd(uint8_t value) = 0;
    virtual void SetExtPort(uint8_t value) = 0;  // no-op when model has none

    // -- breakpoints ------------------------------------------------------------
    // Address bits: bit0 BPX (execute), bit1 BPR (read), bit2 BPW (write),
    // plus the ripper/coverage R/W/X trace bits (bit3..).
    virtual uint8_t BpBitsAt(uint16_t addr) = 0;
    virtual void SetBpBits(uint16_t addr, uint8_t bits) = 0;
    virtual bool HasAnyBreakpoints() = 0;
    // Conditional breakpoints, compiled expression language (§10.2).
    virtual ExprCompileResult AddCondition(int cpu, const std::string& text) = 0;
    virtual void DeleteCondition(int cpu, int index) = 0;
    virtual std::vector<std::string> GetConditions(int cpu) = 0;  // decompiled form
    virtual int ConditionCount(int cpu) = 0;

    // -- assemble / find ----------------------------------------------------------
    virtual AssembleResult Assemble(uint16_t addr, const std::string& text) = 0;
    virtual FindResult FindBytes(EditorSpace space, uint16_t start, const BytePattern& pattern,
                                 const BytePattern& mask) = 0;  // mask may be empty (all FF)

    // -- labels (§11) -----------------------------------------------------------------
    virtual std::optional<std::string> LabelFor(uint16_t addr) = 0;
    virtual std::vector<std::pair<uint16_t, std::string>> LabelsInCurrentMap() = 0;
    virtual void LoadUserLabels(const std::string& path, std::string* error,
                                int* loadedCount) = 0;
    // Import scans: return item texts + enabled flags (§11 import menu).
    struct ImportItem {
        std::string text;
        bool enabled = false;
        int count = 0;  // labels imported when selected
    };
    virtual std::vector<ImportItem> ScanLabelImports() = 0;
    virtual void ImportLabels(int itemIndex) = 0;

    // -- ripper (§6.3) ------------------------------------------------------------------
    virtual void ArmRipper(bool traceReads, bool traceWrites) = 0;
    virtual bool RipperArmed() = 0;
    virtual std::vector<uint8_t> CollectRipper(uint8_t unrefByte) = 0;  // 64K, disarms
};

// Backend factory identifiers (CLI: --backend <name> [--endpoint <url/pipe>]).
enum class BackendKind { Mock, Rest, WebSocket, Ipc };

// Creates a backend of the given kind. Mock and Rest are implemented
// ("--backend rest --endpoint http://localhost:8090"); the other remote
// kinds return nullptr until their transport lands.
std::unique_ptr<IDebuggerBackend> CreateBackend(BackendKind kind, const std::string& endpoint);

}  // namespace dbg
