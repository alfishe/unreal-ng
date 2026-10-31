#include "debugger/analyzers/analysis/l3experiment.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/analyzers/analysis/analysisinstance.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"
#include "emulator/state/statenodejson.h"
#include "loaders/snapshot/machinestatetransfer.h"

namespace analysis
{

namespace
{

std::string Hex4(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value & 0xFFFF);
    return text;
}

std::string Hex2(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%02X", value & 0xFF);
    return text;
}

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// A number: an integer node, or text ($hex, 0xhex, hexh, #hex, decimal)
bool NumberOf(const StateNode& node, uint64_t limit, uint64_t& value)
{
    if (node.kind == StateNode::Kind::Int)
    {
        if (node.i < 0 || static_cast<uint64_t>(node.i) > limit)
            return false;
        value = static_cast<uint64_t>(node.i);
        return true;
    }
    if (node.kind != StateNode::Kind::String)
        return false;
    uint16_t parsed = 0;
    if (limit <= 0xFFFF)
    {
        if (!ParseAddress(node.s, parsed) || parsed > limit)
            return false;
        value = parsed;
        return true;
    }
    if (node.s.empty() || !std::all_of(node.s.begin(), node.s.end(), [](unsigned char c) { return std::isdigit(c); }))
        return false;
    value = std::strtoull(node.s.c_str(), nullptr, 10);
    return value <= limit;
}

bool TextNumber(const std::string& text, uint64_t limit, uint64_t& value)
{
    return NumberOf(StateNode(text), limit, value);
}

bool ParseExpect(const std::string& key, const StateNode& node, const std::vector<ExperimentWatch>& watches,
                 ExperimentExpect& out, std::string& error)
{
    out.key = key;
    std::string text;
    if (node.kind == StateNode::Kind::Int)
        text = std::to_string(node.i);
    else if (node.kind == StateNode::Kind::String)
        text = Lower(node.s);
    else
    {
        error = "expect." + key + ": a string or a number";
        return false;
    }
    out.text = text;
    const auto numeric = [&](uint64_t limit) {
        std::string rest = text;
        out.op = ExperimentExpect::Op::Equal;
        if (rest.rfind(">=", 0) == 0)
        {
            out.op = ExperimentExpect::Op::AtLeast;
            rest = rest.substr(2);
        }
        else if (rest.rfind("<=", 0) == 0)
        {
            out.op = ExperimentExpect::Op::AtMost;
            rest = rest.substr(2);
        }
        else if (rest.rfind("!=", 0) == 0)
        {
            out.op = ExperimentExpect::Op::NotEqual;
            rest = rest.substr(2);
        }
        else if (rest.rfind("==", 0) == 0)
            rest = rest.substr(2);
        else if (rest.rfind(">", 0) == 0)
        {
            uint64_t v = 0;
            if (!TextNumber(rest.substr(1), limit, v))
                return false;
            out.op = ExperimentExpect::Op::AtLeast;
            out.value = v + 1;
            return true;
        }
        return TextNumber(rest, limit, out.value);
    };

    if (key == "screen")
    {
        out.subject = ExperimentExpect::Subject::Screen;
        if (text == "changed")
            out.op = ExperimentExpect::Op::Changed;
        else if (text == "unchanged")
            out.op = ExperimentExpect::Op::Unchanged;
        else
        {
            error = "expect.screen: changed or unchanged";
            return false;
        }
        return true;
    }
    if (key == "new_code")
    {
        out.subject = ExperimentExpect::Subject::NewCode;
        if (!numeric(1000000))
        {
            error = "expect.new_code: >0, 0 or >=N";
            return false;
        }
        return true;
    }
    if (key == "branch")
    {
        out.subject = ExperimentExpect::Subject::Branch;
        if (text != "flipped")
        {
            error = "expect.branch: flipped";
            return false;
        }
        return true;
    }
    std::string addressText;
    if (key.rfind("mem:", 0) == 0)
        addressText = key.substr(4);
    else if (std::none_of(watches.begin(), watches.end(), [&](const ExperimentWatch& w) { return w.name == key; }))
        addressText = key;
    if (!addressText.empty())
    {
        out.subject = ExperimentExpect::Subject::Memory;
        if (!ParseAddress(addressText, out.address))
        {
            error = "expect." + key + ": a watch name, mem:<address>, screen or new_code";
            return false;
        }
        if (text == "changed")
            out.op = ExperimentExpect::Op::Changed;
        else if (text == "unchanged")
            out.op = ExperimentExpect::Op::Unchanged;
        else if (!numeric(0xFF))
        {
            error = "expect." + key + ": a byte value (N, ==N, !=N, >=N, <=N) or changed / unchanged";
            return false;
        }
        return true;
    }
    out.subject = ExperimentExpect::Subject::Watch;
    out.watch = key;
    if (text == "reached")
        out.op = ExperimentExpect::Op::Reached;
    else if (text == "not_reached")
        out.op = ExperimentExpect::Op::NotReached;
    else if (!numeric(1000000000))
    {
        error = "expect." + key + ": reached, not_reached, >=N, <=N or ==N";
        return false;
    }
    return true;
}

bool ParseInput(const StateNode& item, ExperimentInput& in, std::string& error)
{
    if (!item.isObject())
    {
        error = "input: an array of objects";
        return false;
    }
    uint64_t frame = 0;
    if (const StateNode* f = item.find("frame"); f != nullptr && !NumberOf(*f, 100000, frame))
    {
        error = "input.frame: frames after the start, 0 to 100000";
        return false;
    }
    in.frame = static_cast<uint32_t>(frame);
    const auto flag = [&](const char* name) {
        const StateNode* n = item.find(name);
        return n != nullptr && n->kind == StateNode::Kind::Bool && n->b;
    };
    in.press = !flag("up") && !flag("release");
    if (flag("release_all"))
    {
        in.type = ExperimentInput::Type::ReleaseAll;
        in.name = "release_all";
        return true;
    }
    if (const StateNode* k = item.find("key"); k != nullptr && k->kind == StateNode::Kind::String)
    {
        const ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(k->s);
        uint8_t row = 0, bit = 0;
        if (key == ZXKEY_NONE || !Keyboard::MatrixPosition(key, row, bit))
        {
            error = "input.key: a ZX key name (a-z, 0-9, enter, space, caps, sym), not '" + k->s + "'";
            return false;
        }
        in.type = ExperimentInput::Type::Key;
        in.key = static_cast<uint8_t>(key);
        in.name = Lower(k->s);
        return true;
    }
    if (const StateNode* j = item.find("joystick"); j != nullptr && j->kind == StateNode::Kind::String)
    {
        const uint8_t mask = Joystick::MaskFromName(j->s);
        if (mask == 0)
        {
            error = "input.joystick: up, down, left, right or fire, not '" + j->s + "'";
            return false;
        }
        in.type = ExperimentInput::Type::Joystick;
        in.mask = mask;
        in.name = "joystick " + Lower(j->s);
        return true;
    }
    error = "input: each event needs key, joystick or release_all";
    return false;
}

const std::vector<std::string>& SpecFields()
{
    static const std::vector<std::string> fields = {"id",    "finding", "claim", "from",   "poke",
                                                    "regs",  "input",   "frames", "watch", "expect"};
    return fields;
}

bool SetRegister(Z80& z80, const std::string& name, uint16_t value)
{
    const auto byte = static_cast<uint8_t>(value);
    if (name == "a") z80.a = byte;
    else if (name == "f") z80.f = byte;
    else if (name == "b") z80.b = byte;
    else if (name == "c") z80.c = byte;
    else if (name == "d") z80.d = byte;
    else if (name == "e") z80.e = byte;
    else if (name == "h") z80.h = byte;
    else if (name == "l") z80.l = byte;
    else if (name == "af") z80.af = value;
    else if (name == "bc") z80.bc = value;
    else if (name == "de") z80.de = value;
    else if (name == "hl") z80.hl = value;
    else if (name == "ix") z80.ix = value;
    else if (name == "iy") z80.iy = value;
    else if (name == "sp") z80.sp = value;
    else if (name == "pc") z80.pc = value;
    else return false;
    return true;
}

uint64_t ScreenHash(Memory& memory)
{
    uint64_t hash = 1469598103934665603ull;
    for (uint32_t a = 0x4000; a < 0x5B00; ++a)
    {
        hash ^= memory.DirectReadFromZ80Memory(static_cast<uint16_t>(a));
        hash *= 1099511628211ull;
    }
    return hash;
}

/// The base: the recording loaded, seeked to one start frame at a time
struct Base
{
    std::mutex mutex;
    AnalysisInstance instance;
    bool made = false;
    std::string error;
    bool positioned = false;
    ttd::TTDTimePoint at;
    uint64_t first = 0;
    uint64_t end = 0;
    std::atomic<uint32_t> seeks{0};

