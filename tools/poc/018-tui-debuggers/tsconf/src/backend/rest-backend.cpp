// rest-backend.cpp - see rest-backend.h.
#include "backend/rest-backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

#include "backend/asm-subset.h"
#include "backend/disasm-unreal.h"
#include "backend/json-mini.h"

namespace dbg {
namespace {

std::string Hex4Path(uint16_t addr) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "/memory/0x%04X?len=4096&format=full", addr);
    return buf;
}

std::string Hex4(uint16_t addr) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%04X", addr);
    return buf;
}

// Human text for a failed reply (HTTP error bodies carry a "message").
std::string ReplyText(const HttpReply& reply) {
    const Json body = Json::Parse(reply.body, nullptr);
    std::string message;
    if (body.Get("message", &message) && !message.empty()) return message;
    if (reply.body.size() <= 120) return reply.body;
    return reply.error.empty() ? "HTTP " + std::to_string(reply.status) : reply.error;
}

// NOTE: "is_running" stays true while the machine is PAUSED (it marks an
// active instance, not execution) - only the "state" string separates the
// two ("running" vs "paused"/"initialized"/"stopped").
bool StateIsRunning(const Json& root) {
    std::string state;
    if (root.Get("state", &state)) return state == "running";
    bool isRunning = false;
    return root.Get("is_running", &isRunning) && isRunning;
}

MemoryModelHint ModelFromName(const std::string& name) {
    if (name == "PENTAGON") return MemoryModelHint::Pentagon;
    if (name == "128K" || name == "128k") return MemoryModelHint::Standard128;
    if (name == "PLUS3" || name == "PLUS3E") return MemoryModelHint::Plus3;
    if (name == "SCORPION") return MemoryModelHint::Scorpion;
    if (name == "PROFSCORP") return MemoryModelHint::ProfiScorpion;
    if (name == "PROFI") return MemoryModelHint::Profi;
    if (name == "ATM450") return MemoryModelHint::Atm450;
    if (name == "ATM710" || name == "ATM3") return MemoryModelHint::Atm710;
    if (name == "KAY") return MemoryModelHint::Kay;
    if (name == "QUORUM") return MemoryModelHint::Quorum;
    if (name == "GMX") return MemoryModelHint::Gmx;
    if (name == "PHOENIX") return MemoryModelHint::Phoenix;
    if (name == "TSCONF" || name == "TSL") return MemoryModelHint::TsConf;
    return MemoryModelHint::Pentagon;  // unknown short name: Pentagon timing
}

}  // namespace

RestBackend::RestBackend(const std::string& baseUrl) : http_(baseUrl) {
    http_.SetTimeoutSeconds(10);  // tolerant of host-side event-loop stalls
    mem_.assign(0x10000, 0);
}

RestBackend::~RestBackend() {
    // best effort: an instance we created dies with the debugger
    if (ownsInstance_ && connected_ && !instanceId_.empty()) {
        Api("DELETE", "");
    }
}

HttpReply RestBackend::Api(const std::string& method, const std::string& subPath,
                           const std::string& body) {
    const HttpReply reply = http_.Request(method, "/api/v1/emulator/" + instanceId_ + subPath, body);
    if (!reply.ok) {
        lastError_ = "transport: " + reply.error;
    } else if (!reply.IsSuccess()) {
        lastError_ = method + " " + subPath + ": HTTP " + std::to_string(reply.status) + " " +
                     ReplyText(reply);
    } else {
        lastError_.clear();
    }
    return reply;
}

