#include "debugsnapshot.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <string>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/memory/memoryread.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/state/devicestate.h"
#include "emulator/memory/memory.h"

namespace DebugSnapshot
{
StateNode Registers(EmulatorContext* context)
{
    Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    return z80 ? RegistersOf(*z80) : StateNode::Object();
}

StateNode RegistersOf(const Z80State& state)
{
    StateNode ret = StateNode::Object();
    const Z80State* z80 = &state;

    StateNode main = StateNode::Object();
    main["af"] = static_cast<int>(z80->af);
    main["bc"] = static_cast<int>(z80->bc);
    main["de"] = static_cast<int>(z80->de);
    main["hl"] = static_cast<int>(z80->hl);
    ret["main"] = main;

    StateNode alt = StateNode::Object();
    alt["af_"] = static_cast<int>(z80->alt.af);
    alt["bc_"] = static_cast<int>(z80->alt.bc);
    alt["de_"] = static_cast<int>(z80->alt.de);
    alt["hl_"] = static_cast<int>(z80->alt.hl);
    ret["alternate"] = alt;

    StateNode index = StateNode::Object();
    index["ix"] = static_cast<int>(z80->ix);
    index["iy"] = static_cast<int>(z80->iy);
    ret["index"] = index;

    StateNode special = StateNode::Object();
    special["pc"] = static_cast<int>(z80->pc);
    special["sp"] = static_cast<int>(z80->sp);
    special["i"] = static_cast<int>(z80->i);
    special["r"] = static_cast<int>(Z80::RegisterR(z80));
    special["memptr"] = static_cast<int>(z80->memptr);
    special["q"] = static_cast<int>(z80->q);
    special["t"] = static_cast<uint64_t>(z80->t);   // CPU T-states since the frame's start
    ret["special"] = special;

    StateNode interrupt = StateNode::Object();
    interrupt["iff1"] = static_cast<int>(z80->iff1);
    interrupt["iff2"] = static_cast<int>(z80->iff2);
    interrupt["im"] = static_cast<int>(z80->im);
    interrupt["halted"] = z80->halted != 0;
    interrupt["boundary"] = std::string(Z80::BoundaryName(z80->boundary));   // what the next INT / NMI sampling sees
    ret["interrupt"] = interrupt;

    const uint8_t f = static_cast<uint8_t>(z80->af & 0xFF);
    StateNode flags = StateNode::Object();
    flags["s"] = (f & 0x80) ? 1 : 0;
    flags["z"] = (f & 0x40) ? 1 : 0;
    flags["y"] = (f & 0x20) ? 1 : 0;
    flags["h"] = (f & 0x10) ? 1 : 0;
    flags["x"] = (f & 0x08) ? 1 : 0;
    flags["pv"] = (f & 0x04) ? 1 : 0;
    flags["n"] = (f & 0x02) ? 1 : 0;
    flags["c"] = (f & 0x01) ? 1 : 0;
    ret["flags"] = flags;
    return ret;
}

StateNode Disasm(EmulatorContext* context, uint16_t address, size_t count)
{
    DebugManager* dbg = context ? context->pDebugManager : nullptr;
    Memory* memory = context ? context->pMemory : nullptr;
    Z80* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    if (!dbg || !dbg->GetDisassembler() || !memory || !z80)
    {
        StateNode unavailable = StateNode::Object();
        unavailable["available"] = false;
        unavailable["description"] = std::string("Disassembler not available");
        return unavailable;
    }
    Z80Disassembler* disasm = dbg->GetDisassembler().get();
    LabelManager* labels = dbg->GetLabelManager();
    count = std::clamp<size_t>(count, 1, 100);

    StateNode ret = StateNode::Object();
    ret["address"] = static_cast<int>(address);
    ret["count"] = static_cast<unsigned>(count);
    StateNode instructions = StateNode::Array();

    uint16_t current = address;
    for (size_t i = 0; i < count && current >= address; ++i)
    {
        // Direct (non-mutating) reads: the view must not strobe the ProfROM quadrant machine on #0000-#0003
        std::vector<uint8_t> buffer;
        for (int j = 0; j < 4; ++j)
            buffer.push_back(memory->DirectReadFromZ80Memory(static_cast<uint16_t>(current + j)));

        uint8_t length = 0;
        DecodedInstruction decoded;
        const std::string mnemonic = disasm->disassembleSingleCommandWithRuntime(buffer, current, &length, z80, memory, &decoded);
        if (length == 0)
            length = 1;   // at least advance by one

        StateNode line = StateNode::Object();
        line["address"] = static_cast<int>(current);
        std::string hex;
        for (uint8_t j = 0; j < length; ++j)
        {
            char byte[4];
            std::snprintf(byte, sizeof(byte), "%02X", buffer[j]);
            hex += byte;
        }
        line["bytes"] = hex;
        line["mnemonic"] = mnemonic;
        line["size"] = static_cast<int>(length);
        if (labels)
        {
            auto label = labels->GetLabelByZ80Address(current);
            if (label && !label->name.empty())
                line["label"] = label->name;
        }
        // Jump / call targets; an indirect one (JP (HL)) only when the runtime registers resolve it
        if (decoded.hasJump || decoded.hasRelativeJump)
        {
            const uint16_t target = decoded.hasRelativeJump ? decoded.relJumpAddr : decoded.jumpAddr;
            if (!decoded.hasIndirect || decoded.hasRuntime)
            {
                line["target"] = static_cast<int>(target);
                if (labels)
                {
                    auto targetLabel = labels->GetLabelByZ80Address(target);
                    if (targetLabel && !targetLabel->name.empty())
                        line["targetLabel"] = targetLabel->name;
                }
            }
        }
        // The effective address of an (IX/IY+d) operand, with the runtime registers
        if (decoded.hasDisplacement && decoded.hasRuntime)
        {
            line["displacement"] = static_cast<int>(decoded.displacement);
            line["effectiveAddress"] = static_cast<int>(decoded.displacementAddr);
            if (labels)
            {
                auto effectiveLabel = labels->GetLabelByZ80Address(decoded.displacementAddr);
                if (effectiveLabel && !effectiveLabel->name.empty())
                    line["effectiveAddressLabel"] = effectiveLabel->name;
            }
        }
        instructions.push(line);
        current = static_cast<uint16_t>(current + length);
    }
    ret["instructions"] = instructions;
    return ret;
}
std::string Base64(const std::vector<uint8_t>& bytes)
{
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3)
    {
        const uint32_t v = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += kAlphabet[(v >> 6) & 63];
        out += kAlphabet[v & 63];
    }
    if (i < bytes.size())
    {
        const uint32_t v = (uint32_t(bytes[i]) << 16) | (i + 1 < bytes.size() ? uint32_t(bytes[i + 1]) << 8 : 0);
        out += kAlphabet[(v >> 18) & 63];
        out += kAlphabet[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kAlphabet[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string Validate(const Options& options)
{
    if (options.stack > kMaxStackWords)
        return "stack: at most " + std::to_string(kMaxStackWords) + " words";
    if (options.memory.size() > kMaxWindows)
        return "memory: at most " + std::to_string(kMaxWindows) + " windows";
    for (const std::string& window : options.memory)
    {
        std::string space, error;
        uint32_t address = 0, length = 0;
        if (!MemoryRead::ParseWindow(window, space, address, length, error))
            return "memory: " + error;
    }
    return {};
}

namespace
{
const char* StopReason(Emulator::DebugStop::Reason reason)
{
    switch (reason)
    {
        case Emulator::DebugStop::Reason::Pause: return "pause";
        case Emulator::DebugStop::Reason::Breakpoint: return "breakpoint";
        case Emulator::DebugStop::Reason::Step: return "step";
        default: return "none";
    }
}

const char* BankKind(MemoryBankModeEnum mode)
{
    switch (mode)
    {
        case BANK_ROM: return "rom";
        case BANK_RAM: return "ram";
        case BANK_CACHE: return "cache";
        default: return "none";
    }
}

/// Every part, read now: the caller guarantees nothing runs the machine meanwhile
StateNode Capture(Emulator* emulator, const Options& options, const char* consistency)
{
    EmulatorContext* context = emulator->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    Memory* memory = context->pMemory;

    StateNode node = StateNode::Object();
    node["seq"] = emulator->DebugSeq();
    node["cpu"] = std::string("z80");
    node["state"] = std::string(emulator->IsPaused() ? "paused" : emulator->IsRunning() ? "running" : "stopped");
    const Emulator::DebugStop stop = emulator->LastStop();
    StateNode pause = StateNode::Object();
    pause["reason"] = std::string(StopReason(stop.reason));
    if (stop.reason == Emulator::DebugStop::Reason::Breakpoint)
    {
        pause["breakpoint_id"] = static_cast<int>(stop.breakpoint.breakpointId);
        pause["address"] = static_cast<int>(stop.breakpoint.address);
    }
    node["pause"] = pause;
    node["consistency"] = std::string(consistency);
    node["regs"] = Registers(context);
    Z80State previous;
    node["prev_regs"] = emulator->PreviousStopRegisters(previous) ? RegistersOf(previous) : StateNode();

    StateNode pages = StateNode::Array();
    for (uint8_t window = 0; window < 4; ++window)
    {
        const MemoryPageDescriptor where = memory->MapZ80AddressToPhysicalPage(static_cast<uint16_t>(window * 0x4000));
        StateNode page = StateNode::Object();
        page["window"] = static_cast<int>(window);
        page["start"] = static_cast<int>(window * 0x4000);
        page["kind"] = std::string(BankKind(where.mode));
        page["page"] = static_cast<int>(where.page);
        pages.push(page);
    }
    node["pages"] = pages;

    if (options.stack)
    {
        StateNode stack = StateNode::Object();
        stack["sp"] = static_cast<int>(z80->sp);
        StateNode words = StateNode::Array();
        for (unsigned i = 0; i < options.stack; ++i)
        {
            const uint16_t at = static_cast<uint16_t>(z80->sp + i * 2);
            words.push(StateNode(static_cast<int>(memory->DirectReadFromZ80Memory(at) |
                                                  (memory->DirectReadFromZ80Memory(static_cast<uint16_t>(at + 1)) << 8))));
        }
        stack["words"] = words;
        node["stack"] = stack;
    }

    StateNode time = StateNode::Object();
    time["frame"] = static_cast<uint64_t>(context->emulatorState.frame_counter);
    time["t"] = static_cast<uint64_t>(z80->t);
    time["frame_t"] = static_cast<uint64_t>(context->config.frame);
    const StateNode beam = DeviceState::VideoBeam(context);
    if (const StateNode* line = beam.find("line"))
        time["line"] = *line;
    if (const StateNode* dot = beam.find("dot_in_line"))
        time["dot"] = *dot;
    node["time"] = time;

    if (options.disasm)
    {
        const StateNode lines = Disasm(context, z80->pc, options.disasm);
        const StateNode* instructions = lines.find("instructions");
        node["disasm"] = instructions ? *instructions : StateNode::Array();
    }

    if (!options.memory.empty())
    {
        StateNode windows = StateNode::Array();
        for (const std::string& text : options.memory)
        {
            std::string space, error;
            uint32_t address = 0, length = 0;
            MemoryRead::ParseWindow(text, space, address, length, error);   // checked by Validate
            const MemoryRead::Result read = MemoryRead::Bytes(context, space, address, length);
            StateNode window = StateNode::Object();
            window["space"] = read.error.empty() ? read.space : space;
            window["address"] = static_cast<uint64_t>(address);
            if (!read.error.empty())
                window["error"] = read.error;
            else
            {
                window["length"] = static_cast<uint64_t>(read.bytes.size());
                if (options.rawBytes)
                    window["bytes"] = std::string(read.bytes.begin(), read.bytes.end());
                else
                    window["base64"] = Base64(read.bytes);
            }
            windows.push(window);
        }
        node["memory"] = windows;
    }
    return node;
}
}  // namespace

Result Build(Emulator* emulator, const Options& options)
{
    Result result;
    if (!emulator || !emulator->GetContext() || !emulator->GetContext()->pCore || !emulator->GetContext()->pMemory)
    {
        result.error = "emulator not available";
        return result;
    }
    result.error = Validate(options);
    if (!result.error.empty())
        return result;

    const auto paused = [&]() {
        return emulator->RunWhileParked([&]() { result.snapshot = Capture(emulator, options, "paused"); });
    };
    if (paused())
        return result;
    // Not started and nobody steps it: nothing can change it
    if (!emulator->IsRunning() && !emulator->IsDirectStepping())
    {
        result.snapshot = Capture(emulator, options, "stopped");
        return result;
    }
    // Running: the emulation thread takes it between two frames, without a pause
    if (emulator->IsRunning() && !emulator->IsPaused() &&
        emulator->RunAtFrameBoundary([&]() { result.snapshot = Capture(emulator, options, "frame"); }, 500))
        return result;
    // It paused meanwhile, or a direct step runs on another thread: wait for the park, briefly
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (paused())
            return result;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    result.busy = true;
    result.error = "no coherent moment within 500 ms (the emulator is stepping or changing state); try again";
    return result;
}
}  // namespace DebugSnapshot