    bool Make(const TraceBundle& bundle)
    {
        if (made)
            return true;
        if (!error.empty())
            return false;
        if (!instance.Make(bundle, "analysis-experiment-base", true, error))
            return false;
        ttd::TimeTravelController* ttd = instance.Context()->pTimeTravelController;
        if (ttd->GetCheckpointCount() == 0)
        {
            error = "the recording has no keyframe";
            return false;
        }
        first = ttd->GetCheckpoint(0)->time.frame;
        end = ttd->SessionEndPosition().frame;
        made = true;
        return true;
    }

    ttd::TTDTimePoint Target(const ExperimentSpec& spec) const
    {
        if (spec.from == ExperimentSpec::From::End)
            return instance.Context()->pTimeTravelController->SessionEndPosition();
        ttd::TTDTimePoint point;
        point.frame = spec.from == ExperimentSpec::From::Start ? first : std::min(end, first + spec.fromFrame);
        return point;
    }

    /// Under mutex: seek, then copy the state into @p target
    bool CopyInto(const ExperimentSpec& spec, AnalysisInstance& target, uint64_t& frame, std::string& why)
    {
        const ttd::TTDTimePoint point = Target(spec);
        if (!positioned || point != at)
        {
            ttd::TimeTravelController* ttd = instance.Context()->pTimeTravelController;
            if (!ttd->SeekTo(point, nullptr))
            {
                why = "the base could not seek to frame " + std::to_string(point.frame);
                positioned = false;
                return false;
            }
            positioned = true;
            at = point;
            ++seeks;
        }
        frame = point.frame;
        const MachineStateTransfer::Report report = MachineStateTransfer::Apply(*instance.Context(), *target.Context());
        if (!report.ok)
        {
            why = "the state did not copy into the experiment's machine: " + report.reason;
            return false;
        }
        return true;
    }
};

/// One experiment on a worker's machine
void RunOne(const ExperimentSpec& spec, AnalysisInstance& machine, const L1Result* l1, ExperimentResult& result)
{
    EmulatorContext* context = machine.Context();
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;
    Keyboard* keyboard = context->pKeyboard;
    Joystick* joystick = context->pJoystick;

    for (const auto& [address, value] : spec.pokes)
        memory->DirectWriteToZ80Memory(address, value);
    for (const auto& [name, value] : spec.regs)
        SetRegister(*z80, name, value);

    std::vector<uint8_t> before;
    for (const ExperimentExpect& e : spec.expect)
        if (e.subject == ExperimentExpect::Subject::Memory)
            before.push_back(memory->DirectReadFromZ80Memory(e.address));
    const uint64_t screenBefore = ScreenHash(*memory);

    std::vector<uint64_t> hits(spec.watch.size(), 0);
    std::vector<int32_t> firstPc(spec.watch.size(), -1);
    std::vector<uint8_t> seen(0x10000, 0);
    std::vector<uint16_t> fresh;
    uint16_t lastPc = 0;
    bool pinsOn = !spec.pins.empty();
    bool branchPending = false;
    uint64_t branchInterrupts = 0;
    bool decided = false;
    bool taken = false;
    uint32_t branchArrivals = 0;

    z80->m1TraceHook = [&](uint16_t pc) {
        if (branchPending)
        {
            branchPending = false;
            if (z80->interruptsAccepted == branchInterrupts && !decided)
            {
                // The first arrival may test flags computed before the start (the state is taken at a frame
                // boundary, possibly between the read and the branch): it counts only when it already went the new
                // way; otherwise the next arrival, after a pass under the poke or the key, decides
                taken = pc != static_cast<uint16_t>(spec.branchPc + spec.branchLength);
                decided = taken == spec.wantTaken || ++branchArrivals > 1;
                if (decided)
                    pinsOn = false;
            }
        }
        if (pinsOn)
            for (const ExperimentPin& pin : spec.pins)
                memory->DirectWriteToZ80Memory(pin.address, pin.value);
        if (spec.branch && pc == spec.branchPc && !decided)
        {
            branchPending = true;
            branchInterrupts = z80->interruptsAccepted;
        }
        lastPc = pc;
        for (size_t i = 0; i < spec.watch.size(); ++i)
            if (spec.watch[i].what == 'X' && spec.watch[i].address == pc)
            {
                if (hits[i]++ == 0)
                    firstPc[i] = pc;
            }
        if (seen[pc] == 0)
        {
            seen[pc] = 1;
            const uint16_t page = memory->GetPageForBank(static_cast<uint8_t>(pc >> 14));
            if (l1 == nullptr || l1->InstructionAt(MakeKey(page, pc)) == nullptr)
                fresh.push_back(pc);
        }
    };
    const bool busWatch = std::any_of(spec.watch.begin(), spec.watch.end(),
                                      [](const ExperimentWatch& w) { return w.what != 'X'; });
    if (busWatch)
        z80->busTraceHook = [&](char type, uint16_t address, uint8_t) {
            if (type != 'W' && type != 'R')
                return;
            for (size_t i = 0; i < spec.watch.size(); ++i)
                if (spec.watch[i].what == type && spec.watch[i].address == address)
                {
                    if (hits[i]++ == 0)
                        firstPc[i] = lastPc;
                }
        };

    std::vector<uint8_t> heldKeys;
    uint8_t heldJoystick = 0;
    const auto releaseAll = [&] {
        for (const uint8_t key : heldKeys)
            keyboard->ReleaseKey(static_cast<ZXKeysEnum>(key));
        heldKeys.clear();
        if (joystick != nullptr && heldJoystick != 0)
            joystick->Release(heldJoystick);
        heldJoystick = 0;
    };
    bool inputReleased = false;
    bool stalled = false;
    uint32_t frame = 0;
    for (; frame < spec.frames; ++frame)
    {
        for (const ExperimentInput& in : spec.input)
        {
            if (in.frame != frame)
                continue;
            if (in.type == ExperimentInput::Type::ReleaseAll)
                releaseAll();
            else if (in.type == ExperimentInput::Type::Key && keyboard != nullptr)
            {
                if (in.press)
                {
                    keyboard->PressKey(static_cast<ZXKeysEnum>(in.key));
                    heldKeys.push_back(in.key);
                }
                else
                {
                    keyboard->ReleaseKey(static_cast<ZXKeysEnum>(in.key));
                    heldKeys.erase(std::remove(heldKeys.begin(), heldKeys.end(), in.key), heldKeys.end());
                }
            }
            else if (in.type == ExperimentInput::Type::Joystick && joystick != nullptr)
            {
                if (in.press)
                {
                    joystick->Press(in.mask);
                    heldJoystick |= in.mask;
                }
                else
                {
                    joystick->Release(in.mask);
                    heldJoystick = static_cast<uint8_t>(heldJoystick & ~in.mask);
                }
            }
        }
        if (!machine.RunFrames(1))
        {
            stalled = true;
            break;
        }
        if (spec.branch && decided)
        {
            if (spec.releaseInputWhenDecided && !inputReleased)
            {
                releaseAll();
                inputReleased = true;
            }
            if (taken != spec.wantTaken)
            {
                ++frame;
                break;  // went the old way: the attempt failed, no need to run on
            }
        }
    }
    z80->m1TraceHook = nullptr;
    z80->busTraceHook = nullptr;
    releaseAll();
    result.framesRun = frame;
    if (stalled)
    {
        result.verdict = "error";
        result.error = "the experiment's machine stopped running at frame " + std::to_string(frame);
        return;
    }

    result.branchDecided = decided;
    result.branchTaken = taken;
    std::sort(fresh.begin(), fresh.end());
    result.newCode = static_cast<uint32_t>(fresh.size());
    result.newPcs.assign(fresh.begin(), fresh.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(fresh.size(), 4096)));
    for (const uint16_t pc : fresh)
    {
        if (!result.newRanges.empty() && pc - result.newRanges.back().second <= 4)
            result.newRanges.back().second = pc;
        else if (result.newRanges.size() < 16)
            result.newRanges.emplace_back(pc, pc);
    }
    for (size_t i = 0; i < spec.watch.size(); ++i)
    {
        result.hits.emplace_back(spec.watch[i].name, hits[i]);
        result.firstPc.emplace_back(spec.watch[i].name, static_cast<uint16_t>(firstPc[i] < 0 ? 0 : firstPc[i]));
    }