bool RestBackend::EnsureInstance() {
    if (connected_) return true;

    const HttpReply list = http_.Request("GET", "/api/v1/emulator");
    if (!list.ok) {
        lastError_ = "server unreachable: " + list.error;
        return false;
    }
    const Json root = Json::Parse(list.body, nullptr);
    const Json* emulators = root.Find("emulators");
    if (emulators != nullptr && emulators->IsArray() && emulators->Size() > 0) {
        // attach to the first existing instance; it outlives us
        emulators->At(0).Get("id", &instanceId_);
        emulators->At(0).Get("model", &model_);
        ownsInstance_ = false;
    } else {
        const HttpReply started = http_.Request("POST", "/api/v1/emulator/start",
                                                "{\"model\":\"128k\"}");
        if (!started.IsSuccess()) {
            lastError_ = "cannot start instance: " + ReplyText(started);
            return false;
        }
        const Json created = Json::Parse(started.body, nullptr);
        if (!created.Get("id", &instanceId_) || instanceId_.empty()) {
            lastError_ = "start reply missing instance id";
            return false;
        }
        created.Get("model", &model_);
        ownsInstance_ = true;
    }
    if (instanceId_.empty()) {
        lastError_ = "no emulator instance available";
        return false;
    }
    connected_ = true;
    // Do NOT enable PUT /debugmode here: combined with registered
    // breakpoints it deadlocks the host's POST /steps (observed live: the
    // request never answers). Breakpoint mirroring alone is safe, and the
    // exec hook needs the host's DebugOn regardless - see RunUntilBreak.
    EnsurePaused();
    FetchRegs();
    FetchMemory();
    RefreshLabels();
    return true;
}

bool RestBackend::EnsurePaused() {
    const HttpReply state = Api("GET", "");
    if (!state.IsSuccess()) return false;
    const Json root = Json::Parse(state.body, nullptr);
    if (StateIsRunning(root)) {
        const HttpReply paused = Api("POST", "/pause");
        return paused.IsSuccess();
    }
    return true;
}

bool RestBackend::FetchRegs() {
    const HttpReply reply = Api("GET", "/registers");
    if (!reply.IsSuccess()) return false;
    const Json root = Json::Parse(reply.body, nullptr);

    if (regsValid_) prevRegs_ = regs_;
    Z80Regs r;
    auto word = [&root](const char* group, const char* key, uint16_t* out) {
        const Json* g = root.Find(group);
        int64_t v = 0;
        if (g == nullptr || !g->Get(key, &v)) return false;
        *out = static_cast<uint16_t>(v);
        return true;
    };
    bool ok = true;
    uint16_t af = 0, bc = 0, de = 0, hl = 0;
    ok = ok && word("main", "af", &af) && word("main", "bc", &bc) && word("main", "de", &de) &&
         word("main", "hl", &hl);
    r.a = static_cast<uint8_t>(af >> 8);
    r.f = static_cast<uint8_t>(af & 0xFF);
    r.bc = bc;
    r.de = de;
    r.hl = hl;
    uint16_t af2 = 0, bc2 = 0, de2 = 0, hl2 = 0;
    ok = ok && word("alternate", "af_", &af2) && word("alternate", "bc_", &bc2) &&
         word("alternate", "de_", &de2) && word("alternate", "hl_", &hl2);
    r.af2 = af2;
    r.bc2 = bc2;
    r.de2 = de2;
    r.hl2 = hl2;
    ok = ok && word("index", "ix", &r.ix) && word("index", "iy", &r.iy);
    uint16_t i = 0, rr = 0;
    ok = ok && word("special", "pc", &r.pc) && word("special", "sp", &r.sp) &&
         word("special", "i", &i) && word("special", "r", &rr);
    r.i = static_cast<uint8_t>(i);
    r.r = static_cast<uint8_t>(rr);
    uint16_t iff1 = 0, iff2 = 0, im = 0;
    ok = ok && word("interrupt", "iff1", &iff1) && word("interrupt", "iff2", &iff2) &&
         word("interrupt", "im", &im);
    r.iff1 = static_cast<uint8_t>(iff1 & 1);
    r.iff2 = static_cast<uint8_t>(iff2 & 1);
    r.im = static_cast<uint8_t>(im & 3);
    if (!ok) {
        lastError_ = "registers reply missing fields";
        return false;
    }
    // WebAPI serves no T-state counter / halt flag: both stay at defaults.
    r.t = 0;
    r.lastBranch = 0;
    regs_ = r;
    regsValid_ = true;
    return true;
}

