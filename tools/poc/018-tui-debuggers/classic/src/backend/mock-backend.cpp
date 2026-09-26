// mock-backend.cpp - see mock-backend.h.
#include "backend/mock-backend.h"

#include <cstdio>
#include <fstream>

#include "backend/asm-subset.h"
#include "backend/disasm-unreal.h"
#include "backend/rest-backend.h"
#include "z80ex.h"

namespace dbg {
namespace {

// Fast opcode classifier for last_branch tracking (no disassembly per
// executed instruction): djnz/jr/halt/jp/call/ret/rst families, jp (ix)/(iy)
// and the ED block / RETI / RETN group.
bool IsControlByte(const uint8_t* mem, uint16_t pc) {
    const uint8_t op = mem[pc];
    switch (op) {
    case 0x10: case 0x18: case 0x76:  // djnz, jr, halt
    case 0xC3: case 0xC9: case 0xCD:  // jp nn, ret, call nn
        return true;
    case 0xDD:
    case 0xFD:
        return mem[static_cast<uint16_t>(pc + 1)] == 0xE9;  // jp (ix)/(iy)
    case 0xED: {
        const uint8_t op2 = mem[static_cast<uint16_t>(pc + 1)];
        if (op2 >= 0xA0 && op2 <= 0xBF) return true;  // LDIR..OTDR blocks
        return (op2 & 0xF7) == 0x45;  // retn (0x45) / reti (0x4D)
    }
    default:
        if (op >= 0xC0 && op <= 0xFF) {  // ret cc / jp cc / call cc / rst
            const uint8_t low = static_cast<uint8_t>(op & 0x07);
            return low == 0x00 || low == 0x02 || low == 0x04 || low == 0x07;
        }
        return false;
    }
}

}  // namespace

MockBackend::MockBackend(std::unique_ptr<ITestSet> testSet) {
    if (testSet != nullptr) {
        seed_ = testSet->Build();
    } else {
        GoldenSampleState golden;
        seed_ = golden.Build();
    }
    mem_ = seed_.memory;
    machine_ = seed_.machine;
    ports_ = seed_.ports;
    beta_ = seed_.beta128;
    ay_ = seed_.ay;
    lastBranch_ = seed_.regs.lastBranch;
    for (const uint16_t a : seed_.execBreakpoints) bpBits_[a] |= 0x01;
    if (machine_.intLength == 0) {
        // mock choice: Pentagon-style INT window at the frame start
        machine_.intStart = 0;
        machine_.intLength = 32;
    }
    cpu_ = z80ex_create(MemReadCb, this, MemWriteCb, this, PortReadCb, this,
                        PortWriteCb, this, IntReadCb, this);
    SeedRegs(seed_.regs);
    prevRegs_ = seed_.prevRegs;
    absT_ = seed_.regs.t;
    const uint64_t delta = static_cast<uint64_t>(seed_.timeDelta >= 0 ? seed_.timeDelta : 0);
    markAbsT_ = (absT_ > delta) ? absT_ - delta : 0;
}

MockBackend::~MockBackend() { z80ex_destroy(cpu_); }

void MockBackend::SeedRegs(const Z80Regs& r) {
    z80ex_set_reg(cpu_, regAF, static_cast<Z80EX_WORD>((r.a << 8) | r.f));
    z80ex_set_reg(cpu_, regBC, r.bc);
    z80ex_set_reg(cpu_, regDE, r.de);
    z80ex_set_reg(cpu_, regHL, r.hl);
    z80ex_set_reg(cpu_, regAF_, r.af2);
    z80ex_set_reg(cpu_, regBC_, r.bc2);
    z80ex_set_reg(cpu_, regDE_, r.de2);
    z80ex_set_reg(cpu_, regHL_, r.hl2);
    z80ex_set_reg(cpu_, regIX, r.ix);
    z80ex_set_reg(cpu_, regIY, r.iy);
    z80ex_set_reg(cpu_, regSP, r.sp);
    z80ex_set_reg(cpu_, regPC, r.pc);
    z80ex_set_reg(cpu_, regI, r.i);
    z80ex_set_reg(cpu_, regR, r.r);
    z80ex_set_reg(cpu_, regR7, r.r);
    z80ex_set_reg(cpu_, regIM, r.im);
    z80ex_set_reg(cpu_, regIFF1, r.iff1);
    z80ex_set_reg(cpu_, regIFF2, r.iff2);
}

// -- z80ex callbacks ---------------------------------------------------------

Z80EX_BYTE MockBackend::MemReadCb(Z80EX_CONTEXT*, Z80EX_WORD addr, int, void* user) {
    MockBackend* self = static_cast<MockBackend*>(user);
    const uint8_t bits = self->bpBits_[addr];
    if (bits & 0x02) self->memBpFired_ = true;  // BPR
    if (self->ripperArmed_ && self->ripTraceReads_) self->bpBits_[addr] |= 0x08;
    return self->mem_[addr];
}

void MockBackend::MemWriteCb(Z80EX_CONTEXT*, Z80EX_WORD addr, Z80EX_BYTE value, void* user) {
    MockBackend* self = static_cast<MockBackend*>(user);
    const uint8_t bits = self->bpBits_[addr];
    if (bits & 0x04) self->memBpFired_ = true;  // BPW
    if (self->ripperArmed_ && self->ripTraceWrites_) self->bpBits_[addr] |= 0x10;
    self->mem_[addr] = value;
}

Z80EX_BYTE MockBackend::PortReadCb(Z80EX_CONTEXT*, Z80EX_WORD port, void* user) {
    MockBackend* self = static_cast<MockBackend*>(user);
    const uint8_t value = self->ApplyPortRead(port);
    self->hadPortAccess_ = true;
    self->portWasOut_ = false;
    self->portAddr_ = port;
    self->portVal_ = value;
    return value;
}

void MockBackend::PortWriteCb(Z80EX_CONTEXT*, Z80EX_WORD port, Z80EX_BYTE value, void* user) {
    MockBackend* self = static_cast<MockBackend*>(user);
    self->ApplyPortWrite(port, value);
    self->hadPortAccess_ = true;
    self->portWasOut_ = true;
    self->portAddr_ = port;
    self->portVal_ = value;
}

Z80EX_BYTE MockBackend::IntReadCb(Z80EX_CONTEXT*, void* user) {
    MockBackend* self = static_cast<MockBackend*>(user);
    return static_cast<Z80EX_BYTE>(self->seed_.regs.intVector & 0xFF);
}

// -- time / interrupt ----------------------------------------------------------

uint32_t MockBackend::TInFrame() const {
    return static_cast<uint32_t>(absT_ % machine_.frameLength);
}

void MockBackend::MaybeDeliverInt() {
    const uint32_t t = TInFrame();
    if (t >= machine_.intStart && t < machine_.intStart + machine_.intLength) {
        if (z80ex_int_possible(cpu_)) z80ex_int(cpu_);
    }
}

int MockBackend::ExecuteOne() {
    const uint16_t pc0 = static_cast<uint16_t>(z80ex_get_reg(cpu_, regPC));
    hadPortAccess_ = false;
    portAddr_ = 0;
    portVal_ = 0;
    memBpFired_ = false;
    int t = 0;
    if (z80ex_doing_halt(cpu_)) {
        t = 4;  // halted: burn T-states until the INT window opens
    } else {
        t = z80ex_step(cpu_);
    }
    absT_ += static_cast<uint64_t>(t);
    if (IsControlByte(mem_.data(), pc0)) lastBranch_ = pc0;
    return t;
}

bool MockBackend::EvalConditions() {
    if (conds_.empty()) return false;
    Z80Regs regs = GetRegs(0);
    EvalContext ctx;
    ctx.regs = &regs;
    ctx.mem = &mem_;
    ctx.fd = ports_.p7ffd;
    ctx.dos = ports_.dosPortsActive;
    if (hadPortAccess_) {
        ctx.outPort = portWasOut_ ? portAddr_ : 0xFFFFFFFFu;
        ctx.inPort = portWasOut_ ? 0xFFFFFFFFu : portAddr_;
        ctx.val = portVal_;
    }
    for (const Cond& c : conds_) {
        if (c.expr.Eval(ctx) != 0) return true;
    }
    return false;
}

std::optional<IDebuggerBackend::StopReason> MockBackend::StopAtBoundary() {
    const uint16_t pc = static_cast<uint16_t>(z80ex_get_reg(cpu_, regPC));
    if (bpBits_[pc] & 0x01) return StopReason::Breakpoint;
    if (!conds_.empty() && EvalConditions()) return StopReason::Breakpoint;
    return std::nullopt;
}

// -- machine / snapshot ---------------------------------------------------------

MachineInfo MockBackend::GetMachineInfo() { return machine_; }

Z80Regs MockBackend::GetRegs(int) {
    Z80Regs r;
    const uint16_t af = z80ex_get_reg(cpu_, regAF);
    r.a = static_cast<uint8_t>(af >> 8);
    r.f = static_cast<uint8_t>(af & 0xFF);
    r.bc = z80ex_get_reg(cpu_, regBC);
    r.de = z80ex_get_reg(cpu_, regDE);
    r.hl = z80ex_get_reg(cpu_, regHL);
    r.af2 = z80ex_get_reg(cpu_, regAF_);
    r.bc2 = z80ex_get_reg(cpu_, regBC_);
    r.de2 = z80ex_get_reg(cpu_, regDE_);
    r.hl2 = z80ex_get_reg(cpu_, regHL_);
    r.ix = z80ex_get_reg(cpu_, regIX);
    r.iy = z80ex_get_reg(cpu_, regIY);
    r.sp = z80ex_get_reg(cpu_, regSP);
    r.pc = z80ex_get_reg(cpu_, regPC);
    r.i = static_cast<uint8_t>(z80ex_get_reg(cpu_, regI));
    r.r = static_cast<uint8_t>((z80ex_get_reg(cpu_, regR) & 0x7F) |
                               (z80ex_get_reg(cpu_, regR7) & 0x80));
    r.im = static_cast<uint8_t>(z80ex_get_reg(cpu_, regIM));
    r.iff1 = static_cast<uint8_t>(z80ex_get_reg(cpu_, regIFF1));
    r.iff2 = static_cast<uint8_t>(z80ex_get_reg(cpu_, regIFF2));
    r.halted = z80ex_doing_halt(cpu_) != 0;
    r.t = TInFrame();
    r.lastBranch = lastBranch_;
    r.intVector = seed_.regs.intVector;
    return r;
}

BranchInfo MockBackend::GetBranchAtPc(int) {
    // shared §4.2.4 analysis (backend/disasm-unreal) over the live 64K map
    return UnrealBranchInfo(mem_.data(), static_cast<uint16_t>(z80ex_get_reg(cpu_, regPC)),
                            GetRegs(0));
}

int64_t MockBackend::GetTimeDelta(int) {
    return static_cast<int64_t>(absT_) - static_cast<int64_t>(markAbsT_);
}

// -- memory / disassembly --------------------------------------------------------

std::vector<uint8_t> MockBackend::ReadMemory(int, uint16_t addr, uint16_t len) {
    std::vector<uint8_t> out;
    out.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        out.push_back(mem_[static_cast<uint16_t>(addr + i)]);
    }
    return out;
}

