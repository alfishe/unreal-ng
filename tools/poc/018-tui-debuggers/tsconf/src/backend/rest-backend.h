// rest-backend.h - IDebuggerBackend over the unreal-ng WebAPI (REST).
//
// The wire protocol is the emulator's own HTTP API (drogon, port 8090):
//
//   GET    /api/v1/emulator                       list instances
//   POST   /api/v1/emulator/start {"model"}       create + run an instance
//   DELETE /api/v1/emulator/{id}                  destroy an instance
//   GET    /api/v1/emulator/{id}                  state ("running"/"paused")
//   POST   /api/v1/emulator/{id}/pause|/resume    run control
//   GET    /api/v1/emulator/{id}/registers        nested register snapshot
//   PUT    /api/v1/emulator/{id}/registers/{name} {"value":N}
//   GET    /api/v1/emulator/{id}/memory/{addr}?len=&format=full
//   PUT    /api/v1/emulator/{id}/memory/{addr}    {"data":[...]}
//   POST   /api/v1/emulator/{id}/step             one instruction
//   POST   /api/v1/emulator/{id}/breakpoints      {"type","address"} -> id
//   DELETE /api/v1/emulator/{id}/breakpoints/{id}
//   GET/POST /api/v1/emulator/{id}/labels ...
//
// Policy (§9.3 of the TDD applies unchanged): the backend keeps a full 64K
// memory image + register snapshot cached locally and invalidates both after
// every mutating command or stop. Disassembly, branch analysis, assembling
// and byte search run over the cached image through the SAME shared helpers
// the mock uses (disasm-unreal / asm-subset), so a debugger session behaves
// identically over either transport and the wire only has to serve bytes.
//
// WebAPI gaps that are intentionally approximated (each marked in the .cpp):
// no T-state counter (time delta reads 0), no port write, no latched
// port/beta128/AY values, no conditional-breakpoint push evaluation. Those
// need the WebSocket transport and/or new server endpoints; the seam stays.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "backend/debugger-backend.h"
#include "backend/expr-lang.h"
#include "backend/http-client.h"

namespace dbg {

class RestBackend : public IDebuggerBackend {
public:
    explicit RestBackend(const std::string& baseUrl);
    ~RestBackend() override;

    std::string Name() const override { return "rest"; }

    MachineInfo GetMachineInfo() override;
    Z80Regs GetRegs(int cpu) override;
    BranchInfo GetBranchAtPc(int cpu) override;
    int64_t GetTimeDelta(int cpu) override;

    std::vector<uint8_t> ReadMemory(int cpu, uint16_t addr, uint16_t len) override;
    void WriteMemory(int cpu, uint16_t addr, const std::vector<uint8_t>& bytes) override;

    std::vector<DisasmLine> Disassemble(int cpu, uint16_t addr, int nLines,
                                        bool withLabels) override;
    uint16_t PrevInstruction(int cpu, uint16_t addr) override;

    PortsState GetPorts() override;
    Beta128State GetBeta128() override;
    AyState GetAy() override;
    void SelectAyChip(int chip) override;

    std::vector<uint8_t> ReadCmos() override;
    std::vector<uint8_t> ReadNvram() override;
    std::vector<uint8_t> ReadCompPal() override;
    DiskTrackData ReadDiskTrack(int drive, int track) override;
    void WriteDiskByte(int drive, int track, bool logical, int offset, uint8_t value) override;
    ScreenPixels GetScreenPreview(int mode) override;

    std::optional<StopReason> Step(int cpu) override;
    StopReason RunUntilBreak(int cpu, const StopRequest& request) override;
    uint64_t GetAbsoluteT(int cpu) override;
    void SetDebugMark(int cpu, uint64_t absT) override;

    void WriteReg(int cpu, RegField field, unsigned value) override;
    void Out(uint16_t port, uint8_t value) override;
    void Set7ffd(uint8_t value) override;
    void SetExtPort(uint8_t value) override;

    uint8_t BpBitsAt(uint16_t addr) override;
    void SetBpBits(uint16_t addr, uint8_t bits) override;
    bool HasAnyBreakpoints() override;
    ExprCompileResult AddCondition(int cpu, const std::string& text) override;
    void DeleteCondition(int cpu, int index) override;
    std::vector<std::string> GetConditions(int cpu) override;
    int ConditionCount(int cpu) override;

    AssembleResult Assemble(uint16_t addr, const std::string& text) override;
    FindResult FindBytes(EditorSpace space, uint16_t start, const BytePattern& pattern,
                         const BytePattern& mask) override;

    std::optional<std::string> LabelFor(uint16_t addr) override;
    std::vector<std::pair<uint16_t, std::string>> LabelsInCurrentMap() override;
    void LoadUserLabels(const std::string& path, std::string* error,
                        int* loadedCount) override;
    std::vector<ImportItem> ScanLabelImports() override;
    void ImportLabels(int itemIndex) override;

    void ArmRipper(bool traceReads, bool traceWrites) override;
    bool RipperArmed() override;
    std::vector<uint8_t> CollectRipper(uint8_t unrefByte) override;

    // Transport status for the UI (banner text when degraded).
    bool Connected() const { return connected_; }
    const std::string& InstanceId() const { return instanceId_; }
    bool OwnsInstance() const { return ownsInstance_; }
    const std::string& LastError() const { return lastError_; }

private:
    // Attaches to the first existing instance or creates one ("128k") and
    // pauses it; refreshes all caches. Idempotent; false leaves lastError_.
    bool EnsureInstance();
    bool EnsurePaused();
    bool FetchRegs();
    bool FetchMemory();
    void RefreshLabels();

    // Instance-scoped request; non-2xx bodies land in lastError_ (see .cpp).
    HttpReply Api(const std::string& method, const std::string& subPath,
                  const std::string& body = "");

    HttpClient http_;
    std::string instanceId_;
    bool ownsInstance_ = false;   // we created it -> we delete it on destruction
    bool connected_ = false;
    std::string model_;           // server-reported short name ("128k" ...)
    std::string lastError_;

    std::vector<uint8_t> mem_;    // 64K cache of the live Z80 map
    bool memValid_ = false;
    Z80Regs regs_;
    bool regsValid_ = false;
    Z80Regs prevRegs_;            // §9.5 change highlighting across stops
    uint64_t markT_ = 0;          // SetDebugMark storage (no T source yet)

    std::array<uint8_t, 0x10000> bpBits_{};  // bit0 X, 1 R, 2 W (local truth)
    std::map<std::pair<uint16_t, int>, int> serverBpIds_;  // (addr, bit) -> id

    PortsState ports_;            // local latches (Set7ffd/SetExtPort/Out)
    AyState ay_;
    std::vector<std::pair<uint16_t, std::string>> labels_;
    std::vector<std::string> conditions_;  // compiled-ok sources (no remote eval)
    bool ripperArmed_ = false;
};

}  // namespace dbg