bool RestBackend::FetchMemory() {
    // 16 x 4096 reads (server caps len at 4096)
    for (uint32_t base = 0; base < 0x10000; base += 0x1000) {
        const HttpReply reply = Api("GET", Hex4Path(static_cast<uint16_t>(base)));
        if (!reply.IsSuccess()) return false;
        const Json root = Json::Parse(reply.body, nullptr);
        const Json* data = root.Find("data");
        if (data == nullptr || !data->IsArray() || data->Size() != 0x1000) {
            lastError_ = "memory reply at " + Hex4(static_cast<uint16_t>(base)) +
                         " is not a 4096-byte data array";
            return false;
        }
        for (size_t i = 0; i < 0x1000; ++i) {
            int64_t v = 0;
            if (!data->At(i).AsInt(&v)) return false;
            mem_[base + i] = static_cast<uint8_t>(v);
        }
    }
    memValid_ = true;
    return true;
}

void RestBackend::RefreshLabels() {
    labels_.clear();
    const HttpReply reply = Api("GET", "/labels");
    if (!reply.IsSuccess()) return;
    const Json root = Json::Parse(reply.body, nullptr);
    const Json* items = root.Find("labels");
    if (items == nullptr || !items->IsArray()) return;
    for (size_t i = 0; i < items->Size(); ++i) {
        const Json& item = items->At(i);
        int64_t address = 0;
        std::string name;
        if (item.Get("address", &address) && item.Get("name", &name) && !name.empty()) {
            labels_.emplace_back(static_cast<uint16_t>(address), name);
        }
    }
}

// -- machine / snapshot ---------------------------------------------------------

MachineInfo RestBackend::GetMachineInfo() {
    MachineInfo info;
    info.model = ModelFromName(model_);
    // Pentagon frame timing; standard models share the 69888/70908 split
    // (only the timing-sensitive panels notice the difference).
    info.frameLength = (info.model == MemoryModelHint::Pentagon) ? 69888 : 70908;
    info.intStart = 0;
    info.intLength = 32;
    info.ayScheme = AyScheme::Single;
    info.trdosPresent = true;
    // Page names need a paging-state decode (GET /state/paging gives banks,
    // not the 4-char titles); static 128K defaults until that lands.
    info.pages[0] = {"ROM0", true};
    info.pages[1] = {"ROM1", true};
    info.pages[2] = {"RAM0", false};
    info.pages[3] = {"RAM3", false};
    return info;
}

Z80Regs RestBackend::GetRegs(int) {
    if (!EnsureInstance()) return regs_;
    if (!regsValid_) FetchRegs();
    return regs_;
}

BranchInfo RestBackend::GetBranchAtPc(int) {
    if (!EnsureInstance()) return BranchInfo{};
    if (!memValid_ && !FetchMemory()) return BranchInfo{};
    if (!regsValid_ && !FetchRegs()) return BranchInfo{};
    return UnrealBranchInfo(mem_.data(), regs_.pc, regs_);
}

int64_t RestBackend::GetTimeDelta(int) {
    // WebAPI has no T-state counter endpoint; the "time delta" cell reads 0
    // over REST until one lands (the mock shows the real figure).
    return 0;
}

// -- memory / disassembly --------------------------------------------------------

std::vector<uint8_t> RestBackend::ReadMemory(int, uint16_t addr, uint16_t len) {
    std::vector<uint8_t> out;
    if (!EnsureInstance()) return out;
    if (!memValid_ && !FetchMemory()) return out;
    out.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        out.push_back(mem_[static_cast<uint16_t>(addr + i)]);
    }
    return out;
}