void MockBackend::WriteMemory(int, uint16_t addr, const std::vector<uint8_t>& bytes) {
    for (size_t i = 0; i < bytes.size(); ++i) {
        mem_[static_cast<uint16_t>(addr + i)] = bytes[i];
    }
}

std::vector<DisasmLine> MockBackend::Disassemble(int, uint16_t addr, int nLines, bool withLabels) {
    std::vector<DisasmLine> lines;
    lines.reserve(nLines);
    uint16_t pc = addr;
    for (int i = 0; i < nLines; ++i) {
        const UnrealInstr ins = UnrealDisasm(mem_.data(), pc);
        DisasmLine d;
        d.addr = pc;
        d.len = ins.len;
        d.bytes.assign(mem_.begin() + pc, mem_.begin() + static_cast<uint16_t>(pc + ins.len));
        d.mnemonic = ins.text;
        if (withLabels) d.label = LabelFor(pc);
        lines.push_back(std::move(d));
        pc = static_cast<uint16_t>(pc + ins.len);
    }
    return lines;
}

uint16_t MockBackend::PrevInstruction(int, uint16_t addr) {
    return UnrealPrevInstruction(mem_.data(), addr);
}

// -- peripherals --------------------------------------------------------------------

PortsState MockBackend::GetPorts() { return ports_; }
Beta128State MockBackend::GetBeta128() { return beta_; }
AyState MockBackend::GetAy() { return ay_; }