    bool all = true;
    size_t memoryIndex = 0;
    for (const ExperimentExpect& e : spec.expect)
    {
        ExperimentCheck check;
        check.key = e.key;
        check.expect = e.text;
        uint64_t actual = 0;
        bool numeric = true;
        switch (e.subject)
        {
            case ExperimentExpect::Subject::Watch:
            {
                for (size_t i = 0; i < spec.watch.size(); ++i)
                    if (spec.watch[i].name == e.watch)
                        actual = hits[i];
                check.actual = std::to_string(actual) + (actual == 1 ? " hit" : " hits");
                break;
            }
            case ExperimentExpect::Subject::Memory:
            {
                actual = memory->DirectReadFromZ80Memory(e.address);
                const uint8_t was = before[memoryIndex++];
                check.actual = Hex2(static_cast<uint32_t>(actual)) + " (was " + Hex2(was) + ")";
                if (e.op == ExperimentExpect::Op::Changed || e.op == ExperimentExpect::Op::Unchanged)
                {
                    numeric = false;
                    check.pass = (actual != was) == (e.op == ExperimentExpect::Op::Changed);
                }
                break;
            }
            case ExperimentExpect::Subject::Screen:
            {
                numeric = false;
                const bool changed = ScreenHash(*memory) != screenBefore;
                check.actual = changed ? "changed" : "unchanged";
                check.pass = changed == (e.op == ExperimentExpect::Op::Changed);
                break;
            }
            case ExperimentExpect::Subject::NewCode:
                actual = result.newCode;
                check.actual = std::to_string(actual) + " new instructions";
                break;
            case ExperimentExpect::Subject::Branch:
                numeric = false;
                check.actual = decided ? (taken ? "taken" : "not taken")
                                       : branchArrivals > 0 ? "not taken (reached once)" : "not reached";
                check.pass = decided && taken == spec.wantTaken;
                break;
        }
        if (numeric)
        {
            switch (e.op)
            {
                case ExperimentExpect::Op::Reached:
                    check.pass = actual > 0;
                    break;
                case ExperimentExpect::Op::NotReached:
                    check.pass = actual == 0;
                    break;
                case ExperimentExpect::Op::Equal:
                    check.pass = actual == e.value;
                    break;
                case ExperimentExpect::Op::NotEqual:
                    check.pass = actual != e.value;
                    break;
                case ExperimentExpect::Op::AtLeast:
                    check.pass = actual >= e.value;
                    break;
                case ExperimentExpect::Op::AtMost:
                    check.pass = actual <= e.value;
                    break;
                case ExperimentExpect::Op::Changed:
                case ExperimentExpect::Op::Unchanged:
                    break;
            }
        }
        all = all && check.pass;
        result.checks.push_back(std::move(check));
    }
    result.verdict = all ? "observed" : "refuted";
}

}  // namespace

