// The sound card's CPU on the CLI (gs-debugger design §7.1): `--cpu gs|ms` on registers, memory and disasm, through
// core CpuView (cpuview.h), and on step, steps, stepover, stepout and skip_until through RunControl::CardStep, as every
// other surface.

#include "cli-processor.h"

#include <debugger/boards/debugboards.h>
#include <debugger/debugmanager.h>
#include <debugger/targets/cpuview.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/execution/runcontrol.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace
{
std::string Hex(unsigned value, int digits)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0') << std::setw(digits) << value;
    return out.str();
}

int Int(const StateNode& node, const char* group, const char* name)
{
    const StateNode* g = node.find(group);
    const StateNode* v = g ? g->find(name) : nullptr;
    return v ? static_cast<int>(v->i) : 0;
}
}  // namespace

bool CLIProcessor::HandleCardCpuCommand(const ClientSession& session, Emulator& emulator, const char* command,
                                        const std::vector<std::string>& args,
                                        const std::function<void(const std::vector<std::string>&)>& again)
{
    if (std::find(args.begin(), args.end(), "--cpu") == args.end())
        return false;
    std::vector<std::string> rest = args;
    DebugCpuId cpu = DebugCpuId::Main;
    if (!TakeCpuOption(session, rest, cpu))
        return true;
    if (!IsCardCpu(cpu))
    {
        again(rest);
        return true;
    }
    EmulatorContext* context = emulator.GetContext();
    DebugCpuId selected = cpu;
    if (const std::string error = CpuView::Select(context, DebugCpuName(cpu), selected); !error.empty())
    {
        session.SendResponse("Error: " + error + NEWLINE);
        return true;
    }
    const std::string name = DebugCpuName(cpu);
    const std::string what(command);
    std::ostringstream out;

    if (what == "registers")
    {
        // registers [--cpu gs]; registers <name> / get <name>; registers <name> <value> / set <name> <value>
        std::vector<std::string> words = rest;
        if (!words.empty())
        {
            std::string first = words[0];
            std::transform(first.begin(), first.end(), first.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (first == "get" || first == "set")
                words.erase(words.begin());
        }
        if (!words.empty())
        {
            uint16_t value = 0;
            bool wide = false;
            if (!CpuView::GetRegister(context, cpu, words[0], value, &wide))
            {
                session.SendResponse("Error: Unknown register '" + words[0] + "'" + NEWLINE);
                return true;
            }
            if (words.size() >= 2)
            {
                uint16_t set = 0;
                if (!ParseAddress(words[1], set, 0xFFFF))
                {
                    session.SendResponse("Error: Invalid value '" + words[1] + "'" + NEWLINE);
                    return true;
                }
                const std::string refused = CpuView::SetRegister(&emulator, cpu, "cli register", words[0], set);
                if (!refused.empty())
                {
                    session.SendResponse("Error: " + refused + NEWLINE);
                    return true;
                }
                CpuView::GetRegister(context, cpu, words[0], value);
                out << name << " " << words[0] << " = 0x" << Hex(value, wide ? 4 : 2) << NEWLINE;
                session.SendResponse(out.str());
                return true;
            }
            out << name << " " << words[0] << " = 0x" << Hex(value, wide ? 4 : 2) << " (" << value << ")" << NEWLINE;
            session.SendResponse(out.str());
            return true;
        }
        const StateNode r = CpuView::Registers(context, cpu);
        out << "Z80 Registers of cpu " << name << ":" << NEWLINE;
        out << "  AF: " << Hex(Int(r, "main", "af"), 4) << "  BC: " << Hex(Int(r, "main", "bc"), 4)
            << "  DE: " << Hex(Int(r, "main", "de"), 4) << "  HL: " << Hex(Int(r, "main", "hl"), 4) << NEWLINE;
        out << "  AF': " << Hex(Int(r, "alternate", "af_"), 4) << " BC': " << Hex(Int(r, "alternate", "bc_"), 4)
            << " DE': " << Hex(Int(r, "alternate", "de_"), 4) << " HL': " << Hex(Int(r, "alternate", "hl_"), 4)
            << NEWLINE;
        out << "  IX: " << Hex(Int(r, "index", "ix"), 4) << "  IY: " << Hex(Int(r, "index", "iy"), 4)
            << "  PC: " << Hex(Int(r, "special", "pc"), 4) << "  SP: " << Hex(Int(r, "special", "sp"), 4) << NEWLINE;
        out << "  I: " << Hex(Int(r, "special", "i"), 2) << "  R: " << Hex(Int(r, "special", "r"), 2)
            << "  IM: " << Int(r, "interrupt", "im") << "  IFF1: " << Int(r, "interrupt", "iff1")
            << "  IFF2: " << Int(r, "interrupt", "iff2")
            << (r.find("interrupt") && r.find("interrupt")->find("halted") && r.find("interrupt")->find("halted")->b
                    ? "  HALT"
                    : "")
            << NEWLINE;
        const int f = Int(r, "main", "af") & 0xFF;
        out << "  Flags: " << ((f & 0x80) ? 'S' : '-') << ((f & 0x40) ? 'Z' : '-') << ((f & 0x20) ? 'Y' : '-')
            << ((f & 0x10) ? 'H' : '-') << ((f & 0x08) ? 'X' : '-') << ((f & 0x04) ? 'P' : '-')
            << ((f & 0x02) ? 'N' : '-') << ((f & 0x01) ? 'C' : '-') << NEWLINE;
        session.SendResponse(out.str());
        return true;
    }

    if (what == "memory")
    {
        // memory [read] <addr> [len], memory write <addr> <bytes...>, memory info / map: the card CPU's view
        std::vector<std::string> words = rest;
        const std::string sub = words.empty() ? std::string() : words[0];
        if (sub == "info" || sub == "map")
        {
            StateNode windows = CpuView::Windows(context, cpu);
            out << "Windows of cpu " << name << ":" << NEWLINE;
            if (StateNode* list = windows.find("windows") ? &windows["windows"] : nullptr)
                for (StateNode& w : list->items)
                {
                    out << "  " << Hex(static_cast<unsigned>(w["start"].i), 4) << "-"
                        << Hex(static_cast<unsigned>(w["start"].i + 0x3FFF), 4) << "  " << w["kind"].s;
                    if (w.find("page"))
                        out << " page " << w["page"].i;
                    out << NEWLINE;
                }
            session.SendResponse(out.str());
            return true;
        }
        if (sub == "write")
        {
            uint16_t address = 0;
            if (words.size() < 3 || !ParseAddress(words[1], address))
            {
                session.SendResponse("Usage: memory write <addr> <bytes...> --cpu " + name + NEWLINE);
                return true;
            }
            std::vector<uint8_t> bytes;
            for (size_t i = 2; i < words.size(); i++)
            {
                uint16_t byte = 0;
                if (!ParseAddress(words[i], byte, 0xFF))
                {
                    session.SendResponse("Error: Invalid byte '" + words[i] + "'" + NEWLINE);
                    return true;
                }
                bytes.push_back(static_cast<uint8_t>(byte));
            }
            size_t written = 0;
            const std::string refused = CpuView::Write(&emulator, cpu, "CLI memory write", address, bytes, &written);
            if (!refused.empty())
                session.SendResponse("Error: " + refused + NEWLINE);
            else
                session.SendResponse("Wrote " + std::to_string(written) + " bytes at 0x" + Hex(address, 4) + " (cpu " +
                                     name + ")" +
                                     (written < bytes.size()
                                          ? ", " + std::to_string(bytes.size() - written) + " kept by a read-only window"
                                          : std::string()) +
                                     NEWLINE);
            return true;
        }
        if (sub == "read")
            words.erase(words.begin());
        uint16_t address = 0;
        uint16_t length = 128;
        if (words.empty() || !ParseAddress(words[0], address) || (words.size() > 1 && !ParseAddress(words[1], length)))
        {
            session.SendResponse("Usage: memory [read] <addr> [len] --cpu " + name +
                                 "; memory write <addr> <bytes...> --cpu " + name + "; memory info --cpu " + name +
                                 NEWLINE);
            return true;
        }
        std::vector<uint8_t> bytes;
        CpuView::Read(context, cpu, address, std::max<uint16_t>(length, 1), bytes);
        for (size_t row = 0; row < bytes.size(); row += 16)
        {
            out << Hex(static_cast<uint16_t>(address + row), 4) << ":";
            std::string text;
            for (size_t i = row; i < std::min(bytes.size(), row + 16); i++)
            {
                out << " " << Hex(bytes[i], 2);
                text += bytes[i] >= 0x20 && bytes[i] < 0x7F ? static_cast<char>(bytes[i]) : '.';
            }
            out << "  " << text << NEWLINE;
        }
        session.SendResponse(out.str());
        return true;
    }

    if (what == "step" || what == "steps" || what == "stepover" || what == "stepout" || what == "skip_until")
    {
        // step [count] / steps <count> / stepover / stepout / skip_until <pc> --cpu gs: the machine runs until the
        // card's step ends (skip_until: the card runs to the cursor)
        unsigned count = 1;
        uint16_t cursor = 0;
        if (what == "skip_until" && (rest.empty() || !ParseAddress(rest[0], cursor)))
        {
            session.SendResponse("Usage: skip_until <pc> --cpu " + name + NEWLINE);
            return true;
        }
        if (what == "step" || what == "steps")
        {
            uint16_t parsed = 1;
            if ((what == "steps" && rest.empty()) ||
                (!rest.empty() && (!ParseAddress(rest[0], parsed, 10000) || parsed == 0)))
            {
                session.SendResponse("Usage: " + what + (what == "steps" ? " <count 1-10000>" : " [count 1-10000]") +
                                     " --cpu " + name + NEWLINE);
                return true;
            }
            count = parsed;
        }
        std::shared_ptr<Emulator> shared = EmulatorManager::GetInstance()->GetEmulator(emulator.GetId());
        if (!shared)
        {
            session.SendResponse("Error: the emulator is gone" + std::string(NEWLINE));
            return true;
        }
        const char* kind = what == "stepover" ? "over" : what == "stepout" ? "out" : what == "skip_until" ? "to" : "in";
        const execution::RunReply reply =
            execution::RunControl(shared).CardStep(CliRequester(context), cpu, kind, count, cursor);
        if (!reply.Ok())
        {
            session.SendResponse("Error: " + reply.message + NEWLINE);
            return true;
        }
        StateNode body = reply.body;
        const std::string reason = body.find("stop") && body["stop"].find("reason") ? body["stop"]["reason"].s : "";
        out << reply.message;
        if (reason == "breakpoint")
        {
            StateNode& stop = body["stop"];
            out << ": breakpoint #" << stop["breakpoint_id"].i << " of cpu " << stop["cpu"].s << " ("
                << stop["access"].s << " 0x" << Hex(static_cast<unsigned>(stop["address"].i), 4) << ")";
        }
        out << NEWLINE;
        out << "  cpu " << name << " PC: 0x" << Hex(static_cast<unsigned>(body["pc"].i), 4) << "  SP: 0x"
            << Hex(static_cast<unsigned>(body["sp"].i), 4) << "  main PC: 0x"
            << Hex(static_cast<unsigned>(body["main_pc"].i), 4) << NEWLINE;
        session.SendResponse(out.str());
        return true;
    }

    // disasm [address] [count] --cpu gs: from the card CPU's PC by default
    uint16_t address = 0;
    CpuView::GetRegister(context, cpu, "pc", address);
    uint16_t count = 10;
    if ((!rest.empty() && !ParseAddress(rest[0], address)) ||
        (rest.size() > 1 && (!ParseAddress(rest[1], count, 256) || count == 0)))
    {
        session.SendResponse("Usage: disasm [address] [count 1-256] --cpu " + name + NEWLINE);
        return true;
    }
    StateNode listing = CpuView::Disasm(context, cpu, address, count);
    uint16_t pc = 0;
    CpuView::GetRegister(context, cpu, "pc", pc);
    for (StateNode& line : listing["instructions"].items)
    {
        const unsigned at = static_cast<unsigned>(line["address"].i);
        if (const StateNode* label = line.find("label"))
            out << "  " << label->s << ":" << NEWLINE;  // the card's firmware symbols
        out << (at == pc ? "> " : "  ") << Hex(at, 4) << "  " << std::left << std::setw(10) << std::setfill(' ')
            << line["bytes"].s << std::right << "  " << line["mnemonic"].s << NEWLINE;
    }
    session.SendResponse(out.str());
    return true;
}

// card_run_to <cycle N | int | command | main_t N> [--cpu gs|ms]: a card CPU's run to a moment (RunControl::CardRunTo,
// the card debugger's Run to): the machine paused, gs by default
void CLIProcessor::HandleCardRunTo(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("No emulator selected.") + NEWLINE);
        return;
    }
    std::vector<std::string> rest = args;
    DebugCpuId cpu = DebugCpuId::GS;
    if (std::find(rest.begin(), rest.end(), "--cpu") != rest.end() && !TakeCpuOption(session, rest, cpu))
        return;
    const std::string usage = "Usage: card_run_to cycle <N> | int | command | main_t <T> [--cpu gs|ms]";
    if (rest.empty())
    {
        session.SendResponse(usage + NEWLINE);
        return;
    }
    const std::string& until = rest[0];
    const bool valued = until == "cycle" || until == "main_t";
    uint64_t value = 0;
    if (valued)
    {
        if (rest.size() < 2)
        {
            session.SendResponse(usage + NEWLINE);
            return;
        }
        try
        {
            size_t used = 0;
            value = std::stoull(rest[1], &used, 0);
            if (used != rest[1].size())
                throw std::invalid_argument(rest[1]);
        }
        catch (const std::exception&)
        {
            session.SendResponse("Error: '" + rest[1] + "' is not a number" + std::string(NEWLINE));
            return;
        }
    }
    const execution::RunReply reply =
        execution::RunControl(emulator).CardRunTo(CliRequester(emulator->GetContext()), cpu, until, value);
    if (!reply.Ok())
    {
        session.SendResponse("Error: " + reply.message + NEWLINE);
        return;
    }
    const StateNode& body = reply.body;
    const StateNode* stop = body.find("stop");
    const StateNode* reason = stop ? stop->find("reason") : nullptr;
    std::ostringstream out;
    out << reply.message;
    if (reason && reason->s == "breakpoint")
        out << ": breakpoint #" << stop->find("breakpoint_id")->i << " of cpu " << stop->find("cpu")->s << " ("
            << stop->find("access")->s << " 0x" << Hex(static_cast<unsigned>(stop->find("address")->i), 4) << ")";
    else if (reason && reason->s != "step")
        out << " (" << reason->s << ")";
    out << NEWLINE;
    auto number = [&body](const char* key) { const StateNode* v = body.find(key); return v ? v->i : 0; };
    out << "  cpu " << DebugCpuName(cpu) << " PC: 0x" << Hex(static_cast<unsigned>(number("pc")), 4) << "  SP: 0x"
        << Hex(static_cast<unsigned>(number("sp")), 4);
    if (body.find("card_cycle"))
        out << "  card cycle " << number("card_cycle");
    out << "  main PC: 0x" << Hex(static_cast<unsigned>(number("main_pc")), 4) << "  main T " << number("main_t")
        << "  frame " << number("frame") << NEWLINE;
    session.SendResponse(out.str());
}