void MockBackend::SelectAyChip(int chip) {
    if (ay_.scheme == AyScheme::TurboSound) ay_.activeChip = chip & 1;
}

std::vector<uint8_t> MockBackend::ReadCmos() { return std::vector<uint8_t>(256, 0); }
std::vector<uint8_t> MockBackend::ReadNvram() { return std::vector<uint8_t>(2048, 0); }
std::vector<uint8_t> MockBackend::ReadCompPal() { return std::vector<uint8_t>(64, 0); }

DiskTrackData MockBackend::ReadDiskTrack(int, int) {
    DiskTrackData d;  // no disk in the mock: "track not found"
    d.present = false;
    return d;
}

void MockBackend::WriteDiskByte(int, int, bool, int, uint8_t) {
    // no disk image in the mock: writes are accepted and dropped
}

ScreenPixels MockBackend::GetScreenPreview(int mode) {
    // 320x224 frame (256x192 screen at (32,16), border elsewhere), centre
    // crop 296x208 (§4.3.2). Index = ZX palette index (0..15).
    const int fh = 224, sx = 32, sy = 16;
    const uint8_t border = static_cast<uint8_t>(ports_.fe & 0x07);
    const uint32_t t = TInFrame();
    const int beamLine = (mode == 2)
                             ? static_cast<int>(static_cast<uint64_t>(t) * fh / machine_.frameLength)
                             : fh;
    const uint64_t frame = absT_ / machine_.frameLength;
    const bool flashPhase = (frame & 1) != 0;
    ScreenPixels px;
    px.width = 296;
    px.height = 208;
    px.index.assign(static_cast<size_t>(px.width) * px.height, border);
    for (int y = sy; y < sy + 192 && y < beamLine; ++y) {
        const int ly = y - sy;
        for (int x = sx; x < sx + 256; ++x) {
            const int lx = x - sx;
            const uint16_t addr = static_cast<uint16_t>(
                0x4000 | ((ly & 0xC0) << 5) | ((ly & 0x07) << 8) | ((ly & 0x38) << 2) | (lx >> 3));
            const uint8_t attr = mem_[static_cast<uint16_t>(0x5800 + ((ly >> 3) << 5) + (lx >> 3))];
            const uint8_t bright = (attr & 0x40) ? 8 : 0;
            const bool on = (mem_[addr] >> (7 - (lx & 7))) & 1;
            const bool swap = (attr & 0x80) && flashPhase;
            const bool inkPix = on ^ swap;
            const uint8_t idx = inkPix ? static_cast<uint8_t>(bright | (attr & 0x07))
                                       : static_cast<uint8_t>(bright | ((attr >> 3) & 0x07));
            px.index[static_cast<size_t>(y - 8) * px.width + (x - 12)] = idx;
        }
    }
    return px;
}