bool ParseExperiment(const StateNode& node, ExperimentSpec& spec, std::string& error)
{
    spec = ExperimentSpec{};
    if (!node.isObject())
    {
        error = "an experiment is a JSON object";
        return false;
    }
    for (const auto& [name, value] : node.members)
    {
        (void)value;
        const std::vector<std::string>& fields = SpecFields();
        if (std::find(fields.begin(), fields.end(), name) == fields.end())
        {
            error = "unknown experiment field '" + name + "' (id, finding, claim, from, poke, regs, input, frames, "
                    "watch, expect)";
            return false;
        }
    }
    const auto text = [&](const char* name, std::string& out) {
        if (const StateNode* n = node.find(name); n != nullptr && n->kind == StateNode::Kind::String)
            out = n->s;
    };
    text("id", spec.id);
    text("finding", spec.finding);
    text("claim", spec.claim);
    if (const StateNode* from = node.find("from"); from != nullptr)
    {
        std::string value = from->kind == StateNode::Kind::String ? Lower(from->s) : std::string();
        uint64_t frame = 0;
        if (value == "start")
            spec.from = ExperimentSpec::From::Start;
        else if (value == "end")
            spec.from = ExperimentSpec::From::End;
        else if ((value.rfind("frame:", 0) == 0 && TextNumber(value.substr(6), 100000000, frame)) ||
                 (from->kind == StateNode::Kind::Int && NumberOf(*from, 100000000, frame)) ||
                 (!value.empty() && TextNumber(value, 100000000, frame)))
        {
            spec.from = ExperimentSpec::From::Frame;
            spec.fromFrame = frame;
        }
        else
        {
            error = "from: start, end, or frames after the first keyframe (a number or frame:N)";
            return false;
        }
    }
    if (const StateNode* frames = node.find("frames"); frames != nullptr)
    {
        uint64_t n = 0;
        if (!NumberOf(*frames, 5000, n) || n == 0)
        {
            error = "frames: 1 to 5000";
            return false;
        }
        spec.frames = static_cast<uint32_t>(n);
    }
    if (const StateNode* poke = node.find("poke"); poke != nullptr)
    {
        if (!poke->isObject())
        {
            error = "poke: an object of address: value ({\"$8100\": 5})";
            return false;
        }
        for (const auto& [address, value] : poke->members)
        {
            uint16_t a = 0;
            uint64_t v = 0;
            if (!ParseAddress(address, a) || !NumberOf(value, 0xFF, v))
            {
                error = "poke." + address + ": an address and a byte value";
                return false;
            }
            spec.pokes.emplace_back(a, static_cast<uint8_t>(v));
        }
    }
    if (const StateNode* regs = node.find("regs"); regs != nullptr)
    {
        if (!regs->isObject())
        {
            error = "regs: an object of register: value ({\"a\": 5, \"pc\": \"$8000\"})";
            return false;
        }
        for (const auto& [name, value] : regs->members)
        {
            const std::string reg = Lower(name);
            static const std::vector<std::string> names = {"a",  "f",  "b",  "c",  "d",  "e",  "h",  "l", "af",
                                                           "bc", "de", "hl", "ix", "iy", "sp", "pc"};
            uint64_t v = 0;
            if (std::find(names.begin(), names.end(), reg) == names.end() ||
                !NumberOf(value, reg.size() == 1 ? 0xFF : 0xFFFF, v))
            {
                error = "regs." + name + ": a register (a f b c d e h l af bc de hl ix iy sp pc) and its value";
                return false;
            }
            spec.regs.emplace_back(reg, static_cast<uint16_t>(v));
        }
    }
    if (const StateNode* input = node.find("input"); input != nullptr)
    {
        if (!input->isArray())
        {
            error = "input: an array of events";
            return false;
        }
        for (const StateNode& item : input->items)
        {
            ExperimentInput in;
            if (!ParseInput(item, in, error))
                return false;
            spec.input.push_back(in);
        }
    }
    if (const StateNode* watch = node.find("watch"); watch != nullptr)
    {
        if (!watch->isArray())
        {
            error = "watch: an array of {pc|write|read: address, as: name}";
            return false;
        }
        for (const StateNode& item : watch->items)
        {
            ExperimentWatch w;
            const StateNode* address = nullptr;
            if ((address = item.find("pc")) != nullptr)
                w.what = 'X';
            else if ((address = item.find("write")) != nullptr)
                w.what = 'W';
            else if ((address = item.find("read")) != nullptr)
                w.what = 'R';
            uint64_t a = 0;
            if (address == nullptr || !NumberOf(*address, 0xFFFF, a))
            {
                error = "watch: each entry needs pc, write or read with an address";
                return false;
            }
            w.address = static_cast<uint16_t>(a);
            if (const StateNode* as = item.find("as"); as != nullptr && as->kind == StateNode::Kind::String)
                w.name = as->s;
            if (w.name.empty())
                w.name = std::string(w.what == 'X' ? "pc" : w.what == 'W' ? "write" : "read") + ":" + Hex4(w.address);
            spec.watch.push_back(w);
        }
    }
    const StateNode* expect = node.find("expect");
    if (expect == nullptr || !expect->isObject() || expect->members.empty())
    {
        error = "expect: an object of checks ({\"rare\": \"reached\", \"mem:$8100\": \"!=5\", \"screen\": \"changed\"})";
        return false;
    }
    for (const auto& [key, value] : expect->members)
    {
        ExperimentExpect e;
        if (!ParseExpect(key, value, spec.watch, e, error))
            return false;
        spec.expect.push_back(e);
    }
    return true;
}

