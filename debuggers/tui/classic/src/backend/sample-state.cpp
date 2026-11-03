// sample-state.cpp - the golden reference scenario, a faithful transcription
// of sample_state() in docs/inprogress/2026-09-24-tui-debugger/unreal_dbg_render.py.
#include "backend/sample-state.h"

namespace dbg {

TestSetState GoldenSampleState::Build() {
    TestSetState st;
    st.memory.assign(0x10000, 0);

    // Program at 0x8000 (bytes match the canned disasm table of the oracle).
    struct ProgEntry {
        uint16_t at;
        std::vector<uint8_t> bytes;
    };
    const ProgEntry prog[] = {
        {0x8000, {0xF3}},                     // di
        {0x8001, {0x31, 0x00, 0xC0}},         // ld sp,C000
        {0x8004, {0x21, 0x00, 0x40}},         // ld hl,4000
        {0x8007, {0x11, 0x01, 0x40}},         // ld de,4001
        {0x800A, {0x01, 0xFF, 0x17}},         // ld bc,17FF
        {0x800D, {0x36, 0x00}},               // ld (hl),00
        {0x800F, {0xED, 0xB0}},               // ldir
        {0x8011, {0xCD, 0x20, 0x80}},         // call 8020
        {0x8014, {0x18, 0xFE}},               // jr 8014
        {0x8020, {0x3E, 0x07}},               // ld a,07
        {0x8022, {0xD3, 0xFE}},               // out (FE),a
        {0x8024, {0xDD, 0x36, 0x05, 0x10}},   // ld (ix+05),10
        {0x8028, {0xFD, 0xCB, 0x01, 0xC6}},   // set 0,(iy+01)
        {0x802C, {0xC9}},                     // ret
    };
    for (const ProgEntry& e : prog) {
        for (size_t i = 0; i < e.bytes.size(); ++i) {
            st.memory[e.at + i] = e.bytes[i];
        }
    }

    const char* msg = "UNREAL SPECCY DEBUGGER";
    for (int i = 0; msg[i] != '\0'; ++i) {
        st.memory[0xC000 + static_cast<uint16_t>(i)] = static_cast<uint8_t>(msg[i]);
    }
    st.memory[0xBFFE] = 0x14;  // return address on stack (little endian 8014)
    st.memory[0xBFFF] = 0x80;
    for (int i = 0; i < 0x10; ++i) {
        st.memory[0x4000 + static_cast<uint16_t>(i)] = static_cast<uint8_t>(i);
    }

    st.regs.a = 0x07;
    st.regs.f = 0x44;  // Z and P set
    st.regs.bc = 0x17FF;
    st.regs.de = 0x4001;
    st.regs.hl = 0x4000;
    st.regs.af2 = 0x0000;
    st.regs.bc2 = 0x0000;
    st.regs.de2 = 0x0000;
    st.regs.hl2 = 0x0000;
    st.regs.sp = 0xBFFE;
    st.regs.pc = 0x8011;
    st.regs.ix = 0x5C3A;
    st.regs.iy = 0x5C3A;
    st.regs.i = 0x3F;
    st.regs.r = 0x12;
    st.regs.im = 1;
    st.regs.iff1 = 0;
    st.regs.iff2 = 0;
    st.regs.t = 17023;
    st.regs.lastBranch = 0x800F;
    st.regs.halted = false;

    st.prevRegs = st.regs;
    st.prevRegs.bc = 0x0000;
    st.prevRegs.pc = 0x800F;
    st.prevRegs.f = 0x40;
    st.prevRegs.r = 0x10;

    st.timeDelta = 12;

    st.ports.fe = 0x07;
    st.ports.p7ffd = 0x10;
    st.ports.lock48 = false;
    st.ports.hasExtPort = false;  // Pentagon: no ext port -> cmos: line
    st.ports.cmosAddr = 0x00;
    st.ports.eff7 = 0x00;
    st.ports.dosPortsActive = false;

    st.beta128.present = true;
    st.beta128.cmd = 0x80;
    st.beta128.data = 0x00;
    st.beta128.statusRead = 0x20;
    st.beta128.statusRaw = 0x20;
    st.beta128.sector = 0x01;
    st.beta128.headTrack = 0x00;
    st.beta128.track = 0x00;
    st.beta128.system = 0x3C;
    st.beta128.rqs = 0x80;

    st.ay.scheme = AyScheme::Single;
    st.ay.activeChip = 0;
    const uint8_t ayRegs[16] = {0x1C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38,
                                0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xBF};
    for (int i = 0; i < 16; ++i) {
        st.ay.regs[0][static_cast<size_t>(i)] = ayRegs[i];
        st.ay.regs[1][static_cast<size_t>(i)] = ayRegs[i];
    }
    st.ay.latchedReg[0] = 7;
    st.ay.latchedReg[1] = 7;

    st.machine.cpuCount = 1;
    st.machine.cpuIndex = 0;
    st.machine.model = MemoryModelHint::Pentagon;
    st.machine.ayScheme = AyScheme::Single;
    st.machine.trdosPresent = true;
    st.machine.gsPresent = false;
    st.machine.gsDmaAddr = 0;
    st.machine.pages = {{
        PageInfo{"BASIC", true},
        PageInfo{"RAM 5", false},
        PageInfo{"RAM 2", false},
        PageInfo{"RAM 0", false},
    }};
    st.machine.frameLength = 69888;

    st.execBreakpoints = {0x8022};

    st.ui.activeWindow = 1;  // TRACE focused (0=REGS 1=TRACE 2=MEM)
    st.ui.regsCurs = 0;
    st.ui.traceTop = 0x8000;
    st.ui.traceCurs = 0x8011;
    st.ui.traceMode = 2;
    st.ui.memTop = 0xC000;
    st.ui.memCurs = 0xC003;
    st.ui.memAscii = 0;
    st.ui.memSecond = 0;
    st.ui.editorSpace = 0;
    st.ui.showScrshot = 0;
    st.ui.userWatches = {0x4000, 0x8000, 0xC000};

    return st;
}

std::unique_ptr<ITestSet> CreateTestSetByName(const std::string& name) {
    if (name.empty() || name == "golden-sample") {
        return std::make_unique<GoldenSampleState>();
    }
    return nullptr;
}

}  // namespace dbg
