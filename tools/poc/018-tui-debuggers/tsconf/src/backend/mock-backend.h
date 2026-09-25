// mock-backend.h - in-process z80ex machine behind the IDebuggerBackend seam.
//
// Executes the vendored z80ex core over a flat 64K RAM image seeded by an
// ITestSet (default: the golden sample, the executable oracle for the TDD
// Appendix A dumps). Peripheral writes (#FE/#7FFD/AY/#EFF7/beta128 #FF/#1F/
// #3F/#5F/#7F) are latched for the panels. The memory/port callbacks
// implement BPR/BPW breakpoints, the ripper trace bits and the §10.2
// OUT/IN/VAL port-access capture. INT delivery follows a Pentagon-style
// window at the frame start; the frame wraps at machine.frameLength T.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "backend/debugger-backend.h"
#include "backend/expr-lang.h"
#include "backend/sample-state.h"
#include "z80ex.h"

namespace dbg {

class MockBackend : public IDebuggerBackend {
public:
    // Seeds the machine from "testSet"; a null pointer falls back to the
    // golden sample test set.
    explicit MockBackend(std::unique_ptr<ITestSet> testSet);
    ~MockBackend() override;

    std::string Name() const override { return "mock"; }

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
    void LoadUserLabels(const std::string& path, std::string* error, int* loadedCount) override;
    std::vector<ImportItem> ScanLabelImports() override;
    void ImportLabels(int itemIndex) override;

    void ArmRipper(bool traceReads, bool traceWrites) override;
    bool RipperArmed() override;
    std::vector<uint8_t> CollectRipper(uint8_t unrefByte) override;

    // TSConf fork (TDD-DBG-02 §5): M1 fetch ring + register block. The ring
    // records every opcode-fetch address z80ex reports (prefix DD/FD/ED/CB
    // fetches are separate M1 cycles, matching the original's history).
    std::vector<PcHistEntry> GetPcHistory(int cpu) override;
    bool GetTsConf(TsConfState& out) override;

    // UI seed from the test set (focus/cursors the golden dumps assume).
    const UiStateSeed& UiSeed() const { return seed_.ui; }

private:
    static Z80EX_BYTE MemReadCb(Z80EX_CONTEXT* cpu, Z80EX_WORD addr, int m1State, void* user);
    static void MemWriteCb(Z80EX_CONTEXT* cpu, Z80EX_WORD addr, Z80EX_BYTE value, void* user);
    static Z80EX_BYTE PortReadCb(Z80EX_CONTEXT* cpu, Z80EX_WORD port, void* user);
    static void PortWriteCb(Z80EX_CONTEXT* cpu, Z80EX_WORD port, Z80EX_BYTE value, void* user);
    static Z80EX_BYTE IntReadCb(Z80EX_CONTEXT* cpu, void* user);

    void SeedRegs(const Z80Regs& r);
    uint32_t TInFrame() const;
    void MaybeDeliverInt();
    int ExecuteOne();                          // one instruction / halt tick
    bool EvalConditions();                     // §10.2 before every instruction
    std::optional<StopReason> StopAtBoundary();
    void ApplyPortWrite(uint16_t port, uint8_t value);
    uint8_t ApplyPortRead(uint16_t port);
    const std::vector<uint8_t>* SpaceData(EditorSpace space);

    void RecordPcFetch(uint16_t addr);               // fork: M1 -> ring

    Z80EX_CONTEXT* cpu_ = nullptr;
    TestSetState seed_;
    std::vector<uint8_t> mem_;                     // 64K live map
    std::array<uint8_t, 0x10000> bpBits_{};        // bit0 X, 1 R, 2 W, 3 traceR, 4 traceW
    MachineInfo machine_;
    PortsState ports_;
    Beta128State beta_;
    AyState ay_;
    Z80Regs prevRegs_;                             // §9.5 change-highlight snapshot
    uint64_t absT_ = 0;
    uint64_t markAbsT_ = 0;
    uint16_t lastBranch_ = 0;
    bool tsConf_ = false;                          // fork variant active
    TsConfState ts_;                               // seeded register block
    std::array<PcHistEntry, 32> pcRing_{};         // newest first, ring cap 32
    int pcRingCount_ = 0;
    bool memBpFired_ = false;
    bool hadPortAccess_ = false;
    bool portWasOut_ = false;
    uint16_t portAddr_ = 0;
    uint8_t portVal_ = 0;
    bool ripperArmed_ = false;
    bool ripTraceReads_ = false;
    bool ripTraceWrites_ = false;
    struct Cond {
        std::string source;
        CompiledExpr expr;
    };
    std::vector<Cond> conds_;
    // mock simplification: physical == flat 64K (see LoadUserLabels)
    std::vector<std::pair<uint16_t, std::string>> labels_;
};

}  // namespace dbg