bool ParseExperiments(const std::string& json, std::vector<ExperimentSpec>& specs, std::string& error)
{
    StateNode node;
    std::string why;
    if (!StateNodeFromJsonText(json, node, &why))
    {
        error = "the experiment is not JSON: " + why;
        return false;
    }
    std::vector<const StateNode*> items;
    if (node.isArray())
        for (const StateNode& item : node.items)
            items.push_back(&item);
    else
        items.push_back(&node);
    if (items.empty() || items.size() > 256)
    {
        error = "1 to 256 experiments a call";
        return false;
    }
    specs.clear();
    for (size_t i = 0; i < items.size(); ++i)
    {
        ExperimentSpec spec;
        if (!ParseExperiment(*items[i], spec, error))
        {
            if (items.size() > 1)
                error = "experiment " + std::to_string(i + 1) + ": " + error;
            return false;
        }
        if (spec.id.empty())
            spec.id = "e" + std::to_string(i + 1);
        specs.push_back(std::move(spec));
    }
    return true;
}

uint32_t DefaultExperimentWorkers()
{
    const unsigned cores = std::thread::hardware_concurrency();
    return std::max<uint32_t>(1, std::min<uint32_t>(4, cores / 4));
}

ExperimentBatch RunExperiments(const TraceBundle& bundle, const std::vector<ExperimentSpec>& specs,
                               const ExperimentRunOptions& options)
{
    const auto started = std::chrono::steady_clock::now();
    ExperimentBatch batch;
    batch.results.resize(specs.size());
    for (size_t i = 0; i < specs.size(); ++i)
    {
        batch.results[i].id = specs[i].id;
        batch.results[i].finding = specs[i].finding;
        batch.results[i].claim = specs[i].claim;
    }
    if (specs.empty())
        return batch;
    uint32_t workers = options.parallel != 0 ? options.parallel : DefaultExperimentWorkers();
    workers = std::max<uint32_t>(1, std::min<uint32_t>({workers, 8u, static_cast<uint32_t>(specs.size())}));
    batch.workers = workers;

    // Start frames in order: the base seeks once per distinct frame
    std::vector<size_t> order(specs.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const auto rank = [](const ExperimentSpec& s) {
            return s.from == ExperimentSpec::From::Start ? uint64_t{0}
                   : s.from == ExperimentSpec::From::Frame ? uint64_t{1} + s.fromFrame
                                                           : UINT64_MAX;
        };
        return rank(specs[a]) < rank(specs[b]);
    });

    Base base;
    std::atomic<size_t> next{0};
    std::atomic<uint32_t> running{0};
    std::atomic<uint32_t> peak{0};
    const auto work = [&](uint32_t worker) {
        AnalysisInstance machine;
        bool made = false;
        std::string makeError;
        for (size_t slot = next++; slot < order.size(); slot = next++)
        {
            const size_t index = order[slot];
            const ExperimentSpec& spec = specs[index];
            ExperimentResult& result = batch.results[index];
            result.worker = worker;
            const auto t0 = std::chrono::steady_clock::now();
            if (!made && makeError.empty())
                made = machine.Make(bundle, "analysis-experiment", false, makeError);
            if (!made)
            {
                result.error = makeError;
                continue;
            }
            bool copied = false;
            std::string why;
            {
                std::lock_guard<std::mutex> lock(base.mutex);
                if (base.Make(bundle))
                    copied = base.CopyInto(spec, machine, result.fromFrame, why);
                else
                    why = base.error;
            }
            if (!copied)
            {
                result.error = why;
                continue;
            }
            const uint32_t now = ++running;
            uint32_t seen = peak.load();
            while (now > seen && !peak.compare_exchange_weak(seen, now))
            {
            }
            if (options.onStarted)
                options.onStarted(index);
            RunOne(spec, machine, options.l1, result);
            --running;
            result.micros = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
        }
    };
    std::vector<std::thread> threads;
    for (uint32_t w = 1; w < workers; ++w)
        threads.emplace_back(work, w);
    work(0);
    for (std::thread& t : threads)
        t.join();
    batch.peakConcurrency = peak.load();
    batch.seeks = base.seeks.load();
    if (!base.error.empty())
        batch.error = base.error;
    batch.micros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count());
    return batch;
}