// -- execution control -------------------------------------------------------------

std::optional<IDebuggerBackend::StopReason> MockBackend::Step(int) {
    prevRegs_ = GetRegs(0);  // §9.5: snapshot + time mark at step start
    markAbsT_ = absT_;
    ExecuteOne();
    MaybeDeliverInt();
    return StopAtBoundary();
}

IDebuggerBackend::StopReason MockBackend::RunUntilBreak(int, const StopRequest& request) {
    prevRegs_ = GetRegs(0);  // §9.5 (b): snapshot at leave
    markAbsT_ = absT_;
    uint64_t count = 0;
    for (;;) {
        const uint16_t pc = static_cast<uint16_t>(z80ex_get_reg(cpu_, regPC));
        const uint16_t sp = z80ex_get_reg(cpu_, regSP);
        if (bpBits_[pc] & 0x01) return StopReason::Breakpoint;
        if (!conds_.empty() && EvalConditions()) return StopReason::Breakpoint;
        if (request.hasStopHere && pc == request.stopHere) return StopReason::StopHere;
        if (request.hasStopSp && sp == request.stopSp &&
            !(pc >= request.loopR1 && pc <= request.loopR2)) {
            return StopReason::StopSp;
        }
        ExecuteOne();
        MaybeDeliverInt();
        if (memBpFired_) return StopReason::Breakpoint;
        if (++count >= request.maxInstructions) return StopReason::MaxInstructions;
    }
}