void RestBackend::WriteMemory(int, uint16_t addr, const std::vector<uint8_t>& bytes) {
    if (!EnsureInstance() || bytes.empty()) return;
    std::string body = "{\"data\":[";
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) body += ",";
        body += std::to_string(static_cast<int>(bytes[i]));
    }
    body += "]}";
    char path[32];
    std::snprintf(path, sizeof path, "/memory/0x%04X", addr);
    if (Api("PUT", path, body).IsSuccess()) {
        for (size_t i = 0; i < bytes.size(); ++i) {
            mem_[static_cast<uint16_t>(addr + static_cast<uint16_t>(i))] = bytes[i];
        }
    }
}

std::vector<DisasmLine> RestBackend::Disassemble(int, uint16_t addr, int nLines, bool withLabels) {
    std::vector<DisasmLine> lines;
    if (!EnsureInstance()) return lines;
    if (!memValid_ && !FetchMemory()) return lines;
    lines.reserve(static_cast<size_t>(nLines));
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

uint16_t RestBackend::PrevInstruction(int, uint16_t addr) {
    if (!EnsureInstance() || (!memValid_ && !FetchMemory())) return addr;
    return UnrealPrevInstruction(mem_.data(), addr);
}

// -- peripherals -------------------------------------------------------------------

PortsState RestBackend::GetPorts() {
    // GET /ports serves the static port MAP, not latched values: the panel
    // reflects writes made through this debugger (Set7ffd/SetExtPort/Out).
    return ports_;
}

Beta128State RestBackend::GetBeta128() {
    // no WD1793 state endpoint yet: the beta128 column reads zeros
    return Beta128State{};
}

AyState RestBackend::GetAy() {
    // GET /state/audio/ay exists but serves register arrays in a different
    // shape; wiring it lands with the AY panel milestone.
    return ay_;
}

void RestBackend::SelectAyChip(int chip) {
    ay_.scheme = AyScheme::Single;
    ay_.activeChip = chip & 1;
}

std::vector<uint8_t> RestBackend::ReadCmos() { return std::vector<uint8_t>(256, 0); }
std::vector<uint8_t> RestBackend::ReadNvram() { return std::vector<uint8_t>(2048, 0); }
std::vector<uint8_t> RestBackend::ReadCompPal() { return std::vector<uint8_t>(64, 0); }
// CMOS/NVRAM/comppal dumps have no WebAPI surface; the editors open empty.

DiskTrackData RestBackend::ReadDiskTrack(int, int) {
    DiskTrackData d;
    d.present = false;  // no disk-sector dump endpoint: "track not found"
    return d;
}

void RestBackend::WriteDiskByte(int, int, bool, int, uint8_t) {
    // accepted and dropped (no disk-image write endpoint)
}

ScreenPixels RestBackend::GetScreenPreview(int) {
    // no palette-index pixel endpoint (screen state is a digest); the
    // preview window stays blank over REST for now.
    return ScreenPixels{};
}

// -- execution control --------------------------------------------------------------

std::optional<IDebuggerBackend::StopReason> RestBackend::Step(int) {
    if (!EnsureInstance() || !EnsurePaused()) return std::nullopt;
    if (regsValid_) prevRegs_ = regs_;
    const HttpReply reply = Api("POST", "/step");
    if (!reply.IsSuccess()) return std::nullopt;  // lastError_ tells why
    memValid_ = false;
    FetchRegs();
    FetchMemory();
    // the server steps without reporting breakpoint triggers; classify from
    // the new PC against the local breakpoint bits
    if ((bpBits_[regs_.pc] & 0x01) != 0) return StopReason::Breakpoint;
    return std::nullopt;
}

IDebuggerBackend::StopReason RestBackend::RunUntilBreak(int, const StopRequest& request) {
    if (!EnsureInstance() || !EnsurePaused()) return StopReason::MaxInstructions;
    if (!regsValid_) FetchRegs();  // WriteReg & co. leave the cache stale
    if (regsValid_) prevRegs_ = regs_;  // §9.5 (b): snapshot at leave

    // The server's per-PC exec hook needs the host's DebugOn, which no WebAPI
    // route turns on - so instead of resume+poll we drive the machine with
    // synchronous batched POST /steps: each reply carries the landing PC/SP,
    // checked against the local stop conditions at every batch boundary.
    // Batch sizes alternate 128/129 so tight loops cannot phase-lock to the
    // boundary check, and once the run-to target is only a few instructions
    // away we single-step to land on it exactly.
    StopReason reason = StopReason::MaxInstructions;
    uint64_t count = 0;
    uint16_t pc = regs_.pc;
    uint16_t sp = regs_.sp;
    int jitter = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
        if ((bpBits_[pc] & 0x01) != 0) {
            reason = StopReason::Breakpoint;
            break;
        }
        if (request.hasStopHere && pc == request.stopHere) {
            reason = StopReason::StopHere;
            break;
        }
        if (request.hasStopSp && sp == request.stopSp &&
            !(pc >= request.loopR1 && pc <= request.loopR2)) {
            reason = StopReason::StopSp;
            break;
        }
        // near the run-to target: exact landing by single-stepping (a tight
        // loop can cross a straight boundary inside one batch otherwise)
        if (request.hasStopHere) {
            const uint16_t ahead = static_cast<uint16_t>(request.stopHere - pc);
            const uint16_t behind = static_cast<uint16_t>(pc - request.stopHere);
            if (ahead <= 4 || behind <= 4) {
                bool done = false;
                for (int i = 0; i < 8; ++i) {
                    const HttpReply reply = Api("POST", "/step");
                    if (!reply.IsSuccess()) {
                        reason = StopReason::MaxInstructions;
                        done = true;
                        break;
                    }
                    ++count;
                    const Json root = Json::Parse(reply.body, nullptr);
                    int64_t rpc = pc, rsp = sp;
                    root.Get("pc", &rpc);
                    root.Get("sp", &rsp);
                    pc = static_cast<uint16_t>(rpc);
                    sp = static_cast<uint16_t>(rsp);
                    if (pc == request.stopHere) {
                        reason = StopReason::StopHere;
                        done = true;
                        break;
                    }
                }
                if (done) break;
                continue;  // a branch left the window: re-check conditions
            }
        }
        const int batch = 128 + (jitter++ & 1);
        char body[48];
        std::snprintf(body, sizeof body, "{\"count\":%d}", batch);
        const HttpReply reply = Api("POST", "/steps", body);
        if (!reply.IsSuccess()) break;  // lastError_ tells why
        count += static_cast<uint64_t>(batch);
        const Json root = Json::Parse(reply.body, nullptr);
        int64_t rpc = 0;
        if (!root.Get("pc", &rpc)) break;
        pc = static_cast<uint16_t>(rpc);
        int64_t rsp = sp;
        if (root.Get("sp", &rsp)) sp = static_cast<uint16_t>(rsp);
        if (count >= request.maxInstructions) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
    }

    memValid_ = false;
    regsValid_ = false;
    FetchRegs();
    FetchMemory();
    return reason;
}