StateNode ExperimentResultNode(const ExperimentResult& r)
{
    StateNode node = StateNode::Object();
    node["id"] = r.id;
    node["finding"] = r.finding;
    node["claim"] = r.claim;
    node["verdict"] = r.verdict;
    node["error"] = r.error;
    node["from_frame"] = r.fromFrame;
    node["frames_run"] = static_cast<uint64_t>(r.framesRun);
    StateNode checks = StateNode::Array();
    for (const ExperimentCheck& c : r.checks)
    {
        StateNode item = StateNode::Object();
        item["key"] = c.key;
        item["expect"] = c.expect;
        item["actual"] = c.actual;
        item["pass"] = c.pass;
        checks.push(item);
    }
    node["checks"] = checks;
    StateNode hits = StateNode::Object();
    for (size_t i = 0; i < r.hits.size(); ++i)
    {
        StateNode item = StateNode::Object();
        item["count"] = r.hits[i].second;
        item["first_pc"] = r.hits[i].second != 0 ? Hex4(r.firstPc[i].second) : std::string();
        hits[r.hits[i].first] = item;
    }
    node["hits"] = hits;
    node["new_code"] = static_cast<uint64_t>(r.newCode);
    StateNode ranges = StateNode::Array();
    for (const auto& [lo, hi] : r.newRanges)
        ranges.push(Hex4(lo) + "-" + Hex4(hi));
    node["new_ranges"] = ranges;
    node["worker"] = static_cast<uint64_t>(r.worker);
    return node;
}

std::string ExperimentResultLine(const ExperimentResult& r)
{
    std::string line = r.id + " " + r.verdict;
    if (!r.error.empty())
        return line + ": " + r.error;
    line += " (from frame " + std::to_string(r.fromFrame) + ", " + std::to_string(r.framesRun) + " frames):";
    for (size_t i = 0; i < r.checks.size(); ++i)
        line += std::string(i ? "," : "") + " " + r.checks[i].key + " " + r.checks[i].expect + " -> " +
                r.checks[i].actual + (r.checks[i].pass ? "" : " FAILED");
    if (r.newCode != 0)
    {
        line += "; " + std::to_string(r.newCode) + " new instructions at";
        for (size_t i = 0; i < r.newRanges.size() && i < 4; ++i)
            line += " " + Hex4(r.newRanges[i].first) + "-" + Hex4(r.newRanges[i].second);
    }
    return line;
}

}  // namespace analysis
