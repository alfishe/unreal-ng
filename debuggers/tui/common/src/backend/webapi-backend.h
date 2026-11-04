// webapi-backend.h - IDebuggerBackend over the unreal-ng WebAPI, by REST requests or by calls on the WebSocket
// /api/v1/ws (api-transport.h: the same routes and bodies either way).
//
// The routes (drogon, port 8090):
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
//   POST   /api/v1/emulator/{id}/step             one instruction, its "stop" reason
//   POST   /api/v1/emulator/{id}/breakpoints      {"type","address"[,"address_end","condition"]} -> id
//   DELETE /api/v1/emulator/{id}/breakpoints/{id}
//   GET/POST /api/v1/emulator/{id}/labels ...
//   GET    .../video/beam, /ports, /state/paging  the frame and its length, the port latches, the pages
//   POST   .../ports/out {"port","value"}         a port write with the decoder's effects
//   GET    .../state/fdc, /state/audio/ay[/{n}]   Beta 128 (WD1793), the AY chips and their latched registers
//   GET    .../rtc/cells, /memory/region/eeprom   CMOS, NVRAM (the ZX-Evo AVR's EEPROM)
//   GET    .../disk/{d}/track/{c}/{s}[/raw], PUT .../disk/{d}/sector/{c}/{s}/{n}   the disk editor
//   GET    .../memory/page/ram/{5|7}              the screen preview
//   POST   .../profiler/memory/start|stop, GET .../profiler/memory/counters   the ripper
//
// Policy (§9.3 of the TDD applies unchanged): the backend keeps a full 64K
// memory image + register snapshot cached locally and invalidates both after
// every mutating command or stop. Disassembly, branch analysis, assembling
// and byte search run over the cached image through the SAME shared helpers
// the mock uses (disasm-unreal / asm-subset), so a debugger session behaves
// identically over either transport and the wire only has to serve bytes.
//
// The conditions (§10.2) are breakpoints of the emulator over the whole 64K: it evaluates them, every client sees
// them. Left out (each marked in the .cpp): the ATM palette editor (no region), the raw track editor's writes (no raw
// track write route), the XAS / ALASM label import scans.
#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "backend/debugger-backend.h"
#include "backend/expr-lang.h"
#include "backend/api-transport.h"

namespace dbg {

class WebApiBackend : public IDebuggerBackend {
public:
    // Over @p transport: HttpClient (REST) or WebSocketClient (calls on /api/v1/ws)
    explicit WebApiBackend(std::unique_ptr<IApiTransport> transport);
    ~WebApiBackend() override;

    std::string Name() const override { return http_->Name(); }

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
    bool PollExternalChanges() override;
    bool IsRunning() override { return running_; }
    void Continue(int cpu) override;
    void Pause() override;
    void StepOver(int cpu) override;
    void StepOut(int cpu) override;
    void RunTo(int cpu, uint16_t addr) override;

    bool Online() override { return connected_; }
    bool HasState() const override { return attachedOnce_; }
    std::string WaitingFor() const override;

    // Transport status for the UI
    bool Connected() const { return connected_; }
    const std::string& InstanceId() const { return instanceId_; }
    const std::string& LastError() const { return lastError_; }
    // Attaches now (see EnsureInstance) instead of at the first call: false when no suitable emulator answers yet
    bool Attach() { return EnsureInstance(); }
    // The server's address, for the waiting dialog
    void SetEndpoint(std::string endpoint) { endpoint_ = std::move(endpoint); }
    // Only this instance (its id or symbolic id; --emulator): the emulator window that started the debugger.
    // Empty: the first suitable one
    void SetEmulator(std::string id) { wanted_ = std::move(id); }
    // Tests: the next look for an instance and the next liveness check happen at the next call, not a second later
    void RetryNow() {
        nextAttach_ = {};
        nextAlive_ = {};
    }

protected:
    // Attaches to the first running instance this debugger suits (Suitable) and pauses it; refreshes all caches.
    // Never starts one: without a suitable instance it waits, asking again at most once a second. Idempotent
    bool EnsureInstance();
    // The machines this debugger reads (the model's short name); all by default
    virtual bool Suitable(const std::string& model) const {
        (void)model;
        return true;
    }
    // "an emulator", "a TS-Conf emulator": the dialog's words for Suitable
    virtual std::string SuitableText() const { return "an emulator"; }
    bool EnsurePaused();
    // The registers, then the beam (absolute T), the pages and the port latches: one snapshot (Snapshots counts them)
    bool FetchRegs();
    bool FetchMemory();
    void RefreshLabels();