uint64_t RestBackend::GetAbsoluteT(int) {
    // no T-state endpoint (same gap as GetTimeDelta)
    return 0;
}

void RestBackend::SetDebugMark(int, uint64_t absT) { markT_ = absT; }

// -- register / port writes -----------------------------------------------------------

void RestBackend::WriteReg(int, RegField field, unsigned value) {
    if (!EnsureInstance()) return;
    const char* name = nullptr;
    switch (field) {
    case RegField::A: name = "A"; break;
    case RegField::F: name = "F"; break;
    case RegField::Bc: name = "BC"; break;
    case RegField::De: name = "DE"; break;
    case RegField::Hl: name = "HL"; break;
    case RegField::Af2: name = "AF'"; break;
    case RegField::Bc2: name = "BC'"; break;
    case RegField::De2: name = "DE'"; break;
    case RegField::Hl2: name = "HL'"; break;
    case RegField::Sp: name = "SP"; break;
    case RegField::Pc: name = "PC"; break;
    case RegField::Ix: name = "IX"; break;
    case RegField::Iy: name = "IY"; break;
    case RegField::I: name = "I"; break;
    case RegField::R: name = "R"; break;
    case RegField::Im:
    case RegField::Iff1:
    case RegField::Iff2:
        // server register table has no IM/IFF entries; nothing to send
        return;
    }
    Api("PUT", std::string("/registers/") + name,
        "{\"value\":" + std::to_string(value & 0xFFFF) + "}");
    regsValid_ = false;
}