uint64_t MockBackend::GetAbsoluteT(int) { return absT_; }

void MockBackend::SetDebugMark(int, uint64_t absT) { markAbsT_ = absT; }

// -- register / port writes ----------------------------------------------------------

void MockBackend::WriteReg(int, RegField field, unsigned v) {
    const uint16_t v16 = static_cast<uint16_t>(v & 0xFFFF);
    switch (field) {
    case RegField::A: {
        const uint16_t af = z80ex_get_reg(cpu_, regAF);
        z80ex_set_reg(cpu_, regAF, static_cast<Z80EX_WORD>((af & 0x00FF) | (v16 << 8)));
        break;
    }
    case RegField::F: {
        const uint16_t af = z80ex_get_reg(cpu_, regAF);
        z80ex_set_reg(cpu_, regAF, static_cast<Z80EX_WORD>((af & 0xFF00) | (v16 & 0xFF)));
        break;
    }
    case RegField::Bc: z80ex_set_reg(cpu_, regBC, v16); break;
    case RegField::De: z80ex_set_reg(cpu_, regDE, v16); break;
    case RegField::Hl: z80ex_set_reg(cpu_, regHL, v16); break;
    case RegField::Af2: z80ex_set_reg(cpu_, regAF_, v16); break;
    case RegField::Bc2: z80ex_set_reg(cpu_, regBC_, v16); break;
    case RegField::De2: z80ex_set_reg(cpu_, regDE_, v16); break;
    case RegField::Hl2: z80ex_set_reg(cpu_, regHL_, v16); break;
    case RegField::Sp: z80ex_set_reg(cpu_, regSP, v16); break;
    case RegField::Pc: z80ex_set_reg(cpu_, regPC, v16); break;
    case RegField::Ix: z80ex_set_reg(cpu_, regIX, v16); break;
    case RegField::Iy: z80ex_set_reg(cpu_, regIY, v16); break;
    case RegField::I: z80ex_set_reg(cpu_, regI, v16); break;
    case RegField::R:
        z80ex_set_reg(cpu_, regR, v16);
        z80ex_set_reg(cpu_, regR7, v16);
        break;
    case RegField::Im: z80ex_set_reg(cpu_, regIM, v16 & 3); break;
    case RegField::Iff1: z80ex_set_reg(cpu_, regIFF1, v16 & 1); break;
    case RegField::Iff2: z80ex_set_reg(cpu_, regIFF2, v16 & 1); break;
    }
}

void MockBackend::Out(uint16_t port, uint8_t value) { ApplyPortWrite(port, value); }

void MockBackend::ApplyPortWrite(uint16_t port, uint8_t value) {
    switch (port) {
    case 0xFE: ports_.fe = value; break;  // border / speaker bits
    case 0x7FFD:
        ports_.p7ffd = value;
        ports_.lock48 = (value & 0x20) != 0;
        break;  // paging: the mock runs a flat 64K RAM map
    case 0xFFFD:
        ay_.latchedReg[static_cast<size_t>(ay_.activeChip) & 1u] = value & 0x0F;
        break;
    case 0xBFFD:
        ay_.regs[static_cast<size_t>(ay_.activeChip) & 1u]
              [ay_.latchedReg[static_cast<size_t>(ay_.activeChip) & 1u]] = value;
        break;
    case 0xEFF7: ports_.eff7 = value; break;
    case 0xFF:
        if (ports_.dosPortsActive) beta_.system = value;
        break;
    case 0x1F:
        if (ports_.dosPortsActive) beta_.cmd = value;
        break;
    case 0x3F:
        if (ports_.dosPortsActive) beta_.track = value;
        break;
    case 0x5F:
        if (ports_.dosPortsActive) beta_.sector = value;
        break;
    case 0x7F:
        if (ports_.dosPortsActive) beta_.data = value;
        break;
    default: break;  // unmodelled ports are latched nowhere
    }
}

