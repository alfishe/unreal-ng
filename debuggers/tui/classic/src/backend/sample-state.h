// sample-state.h - the DEFAULT TEST SET (pluggable, separate class).
//
// ITestSet seeds a fresh machine with a reproducible scenario: memory image,
// registers, previous-register snapshot, peripheral latches, breakpoints and
// a recommended initial UI state. The bundled GoldenSampleState reproduces
// the reference state of TDD-DBG-01 Appendix A / unreal_dbg_render.py
// sample_state() so the rendered screen can be diffed cell-for-cell against
// the golden dumps. Additional scenarios can be plugged by implementing
// ITestSet and passing them to the backend factory.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "model/model.h"

namespace dbg {

// UI-only state the golden dumps assume (§13 keeps this client-side; the
// test set ships a recommended seed so tests reproduce the reference screen).
struct UiStateSeed {
    int activeWindow = 1;             // 0=REGS 1=TRACE 2=MEM (golden starts on TRACE, §3)
    int regsCurs = 0;
    uint16_t traceTop = 0x8000;
    uint16_t traceCurs = 0x8011;
    int traceMode = 2;                // 0 addr, 1 bytes, 2 mnemonic
    uint16_t memTop = 0xC000;
    uint16_t memCurs = 0xC003;
    int memAscii = 0;
    int memSecond = 0;
    int editorSpace = 0;              // EditorSpace as int
    int showScrshot = 0;              // 0 watches, 1 screen, 2 ray
    std::array<uint16_t, 3> userWatches{0x4000, 0x8000, 0xC000};
};

struct TestSetState {
    std::vector<uint8_t> memory;      // 64K
    Z80Regs regs;
    Z80Regs prevRegs;
    int64_t timeDelta = 0;            // displayed value
    uint16_t debugLastT = 0;          // internal mark (t at mark)
    PortsState ports;
    Beta128State beta128;
    AyState ay;
    MachineInfo machine;
    std::vector<uint16_t> execBreakpoints;  // addresses with BPX pre-set
    UiStateSeed ui;
};

class ITestSet {
public:
    virtual ~ITestSet() = default;
    virtual std::string Name() const = 0;
    virtual TestSetState Build() = 0;
};

// The golden reference scenario (Appendix A state).
class GoldenSampleState : public ITestSet {
public:
    std::string Name() const override { return "golden-sample"; }
    TestSetState Build() override;
};

// Registry for pluggable test sets; "golden-sample" is always available.
std::unique_ptr<ITestSet> CreateTestSetByName(const std::string& name);

}  // namespace dbg