// selected-cpu [main|gs|ms]: the CPU the front-ends show (gs-debugger design §5.2, core CpuView::SetSelected, the
// WebAPI GET / PUT /debug/selected_cpu); a card shown follows the main CPU's steps of the paused machine
void CLIProcessor::HandleSelectedCpu(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("No emulator selected.") + NEWLINE);
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    if (!args.empty())
    {
        if (const std::string error = CpuView::SetSelected(context, args[0]); !error.empty())
        {
            session.SendResponse("Error: " + error + NEWLINE);
            return;
        }
    }
    StateNode selected = CpuView::Selected(context);
    session.SendResponse("Selected cpu: " + selected["cpu"].s +
                         (selected["follows"].b ? " (follows the main CPU's steps)" : "") + NEWLINE);
}

// board [list] | board <id> [values] | board <id> <action> [arg]: the device boards (debugger-model widget catalog §4,
// core DebugBoards, the WebAPI /debug/boards); neogs_sd insert <path> / eject go to the media manager's slot sd.ngs
void CLIProcessor::HandleBoard(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("No emulator selected.") + NEWLINE);
        return;
    }
    EmulatorContext* context = emulator->GetContext();
    DebugBoardReply reply;
    if (args.empty() || args[0] == "list")
        reply = DebugBoards::List(context);
    else if (args.size() == 1 || args[1] == "values")
    {
        reply = DebugBoards::Read(context, args[0], args.size() > 1);
        if (reply.status == 200 && args.size() > 1)
        {
            std::ostringstream values;
            for (const auto& [id, value] : reply.body["values"].members)
                values << id << " = " << (value.kind == StateNode::Kind::String ? value.s
                                          : value.kind == StateNode::Kind::Bool ? (value.b ? "true" : "false")
                                          : value.kind == StateNode::Kind::Double ? std::to_string(value.d)
                                                                                  : std::to_string(value.i))
                       << NEWLINE;
            reply.text = values.str();
        }
    }
    else
    {
        // The rest of the line is the argument: a path may hold spaces
        std::string arg;
        for (size_t i = 2; i < args.size(); i++)
            arg += (i > 2 ? " " : "") + args[i];
        reply = DebugBoards::Action(context, args[0], args[1], arg);
    }
    std::string text = reply.text;
    if (!text.empty() && text.back() == '\n')
        text.pop_back();
    session.SendResponse((reply.status == 200 ? "" : "Error: ") + text + NEWLINE);
}