    // Instance-scoped request; non-2xx bodies land in lastError_ (see .cpp).
    HttpReply Api(const std::string& method, const std::string& subPath,
                  const std::string& body = "");
    // Counts the register snapshots taken: a cache of machine state is stale once it moves
    uint64_t Snapshots() const { return snapshots_; }

private:
    // The four windows' page titles (GET /state/paging), asked with the registers
    void FetchPaging();
    // The ports' latched values (GET /ports), asked with the registers
    void FetchPorts();
    // The emulator's breakpoints (GET /breakpoints) as the local bits: another client may have set or removed some
    void SyncBreakpoints();
    // The machine again after a change of others: the registers, and when they moved, the memory and the labels
    bool RefreshAfterForeignChange();
    // The machine after a command that ran it here (/stepover, /stepout): everything read again
    void RefreshAfterRun();
    // The run-to breakpoint goes at the stop after it (its group, see RunTo)
    void DropRunTo();

    std::unique_ptr<IApiTransport> http_;
    std::string instanceId_;
    bool connected_ = false;
    std::string endpoint_;
    std::string wanted_;  // SetEmulator
    bool attachedOnce_ = false;  // HasState
    // The instance went away (its emulator closed, the server stopped): wait for a suitable one again
    void Lost();
    // The instance is still listed by the server (asked about once a second while attached)
    bool InstanceListed();
    std::chrono::steady_clock::time_point nextAttach_{};  // the next look for an instance while offline
    std::chrono::steady_clock::time_point nextAlive_{};   // the next liveness check while attached
    std::string model_;           // server-reported short name ("128k" ...)
    std::string lastError_;

    std::vector<uint8_t> mem_;    // 64K cache of the live Z80 map
    bool memValid_ = false;
    Z80Regs regs_;
    bool regsValid_ = false;
    Z80Regs prevRegs_;            // §9.5 change highlighting across stops
    uint64_t markT_ = 0;          // SetDebugMark
    uint64_t absT_ = 0;           // frame x frame length + T in the frame, at the last register read
    uint32_t frameT_ = 0;         // the frame's length in T-states (GET /video/beam); 0 before the first read

    std::array<uint8_t, 0x10000> bpBits_{};  // bit0 X, 1 R, 2 W (local truth)
    std::map<std::pair<uint16_t, int>, int> serverBpIds_;  // (addr, bit) -> id

    std::array<PageInfo, 4> pages_{};  // FetchPaging
    uint64_t snapshots_ = 0;      // Snapshots
    bool pushed_ = false;         // the transport is pushed the topics (WebSocket); polled otherwise
    bool running_ = false;        // the instance runs (run.state; Continue; RunTo)
    bool runToArmed_ = false;     // RunTo's breakpoint is set
    std::chrono::steady_clock::time_point nextPoll_{};  // polling without topics
    PortsState ports_;            // FetchPorts, FetchPaging (the DOS ROM)
    AyState ay_;
    uint64_t ayAt_ = UINT64_MAX;  // the snapshot ay_ was read at (GetAy reads once per snapshot)
    bool ayChosen_ = false;       // the panel's chip was chosen (SelectAyChip): the machine's selection no longer shows
    Beta128State beta128_;
    uint64_t beta128At_ = UINT64_MAX;
    std::vector<std::pair<uint16_t, std::string>> labels_;
    std::vector<std::string> conditions_;  // the conditions' texts, as the emulator lists them
    std::vector<int> conditionIds_;        // their breakpoints' ids
    bool ripperArmed_ = false;    // the memory profiler counts for the ripper
    bool ripTraceReads_ = false;
    bool ripTraceWrites_ = false;
};

}  // namespace dbg