uint8_t MockBackend::ApplyPortRead(uint16_t port) {
    switch (port) {
    case 0x1F:
        return ports_.dosPortsActive ? beta_.statusRead : 0xFF;
    case 0x3F:
        return ports_.dosPortsActive ? beta_.track : 0xFF;
    case 0x5F:
        return ports_.dosPortsActive ? beta_.sector : 0xFF;
    case 0x7F:
        return ports_.dosPortsActive ? beta_.data : 0xFF;
    case 0xFF:
        return ports_.dosPortsActive ? beta_.rqs : 0xFF;
    default:
        return 0xFF;  // floating bus not modelled
    }
}

void MockBackend::Set7ffd(uint8_t value) { ApplyPortWrite(0x7FFD, value); }

void MockBackend::SetExtPort(uint8_t) {
    // the golden machine (Pentagon) has no extended port
}

// -- breakpoints / conditions ----------------------------------------------------------

uint8_t MockBackend::BpBitsAt(uint16_t addr) { return bpBits_[addr]; }

void MockBackend::SetBpBits(uint16_t addr, uint8_t bits) { bpBits_[addr] = bits; }

bool MockBackend::HasAnyBreakpoints() {
    for (const uint8_t bits : bpBits_) {
        if (bits & 0x07) return true;
    }
    return !conds_.empty();
}

ExprCompileResult MockBackend::AddCondition(int, const std::string& text) {
    ExprCompileResult res;
    CompiledExpr expr;
    std::string error;
    if (!CompiledExpr::Compile(text, &expr, &error)) {
        res.error = error;
        return res;
    }
    Cond c;
    c.source = text;
    c.expr = std::move(expr);
    conds_.push_back(std::move(c));
    res.ok = true;
    return res;
}

void MockBackend::DeleteCondition(int, int index) {
    if (index >= 0 && static_cast<size_t>(index) < conds_.size()) {
        conds_.erase(conds_.begin() + index);
    }
}

std::vector<std::string> MockBackend::GetConditions(int) {
    std::vector<std::string> out;
    out.reserve(conds_.size());
    for (const Cond& c : conds_) out.push_back(c.expr.Decompile());
    return out;
}

int MockBackend::ConditionCount(int) { return static_cast<int>(conds_.size()); }

// -- assemble / find ---------------------------------------------------------------------

AssembleResult MockBackend::Assemble(uint16_t addr, const std::string& text) {
    return AssembleSubset(addr, text);
}

const std::vector<uint8_t>* MockBackend::SpaceData(EditorSpace space) {
    static const std::vector<uint8_t> kCmos(256, 0);
    static const std::vector<uint8_t> kNvram(2048, 0);
    static const std::vector<uint8_t> kCompPal(64, 0);
    switch (space) {
    case EditorSpace::Mem: return &mem_;
    case EditorSpace::Cmos: return &kCmos;
    case EditorSpace::Nvram: return &kNvram;
    case EditorSpace::CompPal: return &kCompPal;
    case EditorSpace::DiskPhys:
    case EditorSpace::DiskLog: return nullptr;  // no disk in the mock
    }
    return nullptr;
}

FindResult MockBackend::FindBytes(EditorSpace space, uint16_t start, const BytePattern& pattern,
                                  const BytePattern& mask) {
    FindResult res;
    const std::vector<uint8_t>* src = SpaceData(space);
    if (src == nullptr || src->empty() || pattern.empty() || pattern.size() > src->size()) {
        return res;
    }
    const uint8_t* data = src->data();
    const size_t n = src->size();
    for (size_t off = 0; off < n; ++off) {
        const size_t a = (start + off) % n;
        bool match = true;
        for (size_t i = 0; i < pattern.size(); ++i) {
            const uint8_t m = (i < mask.size()) ? mask[i] : 0xFF;
            if (m == 0) continue;
            if ((data[(a + i) % n] & m) != (pattern[i] & m)) {
                match = false;
                break;
            }
        }
        if (match) {
            res.found = true;
            res.address = static_cast<uint16_t>(a);
            return res;
        }
    }
    return res;
}

// -- labels (§11) ---------------------------------------------------------------------------