void RestBackend::Out(uint16_t port, uint8_t value) {
    // no port-write endpoint; latch locally so the panels still react
    switch (port) {
    case 0xFE: ports_.fe = value; break;
    case 0x7FFD:
        ports_.p7ffd = value;
        ports_.lock48 = (value & 0x20) != 0;
        break;
    case 0xEFF7: ports_.eff7 = value; break;
    default: break;
    }
}

void RestBackend::Set7ffd(uint8_t value) { Out(0x7FFD, value); }

void RestBackend::SetExtPort(uint8_t value) {
    // generic extended port latch (model-specific meaning on the server)
    ports_.hasExtPort = true;
    ports_.extValue = value;
}

// -- breakpoints / conditions -----------------------------------------------------------

uint8_t RestBackend::BpBitsAt(uint16_t addr) { return bpBits_[addr]; }

void RestBackend::SetBpBits(uint16_t addr, uint8_t bits) {
    // local bits are the truth (checked at /steps boundaries, see
    // RunUntilBreak); exec/read/write bits also mirror to the server so the
    // breakpoints can fire inside the emulator once a WebAPI route turns on
    // the host's debug hook. Mirroring alone is safe - but keep it away from
    // PUT /debugmode, that combination deadlocks POST /steps (EnsureInstance).
    static const char* const kTypes[3] = {"exec", "read", "write"};
    const uint8_t old = bpBits_[addr];
    bpBits_[addr] = bits;
    if (!connected_ && !EnsureInstance()) return;
    for (int bit = 0; bit < 3; ++bit) {
        const auto key = std::make_pair(addr, bit);
        const bool was = (old & (1u << bit)) != 0;
        const bool now = (bits & (1u << bit)) != 0;
        if (!was && now) {
            char body[64];
            std::snprintf(body, sizeof body, "{\"type\":\"%s\",\"address\":%u}", kTypes[bit],
                          static_cast<unsigned>(addr));
            const HttpReply added = Api("POST", "/breakpoints", body);
            const Json root = Json::Parse(added.body, nullptr);
            int64_t id = 0;
            if (added.IsSuccess() && root.Get("id", &id)) serverBpIds_[key] = static_cast<int>(id);
        } else if (was && !now) {
            const auto it = serverBpIds_.find(key);
            if (it != serverBpIds_.end()) {
                Api("DELETE", "/breakpoints/" + std::to_string(it->second));
                serverBpIds_.erase(it);
            }
        }
    }
}

bool RestBackend::HasAnyBreakpoints() {
    for (const uint8_t bits : bpBits_) {
        if (bits & 0x07) return true;
    }
    return !conditions_.empty();
}

ExprCompileResult RestBackend::AddCondition(int, const std::string& text) {
    // syntax is validated locally with the shared expression compiler;
    // remote evaluation needs per-instruction push (WebSocket transport).
    ExprCompileResult res;
    CompiledExpr expr;
    std::string error;
    if (!CompiledExpr::Compile(text, &expr, &error)) {
        res.error = error;
        return res;
    }
    conditions_.push_back(text);
    res.ok = true;
    return res;
}

void RestBackend::DeleteCondition(int, int index) {
    if (index >= 0 && static_cast<size_t>(index) < conditions_.size()) {
        conditions_.erase(conditions_.begin() + index);
    }
}

std::vector<std::string> RestBackend::GetConditions(int) { return conditions_; }

int RestBackend::ConditionCount(int) { return static_cast<int>(conditions_.size()); }

// -- assemble / find --------------------------------------------------------------------

