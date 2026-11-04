// webapi-backend.cpp - see webapi-backend.h.
#include "backend/webapi-backend.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

// "0x7FFD", "#7FFD", "7FFD" or a number: the server writes ports and latch values either way
bool HexOrNumber(const Json& value, int64_t* out) {
    if (value.AsInt(out)) return true;
    std::string text;
    if (!value.AsString(&text)) return false;
    size_t at = 0;
    if (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0) at = 2;
    else if (!text.empty() && text[0] == '#') at = 1;
    if (at >= text.size()) return false;
    char* end = nullptr;
    const long long v = std::strtoll(text.c_str() + at, &end, 16);
    if (end == nullptr || *end != '\0') return false;
    *out = v;
    return true;
}

bool Base64Decode(const std::string& text, std::vector<uint8_t>* out) {
    out->clear();
    out->reserve(text.size() / 4 * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        int v = 0;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == '\r' || c == '\n') continue;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<uint8_t>(acc >> bits));
        }
    }
    return true;
}

// A dense byte array of a reply ("data": [..]) into @p out; false when absent or not bytes
bool ByteArray(const Json* data, std::vector<uint8_t>* out) {
    if (data == nullptr || !data->IsArray()) return false;
    out->resize(data->Size());
    for (size_t i = 0; i < data->Size(); ++i) {
        int64_t v = 0;
        if (!data->At(i).AsInt(&v)) return false;
        (*out)[i] = static_cast<uint8_t>(v);
    }
    return true;
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

WebApiBackend::WebApiBackend(std::unique_ptr<IApiTransport> transport) : http_(std::move(transport)) {
    http_->SetTimeoutSeconds(10);  // tolerant of host-side event-loop stalls
    mem_.assign(0x10000, 0);
}

WebApiBackend::~WebApiBackend() = default;

std::string WebApiBackend::WaitingFor() const {
    const std::string what = wanted_.empty() ? SuitableText() : SuitableText() + " " + wanted_;
    return endpoint_.empty() ? what : what + " at " + endpoint_;
}

void WebApiBackend::Lost() {
    // The caches stay: the UI shows them grayed out while it waits
    connected_ = false;
    instanceId_.clear();
    running_ = false;
    runToArmed_ = false;
    lastError_.clear();
    nextAttach_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

bool WebApiBackend::InstanceListed() {
    const HttpReply list = http_->Request("GET", "/api/v1/emulator");
    if (!list.ok) return false;
    if (!list.IsSuccess()) return true;  // the server answers: the instance is asked itself
    const Json root = Json::Parse(list.body, nullptr);
    const Json* emulators = root.Find("emulators");
    if (emulators == nullptr || !emulators->IsArray()) return true;
    for (size_t i = 0; i < emulators->Size(); ++i) {
        std::string id;
        if (emulators->At(i).Get("id", &id) && id == instanceId_) return true;
    }
    return false;
}

HttpReply WebApiBackend::Api(const std::string& method, const std::string& subPath,
                           const std::string& body) {
    if (instanceId_.empty()) return HttpReply{};  // offline: nothing to ask
    const HttpReply reply = http_->Request(method, "/api/v1/emulator/" + instanceId_ + subPath, body);
    if (!reply.ok) {
        Lost();  // the server is gone
    } else if (reply.status == 404 && !InstanceListed()) {
        Lost();  // the instance is gone
    } else if (!reply.IsSuccess()) {
        lastError_ = method + " " + subPath + ": HTTP " + std::to_string(reply.status) + " " +
                     ReplyText(reply);
    } else {
        lastError_.clear();
    }
    return reply;
}

bool WebApiBackend::EnsureInstance() {
    if (connected_) return true;
    // Offline: one look a second, whoever asks (every paint reads the machine)
    const auto now = std::chrono::steady_clock::now();
    if (now < nextAttach_) return false;
    nextAttach_ = now + std::chrono::seconds(1);

    const HttpReply list = http_->Request("GET", "/api/v1/emulator");
    if (!list.IsSuccess()) return false;
    const Json root = Json::Parse(list.body, nullptr);
    const Json* emulators = root.Find("emulators");
    if (emulators == nullptr || !emulators->IsArray()) return false;
    // The first running instance this debugger suits (the wanted one only, when named); it outlives us
    std::string id, model;
    for (size_t i = 0; i < emulators->Size(); ++i) {
        std::string symbolic;
        emulators->At(i).Get("id", &id);
        emulators->At(i).Get("symbolic_id", &symbolic);
        emulators->At(i).Get("model", &model);
        const bool wanted = wanted_.empty() || wanted_ == id || (!symbolic.empty() && wanted_ == symbolic);
        if (!id.empty() && wanted && Suitable(model)) break;
        id.clear();
    }
    if (id.empty()) return false;
    instanceId_ = id;
    model_ = model;
    connected_ = true;
    attachedOnce_ = true;
    nextAlive_ = now + std::chrono::seconds(1);
    // Steps, stops and breakpoints of other clients come as topics (websocket-interface.md §3.3)
    pushed_ = http_->Subscribe("[\"debug.events\",\"run.state\"]");
    // The debugger runs the machine with the emulator's own breakpoints (Continue, RunTo): they fire in debug mode
    Api("PUT", "/debugmode", "{\"enabled\":true}");
    EnsurePaused();
    FetchRegs();
    FetchMemory();
    RefreshLabels();
    SyncBreakpoints();
    return true;
}

bool WebApiBackend::PollExternalChanges() {
    if (!connected_) return EnsureInstance();  // attached again: everything is read, paint it
    {
        // The instance may go without a word (its emulator closed): asked about once a second
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextAlive_) {
            nextAlive_ = now + std::chrono::seconds(1);
            if (!InstanceListed()) {
                Lost();
                return true;  // paint the waiting dialog
            }
        }
    }
    bool refresh = false;
    bool breakpoints = false;
    bool stateChanged = false;
    if (pushed_) {
        std::vector<std::string> events;
        bool reconnected = false;
        http_->TakeEvents(&events, &reconnected);
        refresh = reconnected;  // what happened in between is not known
        breakpoints = reconnected;
        for (const std::string& text : events) {
            const Json message = Json::Parse(text, nullptr);
            std::string topic, instance;
            message.Get("t", &topic);
            message.Get("instance", &instance);
            if (instance != instanceId_) continue;
            if (topic == "run.state") {
                const Json* value = message.Find("value");
                std::string state;
                if (value != nullptr && value->Get("state", &state)) {
                    const bool running = state == "running";
                    if (running_ != running) stateChanged = true;
                    if (running_ && !running) refresh = true;  // stopped: at a breakpoint, by a pause, by others
                    running_ = running;
                }
            } else if (topic == "debug.events") {
                std::string event;
                message.Get("event", &event);
                if (event == "paused" || event == "step_done") {
                    refresh = true;
                    if (event == "paused") {
                        stateChanged = stateChanged || running_;
                        running_ = false;
                    }
                } else if (event == "resumed") {
                    stateChanged = stateChanged || !running_;
                    running_ = true;
                } else if (event == "breakpoints_changed") {
                    breakpoints = true;
                }
            }
        }
    } else {
        // REST: no topics; the state and the registers twice a second tell a run, a step or a stop of others
        const auto now = std::chrono::steady_clock::now();
        if (now < nextPoll_) return false;
        nextPoll_ = now + std::chrono::milliseconds(500);
        const HttpReply state = Api("GET", "");
        if (!connected_) return true;  // lost: paint the waiting dialog
        if (!state.IsSuccess()) return false;
        const bool running = StateIsRunning(Json::Parse(state.body, nullptr));
        stateChanged = running != running_;
        running_ = running;
        refresh = !running;  // a running machine is read when it stops
        breakpoints = true;
    }
    bool changed = stateChanged;
    if (refresh && !running_) {
        if (runToArmed_) DropRunTo();
        changed = RefreshAfterForeignChange() || changed;
    }
    if (breakpoints) {
        const std::array<uint8_t, 0x10000> before = bpBits_;
        SyncBreakpoints();
        changed = changed || before != bpBits_;
    }
    return changed;
}

bool WebApiBackend::RefreshAfterForeignChange() {
    const Z80Regs before = regs_;
    const Z80Regs beforePrev = prevRegs_;
    const uint64_t snapshot = snapshots_;
    if (!FetchRegs()) return false;
    // A step of this debugger comes back as an event too: the same registers, nothing to read again
    if (regs_ == before) {
        prevRegs_ = beforePrev;  // the change marks of the last step stay
        snapshots_ = snapshot;  // the caches over the snapshot stay valid
        return false;
    }
    prevRegs_ = before;
    FetchMemory();
    RefreshLabels();
    return true;
}

void WebApiBackend::RefreshAfterRun() {
    regsValid_ = false;
    memValid_ = false;
    FetchRegs();
    FetchMemory();
}

namespace {
constexpr const char* kRunToGroup = "tui-run-to";
// The conditions (§10.2): breakpoints over the whole 64K with the condition, the emulator evaluates them
constexpr const char* kConditionGroup = "tui-conditions";
}  // namespace

void WebApiBackend::Continue(int) {
    if (!EnsureInstance() || running_) return;
    if (regsValid_) prevRegs_ = regs_;
    if (Api("POST", "/resume").IsSuccess()) running_ = true;
}

void WebApiBackend::Pause() {
    if (!EnsureInstance()) return;
    Api("POST", "/pause");  // the stop comes back as an event and is read then
}

void WebApiBackend::StepOver(int cpu) {
    if (!EnsureInstance() || running_) return;
    // HALT: to the next instruction, as UnrealSpeccy does (the server's /stepover would enter the interrupt)
    const std::vector<DisasmLine> line = Disassemble(cpu, regs_.pc, 1, false);
    if (!line.empty()) {
        std::string m = line[0].mnemonic;
        for (char& c : m) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (m.compare(0, 4, "halt") == 0) {
            RunTo(cpu, static_cast<uint16_t>(regs_.pc + line[0].len));
            return;
        }
    }
    if (regsValid_) prevRegs_ = regs_;
    http_->SetTimeoutSeconds(20);  // answered when the machine has stopped after the CALL
    Api("POST", "/stepover");
    http_->SetTimeoutSeconds(10);
    RefreshAfterRun();
}

void WebApiBackend::StepOut(int) {
    if (!EnsureInstance() || running_) return;
    if (regsValid_) prevRegs_ = regs_;
    http_->SetTimeoutSeconds(20);
    Api("POST", "/stepout");
    http_->SetTimeoutSeconds(10);
    RefreshAfterRun();
}

void WebApiBackend::RunTo(int cpu, uint16_t addr) {
    if (!EnsureInstance() || running_) return;
    // A breakpoint of its own group, gone at the next stop; on a user's breakpoint the machine stops there anyway
    if ((bpBits_[addr] & 0x01) == 0) {
        char body[128];
        std::snprintf(body, sizeof body,
                      "{\"type\":\"execution\",\"address\":%u,\"group\":\"%s\",\"note\":\"run to the cursor\"}",
                      static_cast<unsigned>(addr), kRunToGroup);
        runToArmed_ = Api("POST", "/breakpoints", body).IsSuccess();
    }
    Continue(cpu);
}

void WebApiBackend::DropRunTo() {
    runToArmed_ = false;
    Api("POST", "/breakpoints/remove", std::string("{\"select\":\"group:") + kRunToGroup + "\"}");
}

void WebApiBackend::SyncBreakpoints() {
    const HttpReply reply = Api("GET", "/breakpoints");
    if (!reply.IsSuccess()) return;
    const Json root = Json::Parse(reply.body, nullptr);
    const Json* list = root.Find("breakpoints");
    if (list == nullptr || !list->IsArray()) return;
    std::array<uint8_t, 0x10000> bits{};
    std::map<std::pair<uint16_t, int>, int> ids;
    std::vector<std::string> conditions;
    std::vector<int> conditionIds;
    for (size_t i = 0; i < list->Size(); ++i) {
        const Json& bp = list->At(i);
        int64_t id = 0, address = 0;
        bool active = true, exec = false, read = false, write = false;
        bp.Get("active", &active);
        if (!active || !bp.Get("id", &id) || !bp.Get("address", &address)) continue;
        std::string group;
        if (bp.Get("group", &group) && group == kRunToGroup) continue;
        if (group == kConditionGroup) {
            std::string condition;
            bp.Get("condition", &condition);
            conditions.push_back(condition);
            conditionIds.push_back(static_cast<int>(id));
            continue;
        }
        bp.Get("execute", &exec);
        bp.Get("read", &read);
        bp.Get("write", &write);
        const uint16_t addr = static_cast<uint16_t>(address);
        const bool flags[3] = {exec, read, write};
        for (int bit = 0; bit < 3; ++bit) {
            if (!flags[bit]) continue;
            bits[addr] = static_cast<uint8_t>(bits[addr] | (1u << bit));
            ids[std::make_pair(addr, bit)] = static_cast<int>(id);
        }
    }
    bpBits_ = bits;
    serverBpIds_ = ids;
    conditions_ = conditions;
    conditionIds_ = conditionIds;
}

bool WebApiBackend::EnsurePaused() {
    const HttpReply state = Api("GET", "");
    if (!state.IsSuccess()) return false;
    const Json root = Json::Parse(state.body, nullptr);
    if (StateIsRunning(root)) {
        const HttpReply paused = Api("POST", "/pause");
        return paused.IsSuccess();
    }
    return true;
}

bool WebApiBackend::FetchRegs() {
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
    // The T-state in the frame and HALT; the frame number and length from the beam make the absolute T of the time delta
    int64_t t = 0;
    if (const Json* special = root.Find("special")) special->Get("t", &t);
    r.t = static_cast<uint32_t>(t);
    if (const Json* interrupt = root.Find("interrupt")) interrupt->Get("halted", &r.halted);
    r.lastBranch = 0;
    const HttpReply beam = Api("GET", "/video/beam");
    if (beam.IsSuccess()) {
        const Json b = Json::Parse(beam.body, nullptr);
        int64_t frame = 0, frameT = 0;
        const Json* timing = b.Find("frame_timing");
        if (b.Get("frame", &frame) && timing != nullptr && timing->Get("frame_tstates", &frameT) && frameT > 0) {
            frameT_ = static_cast<uint32_t>(frameT);
            absT_ = static_cast<uint64_t>(frame) * static_cast<uint64_t>(frameT) + r.t;
        }
    }
    regs_ = r;
    regsValid_ = true;
    ++snapshots_;
    FetchPaging();
    FetchPorts();
    return true;
}

void WebApiBackend::FetchPorts() {
    // GET /ports: the decoder's map, the latched value of each port that keeps one (#FE, #7FFD, #EFF7, the model's
    // extended port: #1FFD, #DFFD, #77 ...)
    const HttpReply reply = Api("GET", "/ports");
    if (!reply.IsSuccess()) return;
    const Json root = Json::Parse(reply.body, nullptr);
    const Json* entries = root.Find("entries");
    if (entries == nullptr || !entries->IsArray()) return;
    PortsState p;
    p.dosPortsActive = ports_.dosPortsActive;
    for (const Json& e : entries->Items()) {
        const Json* port = e.Find("port");
        const Json* value = e.Find("latch_value");
        int64_t number = 0, v = 0;
        if (port == nullptr || value == nullptr || !HexOrNumber(*port, &number) || !HexOrNumber(*value, &v)) continue;
        const uint8_t byte = static_cast<uint8_t>(v);
        if (number == 0xFE) p.fe = byte;
        else if (number == 0x7FFD) {
            p.p7ffd = byte;
            p.lock48 = (byte & 0x20) != 0;
        } else if (number == 0xEFF7) p.eff7 = byte;
        else if (!p.hasExtPort) {
            p.hasExtPort = true;
            p.extPort = static_cast<uint16_t>(number);
            p.extValue = byte;
        }
    }
    ports_ = p;
}

void WebApiBackend::FetchPaging() {
    // GET /state/paging: a bank per 16K window, its type and page number
    const HttpReply reply = Api("GET", "/state/paging");
    if (!reply.IsSuccess()) return;
    const Json root = Json::Parse(reply.body, nullptr);
    root.Get("trdos_active", &ports_.dosPortsActive);
    const Json* banks = root.Find("banks");
    if (banks == nullptr || !banks->IsArray()) return;
    for (size_t w = 0; w < banks->Size() && w < pages_.size(); ++w) {
        const Json& bank = banks->At(w);
        std::string type;
        int64_t page = 0;
        bool writable = true;
        bank.Get("type", &type);
        bank.Get("page", &page);
        bank.Get("writable", &writable);
        char name[8];  // 5 characters: "RAM 5", "ROM 0", "RAMF1"
        if (page < 16)
            std::snprintf(name, sizeof name, "%.3s %X", type.c_str(), static_cast<unsigned>(page));
        else
            std::snprintf(name, sizeof name, "%.3s%02X", type.c_str(), static_cast<unsigned>(page & 0xFF));
        pages_[w] = {name, !writable};
    }
}

bool WebApiBackend::FetchMemory() {
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

void WebApiBackend::RefreshLabels() {
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

MachineInfo WebApiBackend::GetMachineInfo() {
    MachineInfo info;
    info.model = ModelFromName(model_);
    info.intStart = 0;
    info.intLength = 32;
    info.ayScheme = AyScheme::Single;
    info.trdosPresent = true;
    if (EnsureInstance() && !regsValid_) FetchRegs();
    // The frame's length from the beam (GET /video/beam with the registers); before the first read a 128K's
    info.frameLength = frameT_ != 0 ? frameT_ : 70908;
    if (ay_.scheme != AyScheme::None) info.ayScheme = ay_.scheme;
    info.pages = pages_;  // GET /state/paging at the last stop
    return info;
}

Z80Regs WebApiBackend::GetRegs(int) {
    if (!EnsureInstance()) return regs_;
    if (!regsValid_) FetchRegs();
    return regs_;
}

// Offline the readers below work on the last memory and registers read (the grayed screen shows them)
BranchInfo WebApiBackend::GetBranchAtPc(int) {
    if (EnsureInstance() && ((!memValid_ && !FetchMemory()) || (!regsValid_ && !FetchRegs()))) return BranchInfo{};
    return UnrealBranchInfo(mem_.data(), regs_.pc, regs_);
}

int64_t WebApiBackend::GetTimeDelta(int) {
    if (EnsureInstance() && !regsValid_) FetchRegs();
    return static_cast<int64_t>(absT_) - static_cast<int64_t>(markT_);
}

// -- memory / disassembly --------------------------------------------------------

std::vector<uint8_t> WebApiBackend::ReadMemory(int, uint16_t addr, uint16_t len) {
    // Always len bytes (the panels index them): offline the last memory read, the grayed screen shows it
    std::vector<uint8_t> out;
    if (EnsureInstance() && !memValid_) FetchMemory();
    out.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        out.push_back(mem_[static_cast<uint16_t>(addr + i)]);
    }
    return out;
}

void WebApiBackend::WriteMemory(int, uint16_t addr, const std::vector<uint8_t>& bytes) {
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

std::vector<DisasmLine> WebApiBackend::Disassemble(int, uint16_t addr, int nLines, bool withLabels) {
    std::vector<DisasmLine> lines;
    if (EnsureInstance() && !memValid_ && !FetchMemory()) return lines;
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

uint16_t WebApiBackend::PrevInstruction(int, uint16_t addr) {
    if (EnsureInstance() && !memValid_ && !FetchMemory()) return addr;
    return UnrealPrevInstruction(mem_.data(), addr);
}

// -- peripherals -------------------------------------------------------------------

PortsState WebApiBackend::GetPorts() {
    // The latches read with the registers (FetchPorts)
    if (EnsureInstance() && !regsValid_) FetchRegs();
    return ports_;
}

Beta128State WebApiBackend::GetBeta128() {
    // GET /state/fdc once per register snapshot: the WD1793's registers, the Beta 128 system register, INTRQ / DRQ
    if (!EnsureInstance() || beta128At_ == snapshots_) return beta128_;
    beta128At_ = snapshots_;
    const HttpReply reply = Api("GET", "/state/fdc");
    Beta128State b;
    if (reply.IsSuccess()) {
        const Json root = Json::Parse(reply.body, nullptr);
        root.Get("available", &b.present);
        int64_t v = 0;
        if (const Json* regs = root.Find("registers")) {
            if (regs->Get("command", &v)) b.cmd = static_cast<uint8_t>(v);
            if (regs->Get("data", &v)) b.data = static_cast<uint8_t>(v);
            if (regs->Get("sector", &v)) b.sector = static_cast<uint8_t>(v);
            if (regs->Get("track", &v)) b.track = static_cast<uint8_t>(v);
            if (regs->Get("status", &v)) b.statusRaw = b.statusRead = static_cast<uint8_t>(v);
        }
        if (root.Get("beta128_register", &v)) b.system = static_cast<uint8_t>(v);
        if (const Json* signals = root.Find("signals")) {
            bool intrq = false, drq = false;
            signals->Get("intrq", &intrq);
            signals->Get("drq", &drq);
            b.rqs = static_cast<uint8_t>((intrq ? 0x80 : 0) | (drq ? 0x40 : 0));
        }
        int64_t selected = 0;
        root.Get("selected_drive", &selected);
        const Json* drives = root.Find("drives");
        if (drives != nullptr && drives->IsArray() && selected >= 0 && static_cast<size_t>(selected) < drives->Size() &&
            drives->At(static_cast<size_t>(selected)).Get("track", &v))
            b.headTrack = static_cast<uint8_t>(v);
    }
    beta128_ = b;
    return beta128_;
}

AyState WebApiBackend::GetAy() {
    // GET /state/audio/ay (the chips, the one #FFFD selects, each one's latched register), then each chip's registers
    if (!EnsureInstance() || ayAt_ == snapshots_) return ay_;
    ayAt_ = snapshots_;
    const HttpReply reply = Api("GET", "/state/audio/ay");
    if (!reply.IsSuccess()) return ay_;
    const Json root = Json::Parse(reply.body, nullptr);
    bool available = false, turbo = false;
    root.Get("available", &available);
    root.Get("turbo_sound", &turbo);
    if (!available) {
        ay_.scheme = AyScheme::None;
        return ay_;
    }
    ay_.scheme = turbo ? AyScheme::TurboSound : AyScheme::Single;
    int64_t active = 0;
    if (!ayChosen_ && root.Get("active_chip", &active)) ay_.activeChip = static_cast<int>(active & 1);
    const int chips = turbo ? 2 : 1;
    for (int chip = 0; chip < chips; ++chip) {
        const HttpReply one = Api("GET", "/state/audio/ay/" + std::to_string(chip));
        if (!one.IsSuccess()) continue;
        const Json c = Json::Parse(one.body, nullptr);
        int64_t latched = 0;
        if (c.Get("latched_register", &latched)) ay_.latchedReg[static_cast<size_t>(chip)] = static_cast<int>(latched & 0x0F);
        // "R0 - Channel A - fine tune" ... "R17 - I/O Port B data store": the register numbers are octal
        const Json* regs = c.Find("registers");
        if (regs == nullptr || !regs->IsObject()) continue;
        for (const auto& [name, value] : regs->Members()) {
            const size_t r = name.find('R');
            int64_t byte = 0;
            if (r == std::string::npos || !value.AsInt(&byte)) continue;
            const unsigned number = static_cast<unsigned>(std::strtoul(name.c_str() + r + 1, nullptr, 8));
            if (number < 16) ay_.regs[static_cast<size_t>(chip)][number] = static_cast<uint8_t>(byte);
        }
    }
    return ay_;
}

void WebApiBackend::SelectAyChip(int chip) {
    // The panel's chip (Alt+Y); the machine's own selection stays as the program set it
    ay_.activeChip = chip & 1;
    ayChosen_ = true;
}

std::vector<uint8_t> WebApiBackend::ReadCmos() {
    // GET /rtc/cells: the machine's clock chip (the ZX-Evo AVR, a Gluk / Mr Gluk, the Profi's); zeros without one
    std::vector<uint8_t> cells(256, 0);
    if (!EnsureInstance()) return cells;
    const HttpReply reply = Api("GET", "/rtc/cells?start=0&count=256");
    std::vector<uint8_t> bytes;
    if (reply.IsSuccess() && ByteArray(Json::Parse(reply.body, nullptr).Find("bytes"), &bytes))
        std::copy_n(bytes.begin(), std::min(bytes.size(), cells.size()), cells.begin());
    return cells;
}

std::vector<uint8_t> WebApiBackend::ReadNvram() {
    // The ZX-Evo AVR's EEPROM, the region "eeprom" (4 KiB): its first 2048 bytes, the editor's size; zeros elsewhere
    std::vector<uint8_t> cells(2048, 0);
    if (!EnsureInstance()) return cells;
    const HttpReply reply = Api("GET", "/memory/region/eeprom?offset=0&length=2048&format=data");
    std::vector<uint8_t> bytes;
    if (reply.IsSuccess() && ByteArray(Json::Parse(reply.body, nullptr).Find("data"), &bytes))
        std::copy_n(bytes.begin(), std::min(bytes.size(), cells.size()), cells.begin());
    return cells;
}

// The ATM's palette has no WebAPI region: the editor opens empty
std::vector<uint8_t> WebApiBackend::ReadCompPal() { return std::vector<uint8_t>(64, 0); }

DiskTrackData WebApiBackend::ReadDiskTrack(int drive, int track) {
    // Track = cylinder * 2 + side, as UnrealSpeccy counts: the raw MFM bytes and each sector's data field
    DiskTrackData d;
    if (!EnsureInstance()) return d;
    const std::string where = "/disk/" + std::to_string(drive) + "/track/" + std::to_string(track / 2) + "/" +
                              std::to_string(track & 1);
    const HttpReply raw = Api("GET", where + "/raw");
    if (!raw.IsSuccess()) return d;  // no disk: "track not found"
    std::string text;
    if (!Json::Parse(raw.body, nullptr).Get("raw_base64", &text) || !Base64Decode(text, &d.physical)) return d;
    d.present = true;
    const HttpReply list = Api("GET", where);
    if (!list.IsSuccess()) return d;
    const Json root = Json::Parse(list.body, nullptr);
    const Json* sectors = root.Find("sectors");
    if (sectors == nullptr || !sectors->IsArray()) return d;
    for (const Json& s : sectors->Items()) {
        int64_t number = 0;
        bool hasData = false;
        s.Get("id_sector", &number);
        s.Get("has_data", &hasData);
        DiskSectorData sector;
        sector.id = static_cast<uint8_t>(number);
        if (hasData) {
            const HttpReply one = Api("GET", "/disk/" + std::to_string(drive) + "/sector/" + std::to_string(track / 2) +
                                                 "/" + std::to_string(track & 1) + "/" + std::to_string(number));
            std::string data;
            if (one.IsSuccess() && Json::Parse(one.body, nullptr).Get("data_base64", &data)) Base64Decode(data, &sector.data);
        }
        sector.length = static_cast<uint16_t>(sector.data.size());
        d.sectors.push_back(std::move(sector));
    }
    return d;
}

void WebApiBackend::WriteDiskByte(int drive, int track, bool logical, int offset, uint8_t value) {
    // The logical view writes the sector's data field (PUT, the CRC kept right); the raw track has no write route
    if (!logical || !EnsureInstance()) return;
    const DiskTrackData d = ReadDiskTrack(drive, track);
    int at = offset;
    for (const DiskSectorData& s : d.sectors) {
        if (at < static_cast<int>(s.data.size())) {
            char body[64];
            std::snprintf(body, sizeof body, "{\"offset\":%d,\"data\":[%u]}", at, static_cast<unsigned>(value));
            Api("PUT", "/disk/" + std::to_string(drive) + "/sector/" + std::to_string(track / 2) + "/" +
                           std::to_string(track & 1) + "/" + std::to_string(s.id),
                body);
            return;
        }
        at -= static_cast<int>(s.data.size());
    }
}

ScreenPixels WebApiBackend::GetScreenPreview(int mode) {
    // The ZX screen from its page (5, or 7 when #7FFD bit 3 shows the shadow one) and the border of #FE, painted as
    // the mock paints it: a 320 x 224 frame cropped to 296 x 208; mode 2 only down to the beam's line
    ScreenPixels px;
    if (!EnsureInstance()) return px;
    if (!regsValid_) FetchRegs();
    const HttpReply reply =
        Api("GET", std::string("/memory/page/ram/") + ((ports_.p7ffd & 0x08) ? "7" : "5") + "?length=6912");
    std::vector<uint8_t> screen;
    if (!reply.IsSuccess() || !ByteArray(Json::Parse(reply.body, nullptr).Find("data"), &screen) || screen.size() < 6912)
        return px;
    const int fh = 224, sx = 32, sy = 16;
    const uint32_t frameT = frameT_ != 0 ? frameT_ : 70908;
    const int beamLine = mode == 2 ? static_cast<int>(static_cast<uint64_t>(absT_ % frameT) * fh / frameT) : fh;
    const bool flashPhase = ((absT_ / frameT) & 16) != 0;  // FLASH swaps every 16 frames
    px.width = 296;
    px.height = 208;
    px.index.assign(static_cast<size_t>(px.width) * px.height, static_cast<uint8_t>(ports_.fe & 0x07));
    for (int y = sy; y < sy + 192 && y < beamLine; ++y) {
        const int ly = y - sy;
        for (int x = sx; x < sx + 256; ++x) {
            const int lx = x - sx;
            const size_t pixel = static_cast<size_t>(((ly & 0xC0) << 5) | ((ly & 0x07) << 8) | ((ly & 0x38) << 2) | (lx >> 3));
            const uint8_t attr = screen[static_cast<size_t>(0x1800 + ((ly >> 3) << 5) + (lx >> 3))];
            const uint8_t bright = (attr & 0x40) ? 8 : 0;
            const bool on = ((screen[pixel] >> (7 - (lx & 7))) & 1) != 0;
            const bool ink = on != ((attr & 0x80) != 0 && flashPhase);
            px.index[static_cast<size_t>(y - 8) * px.width + static_cast<size_t>(x - 12)] =
                ink ? static_cast<uint8_t>(bright | (attr & 0x07)) : static_cast<uint8_t>(bright | ((attr >> 3) & 0x07));
        }
    }
    return px;
}

// -- execution control --------------------------------------------------------------

std::optional<IDebuggerBackend::StopReason> WebApiBackend::Step(int) {
    if (!EnsureInstance() || !EnsurePaused()) return std::nullopt;
    if (regsValid_) prevRegs_ = regs_;
    const HttpReply reply = Api("POST", "/step");
    if (!reply.IsSuccess()) return std::nullopt;  // lastError_ tells why
    memValid_ = false;
    FetchRegs();
    FetchMemory();
    // The reply's stop: "breakpoint" when the step hit one (a memory, port or conditional one too); an execution
    // breakpoint at the new PC stops the next run there
    std::string why;
    if (const Json* stop = Json::Parse(reply.body, nullptr).Find("stop")) stop->Get("reason", &why);
    if (why == "breakpoint" || (bpBits_[regs_.pc] & 0x01) != 0) return StopReason::Breakpoint;
    return std::nullopt;
}

IDebuggerBackend::StopReason WebApiBackend::RunUntilBreak(int, const StopRequest& request) {
    if (!EnsureInstance() || !EnsurePaused()) return StopReason::MaxInstructions;
    if (!regsValid_) FetchRegs();  // WriteReg & co. leave the cache stale
    if (regsValid_) prevRegs_ = regs_;  // §9.5 (b): snapshot at leave

    // Synchronous batched POST /steps: each reply carries the landing PC/SP, checked against the local stop
    // conditions at every batch boundary; a batch ends early at a breakpoint of the emulator's (a memory, port or
    // conditional one: its "stop" reason).
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
        std::string why;
        if (const Json* stop = root.Find("stop")) stop->Get("reason", &why);
        if (why == "breakpoint") {
            reason = StopReason::Breakpoint;
            break;
        }
        if (count >= request.maxInstructions) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
    }

    memValid_ = false;
    regsValid_ = false;
    FetchRegs();
    FetchMemory();
    return reason;
}

uint64_t WebApiBackend::GetAbsoluteT(int) {
    if (EnsureInstance() && !regsValid_) FetchRegs();
    return absT_;
}

void WebApiBackend::SetDebugMark(int, uint64_t absT) { markT_ = absT; }

// -- register / port writes -----------------------------------------------------------

void WebApiBackend::WriteReg(int, RegField field, unsigned value) {
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
    case RegField::Im: name = "IM"; break;
    case RegField::Iff1: name = "IFF1"; break;
    case RegField::Iff2: name = "IFF2"; break;
    }
    Api("PUT", std::string("/registers/") + name,
        "{\"value\":" + std::to_string(value & 0xFFFF) + "}");
    regsValid_ = false;
}

void WebApiBackend::Out(uint16_t port, uint8_t value) {
    // POST /ports/out: the machine's decoder takes the write with all its effects (paging, border, a TS-Conf
    // register), recorded as a tool edit; the registers, pages, latches and memory are read again
    if (!EnsureInstance()) return;
    char body[64];
    std::snprintf(body, sizeof body, "{\"port\":\"0x%04X\",\"value\":\"0x%02X\"}", static_cast<unsigned>(port),
                  static_cast<unsigned>(value));
    if (!Api("POST", "/ports/out", body).IsSuccess()) return;  // lastError_ tells why
    regsValid_ = false;
    memValid_ = false;
}

void WebApiBackend::Set7ffd(uint8_t value) { Out(0x7FFD, value); }

void WebApiBackend::SetExtPort(uint8_t value) {
    // The model's extended port as the port map names it (#1FFD, #DFFD, #77 ...); none: nothing to write
    if (EnsureInstance() && !regsValid_) FetchRegs();
    if (ports_.hasExtPort) Out(ports_.extPort, value);
}

// -- breakpoints / conditions -----------------------------------------------------------

uint8_t WebApiBackend::BpBitsAt(uint16_t addr) { return bpBits_[addr]; }

void WebApiBackend::SetBpBits(uint16_t addr, uint8_t bits) {
    // local bits are the truth at /steps boundaries (RunUntilBreak); exec/read/write bits also mirror to the server,
    // where they stop a Continue / RunTo (debug mode is on from EnsureInstance)
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

bool WebApiBackend::HasAnyBreakpoints() {
    for (const uint8_t bits : bpBits_) {
        if (bits & 0x07) return true;
    }
    return !conditions_.empty();
}

ExprCompileResult WebApiBackend::AddCondition(int, const std::string& text) {
    // The emulator evaluates the condition at every instruction (an execution breakpoint over 0000-FFFF), or at every
    // read or write when it asks for RD / WR (its trigger refuses the other kinds: the next one is tried). Its language
    // is a superset of the shared compiler's (#hex, PG0-PG3, EFF7 ...): a text both refuse answers in UnrealSpeccy's
    // words
    ExprCompileResult res;
    CompiledExpr expr;
    std::string localError;
    const bool compiles = CompiledExpr::Compile(text, &expr, &localError);
    if (!EnsureInstance()) {
        res.error = compiles ? lastError_ : localError;
        return res;
    }
    for (const char* type : {"exec", "read", "write"}) {
        const std::string body = std::string("{\"type\":\"") + type +
                                 "\",\"address\":0,\"address_end\":65535,\"condition\":\"" + JsonEscape(text) +
                                 "\",\"group\":\"" + kConditionGroup + "\",\"note\":\"condition\"}";
        const HttpReply added = Api("POST", "/breakpoints", body);
        int64_t id = 0;
        if (added.IsSuccess() && Json::Parse(added.body, nullptr).Get("id", &id)) {
            conditions_.push_back(text);
            conditionIds_.push_back(static_cast<int>(id));
            res.ok = true;
            return res;
        }
        res.error = lastError_;
        if (lastError_.find("not supplied") == std::string::npos) break;  // a refusal of another kind
    }
    if (!compiles && res.error.find("not supplied") == std::string::npos) res.error = localError;
    return res;
}

void WebApiBackend::DeleteCondition(int, int index) {
    if (index < 0 || static_cast<size_t>(index) >= conditions_.size()) return;
    if (EnsureInstance()) Api("DELETE", "/breakpoints/" + std::to_string(conditionIds_[static_cast<size_t>(index)]));
    conditions_.erase(conditions_.begin() + index);
    conditionIds_.erase(conditionIds_.begin() + index);
}

std::vector<std::string> WebApiBackend::GetConditions(int) { return conditions_; }

int WebApiBackend::ConditionCount(int) { return static_cast<int>(conditions_.size()); }

// -- assemble / find --------------------------------------------------------------------

AssembleResult WebApiBackend::Assemble(uint16_t addr, const std::string& text) {
    // shared subset assembler; a success is pushed through the same
    // PUT /memory/{addr} path the memory editor uses
    const AssembleResult local = AssembleSubset(addr, text);
    if (!local.ok) return local;
    WriteMemory(0, addr, local.bytes);
    return local;
}

FindResult WebApiBackend::FindBytes(EditorSpace space, uint16_t start, const BytePattern& pattern,
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

std::optional<std::string> WebApiBackend::LabelFor(uint16_t addr) {
    for (const auto& l : labels_) {
        if (l.first == addr) return l.second;
    }
    return std::nullopt;
}

std::vector<std::pair<uint16_t, std::string>> WebApiBackend::LabelsInCurrentMap() {
    EnsureInstance();
    return labels_;
}

void WebApiBackend::LoadUserLabels(const std::string& path, std::string* error, int* loadedCount) {
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

std::vector<IDebuggerBackend::ImportItem> WebApiBackend::ScanLabelImports() {
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

void WebApiBackend::ImportLabels(int) {
    // disabled items only: nothing to import
}

// -- ripper (§6.3) -------------------------------------------------------------------------

void WebApiBackend::ArmRipper(bool traceReads, bool traceWrites) {
    // The emulator's memory profiler counts every access from now on; the collection reads its counters
    ripTraceReads_ = traceReads;
    ripTraceWrites_ = traceWrites;
    if (!EnsureInstance()) return;
    Api("POST", "/profiler/memory/stop");  // a fresh count
    ripperArmed_ = Api("POST", "/profiler/memory/start").IsSuccess();
}

bool WebApiBackend::RipperArmed() { return ripperArmed_; }

std::vector<uint8_t> WebApiBackend::CollectRipper(uint8_t unrefByte) {
    // A byte read or written (as armed) since ArmRipper keeps its value, the others read unrefByte
    std::vector<uint8_t> out(0x10000, unrefByte);
    if (!ripperArmed_ || !EnsureInstance()) {
        ripperArmed_ = false;
        return out;
    }
    ripperArmed_ = false;
    const HttpReply reply = Api("GET", "/profiler/memory/counters?mode=z80");
    Api("POST", "/profiler/memory/stop");
    if (!reply.IsSuccess() || (!memValid_ && !FetchMemory())) return out;
    const Json root = Json::Parse(reply.body, nullptr);
    const Json* counters = root.Find("counters");
    if (counters == nullptr) return out;
    const Json* reads = counters->Find("reads");
    const Json* writes = counters->Find("writes");
    for (size_t a = 0; a < out.size(); ++a) {
        int64_t r = 0, w = 0;
        if (ripTraceReads_ && reads != nullptr && a < reads->Size()) reads->At(a).AsInt(&r);
        if (ripTraceWrites_ && writes != nullptr && a < writes->Size()) writes->At(a).AsInt(&w);
        if (r != 0 || w != 0) out[a] = mem_[a];
    }
    return out;
}

}  // namespace dbg
