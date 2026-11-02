// The sound card's CPU on the CLI (gs-debugger design §7.1): `--cpu gs|ms` on registers, memory and disasm, through
// core CpuView (cpuview.h) as every other surface.

#include "cli-processor.h"

#include <debugger/debugmanager.h>
#include <debugger/targets/cpuview.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>

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
        out << (at == pc ? "> " : "  ") << Hex(at, 4) << "  " << std::left << std::setw(10) << std::setfill(' ')
            << line["bytes"].s << std::right << "  " << line["mnemonic"].s << NEWLINE;
    }
    session.SendResponse(out.str());
    return true;
}