AssembleResult RestBackend::Assemble(uint16_t addr, const std::string& text) {
    // shared subset assembler; a success is pushed through the same
    // PUT /memory/{addr} path the memory editor uses
    const AssembleResult local = AssembleSubset(addr, text);
    if (!local.ok) return local;
    WriteMemory(0, addr, local.bytes);
    return local;
}

FindResult RestBackend::FindBytes(EditorSpace space, uint16_t start, const BytePattern& pattern,
                                  const BytePattern& mask) {
    // only the live 64K map exists remotely; CMOS/NVRAM/disk find is refused
    FindResult res;
    if (space != EditorSpace::Mem) return res;
    if (!EnsureInstance() || (!memValid_ && !FetchMemory())) return res;
    if (pattern.empty() || pattern.size() > mem_.size()) return res;
    const uint8_t* data = mem_.data();
    const size_t n = mem_.size();
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

// -- labels (§11) -------------------------------------------------------------------------

std::optional<std::string> RestBackend::LabelFor(uint16_t addr) {
    for (const auto& l : labels_) {
        if (l.first == addr) return l.second;
    }
    return std::nullopt;
}

std::vector<std::pair<uint16_t, std::string>> RestBackend::LabelsInCurrentMap() {
    if (!EnsureInstance()) return {};
    return labels_;
}

void RestBackend::LoadUserLabels(const std::string& path, std::string* error, int* loadedCount) {
    if (loadedCount != nullptr) *loadedCount = 0;
    if (!EnsureInstance()) {
        if (error != nullptr) *error = lastError_;
        return;
    }
    std::ifstream in(path);
    if (!in) {
        if (error != nullptr) {
            char buf[512];
            std::snprintf(buf, sizeof buf, "can't find label file %s", path.c_str());
            *error = buf;
        }
        return;
    }
    // same file format as the mock ("XXXX name" / "PP:XXXX name" lines);
    // every label is mirrored to the server so all clients see it
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        size_t b = line.find_first_not_of(" \t\r");
        size_t e = line.find_last_not_of(" \t\r");
        if (b == std::string::npos) continue;
        line = line.substr(b, e - b + 1);
        if (line.empty()) continue;
        uint16_t addr = 0;
        size_t nameAt = std::string::npos;
        unsigned page = 0, offset = 0;
        if (std::sscanf(line.c_str(), "%u:%4x", &page, &offset) == 2) {
            addr = static_cast<uint16_t>((page & 0x3F) * 0x4000u + (offset & 0x3FFFu));
            nameAt = line.find(' ');
        } else {
            unsigned a16 = 0;
            if (std::sscanf(line.c_str(), "%4x", &a16) == 1 && line.size() > 4 && line[4] == ' ') {
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
        const std::string body = "{\"name\":\"" + JsonEscape(name) + "\",\"address\":" +
                                  std::to_string(addr) + "}";
        if (Api("POST", "/labels", body).IsSuccess()) {
            labels_.emplace_back(addr, name);
            if (loadedCount != nullptr) ++(*loadedCount);
        }
    }
}

std::vector<IDebuggerBackend::ImportItem> RestBackend::ScanLabelImports() {
    // scanning XAS/ALASM tables needs the raw bank images (state/memory
    // endpoints serve them, but the table walkers are not ported yet)
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

void RestBackend::ImportLabels(int) {
    // disabled items only: nothing to import
}

// -- ripper (§6.3) -------------------------------------------------------------------------

void RestBackend::ArmRipper(bool, bool) {
    // read/write coverage needs per-access push (WebSocket transport);
    // arming is remembered so the dialog round-trip still works
    ripperArmed_ = true;
}

bool RestBackend::RipperArmed() { return ripperArmed_; }

std::vector<uint8_t> RestBackend::CollectRipper(uint8_t unrefByte) {
    // no coverage stream over REST: every byte reads unreferenced
    ripperArmed_ = false;
    return std::vector<uint8_t>(0x10000, unrefByte);
}

}  // namespace dbg