std::optional<std::string> MockBackend::LabelFor(uint16_t addr) {
    for (const auto& l : labels_) {
        if (l.first == addr) return l.second;
    }
    return std::nullopt;
}

std::vector<std::pair<uint16_t, std::string>> MockBackend::LabelsInCurrentMap() { return labels_; }

void MockBackend::LoadUserLabels(const std::string& path, std::string* error, int* loadedCount) {
    if (loadedCount != nullptr) *loadedCount = 0;
    std::ifstream in(path);
    if (!in) {
        if (error != nullptr) {
            char buf[512];
            std::snprintf(buf, sizeof buf, "can't find label file %s", path.c_str());
            *error = buf;
        }
        return;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        // trim
        size_t b = line.find_first_not_of(" \t\r");
        size_t e = line.find_last_not_of(" \t\r");
        if (b == std::string::npos) continue;
        line = line.substr(b, e - b + 1);
        if (line.empty()) continue;
        uint16_t addr = 0;
        size_t nameAt = std::string::npos;
        unsigned page = 0, offset = 0;
        if (sscanf(line.c_str(), "%u:%4x", &page, &offset) == 2) {
            // PP:XXXX name — mock maps physical page PP to PP*0x4000 flat
            addr = static_cast<uint16_t>((page & 0x3F) * 0x4000u + (offset & 0x3FFFu));
            nameAt = line.find(' ');
        } else {
            unsigned a16 = 0;
            if (sscanf(line.c_str(), "%4x", &a16) == 1 && line.size() > 4 && line[4] == ' ') {
                addr = static_cast<uint16_t>(a16);
                nameAt = 4;
            }
        }
        if (nameAt == std::string::npos || nameAt + 1 >= line.size()) {
            if (error != nullptr) {
                *error = "error in " + path + ", line " + std::to_string(lineNo);
            }
            continue;
        }
        const std::string name = line.substr(nameAt + 1);
        labels_.emplace_back(addr, name);
        if (loadedCount != nullptr) ++(*loadedCount);
    }
}

std::vector<IDebuggerBackend::ImportItem> MockBackend::ScanLabelImports() {
    // the mock has no XAS/ALASM tables in its flat 64K: disabled §11 items
    std::vector<ImportItem> items;
    ImportItem xas;
    xas.text = "XAS labels not found in bank #06";
    xas.enabled = false;
    items.push_back(xas);
    ImportItem alasm;
    alasm.text = "No ALASM labels in whole NK memory";
    alasm.enabled = false;
    items.push_back(alasm);
    return items;
}

void MockBackend::ImportLabels(int) {
    // disabled items only: nothing to import
}

// -- ripper (§6.3) -----------------------------------------------------------------------------

void MockBackend::ArmRipper(bool traceReads, bool traceWrites) {
    ripperArmed_ = true;
    ripTraceReads_ = traceReads;
    ripTraceWrites_ = traceWrites;
    for (auto& bits : bpBits_) bits &= static_cast<uint8_t>(~(0x08 | 0x10));
}

bool MockBackend::RipperArmed() { return ripperArmed_; }

std::vector<uint8_t> MockBackend::CollectRipper(uint8_t unrefByte) {
    std::vector<uint8_t> out(0x10000);
    for (size_t a = 0; a < 0x10000; ++a) {
        const uint8_t bits = bpBits_[a];
        const bool touched = (ripTraceReads_ && (bits & 0x08) && ripperArmed_) ||
                             (ripTraceWrites_ && (bits & 0x10) && ripperArmed_);
        out[a] = touched ? mem_[a] : unrefByte;
    }
    ripperArmed_ = false;
    return out;
}

// -- factory -------------------------------------------------------------------------------------

std::unique_ptr<IDebuggerBackend> CreateBackend(BackendKind kind, const std::string& endpoint) {
    switch (kind) {
    case BackendKind::Mock:
        return std::make_unique<MockBackend>(CreateTestSetByName("golden-sample"));
    case BackendKind::Rest:
        return std::make_unique<RestBackend>(
            endpoint.empty() ? "http://localhost:8090" : endpoint);
    case BackendKind::WebSocket:
    case BackendKind::Ipc:
        return nullptr;  // remote transports: wired when they land
    }
    return nullptr;
}

}  // namespace dbg
