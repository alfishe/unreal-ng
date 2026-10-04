// ISA slots - 'isa' command and 'state isa' (Sprinter ISA tdd §10). Same core calls every interface uses:
// DeviceState::Isa for the report, IsaAccess::Execute for cycles.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/sprinter/isa/isaaccess.h>
#include <emulator/state/devicestate.h>

#include <cstdlib>
#include <sstream>

#include "cli-processor.h"

std::string CLIProcessor::IsaReportText(EmulatorContext* context)
{
    std::stringstream ss;
    ss << "ISA slots" << NEWLINE << "=========" << NEWLINE << DeviceState::ToText(DeviceState::Isa(context));
    return ss.str();
}

void CLIProcessor::HandleIsa(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    EmulatorContext* context = emulator->GetContext();

    if (args.empty() || args[0] == "state" || args[0] == "show")
    {
        session.SendResponse(IsaReportText(context));
        return;
    }

    const std::string& sub = args[0];
    if (sub == "irq")
    {
        // The interrupt lines only: PIO port B and each slot's IRQ object (the same report, cut down)
        const StateNode report = DeviceState::Isa(context);
        StateNode view = StateNode::Object();
        if (const StateNode* available = report.find("available"); available && !available->b)
        {
            session.SendResponse(DeviceState::ToText(report));
            return;
        }
        if (const StateNode* summary = report.find("irq_summary"))
            view["irq_summary"] = *summary;
        if (const StateNode* pio = report.find("pio_port_b"))
            view["pio_port_b"] = *pio;
        StateNode slots = StateNode::Array();
        if (const StateNode* all = report.find("slots"))
        {
            for (const StateNode& slot : all->items)
            {
                StateNode one = StateNode::Object();
                one["slot"] = *slot.find("slot");
                one["card"] = *slot.find("card");
                if (const StateNode* irq = slot.find("irq_line"))
                    one["irq_line"] = *irq;
                slots.push(one);
            }
        }
        view["slots"] = slots;
        session.SendResponse(DeviceState::ToText(view));
        return;
    }
    if (sub == "help")
    {
        std::stringstream ss;
        ss << "ISA slots (Sprinter Sp2000: slot 1 = J6, page #D4 / #D0; slot 2 = J7, page #D6 / #D2):" << NEWLINE;
        ss << "  isa                              - Report: #9FBD latch, window 3, slots, cards, counters" << NEWLINE;
        ss << "  isa io <slot> <address> [value]  - ISA I/O cycle: read, or write the value" << NEWLINE;
        ss << "  isa mem <slot> <address> [value] - ISA memory cycle: read, or write the value" << NEWLINE;
        ss << "  isa peek <slot> <address> [mem]  - What the card shows (I/O, or memory with 'mem'), no side effect" << NEWLINE;
        ss << "  isa reset                        - One RESET DRV pulse to both slots" << NEWLINE;
        ss << "  isa latch <value>                - Write the #9FBD latch (A19-A14, AEN bit 6, RESET bit 7)" << NEWLINE;
        ss << "  isa irq                          - The IRQ lines: level, driver, PIO port B route, pending, counters" << NEWLINE;
        ss << "  isa journal [n | clear | on | off] - Who touched which card register, and the IRQ events" << NEWLINE;
        ss << "  Addresses: 20-bit ISA address, decimal, 0x.., #.. or ..h (the NE2000 at #300: isa io 2 #30A)" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (sub == "journal")
    {
        if (args.size() > 1 && (args[1] == "clear" || args[1] == "on" || args[1] == "off"))
        {
            StateNode result;
            std::string error;
            if (!IsaAccess::Execute(context, "journal_" + args[1], 0, 0, -1, "CLI isa", result, error))
                session.SendResponse("Error: " + error + NEWLINE);
            else
                session.SendResponse("ISA journal " + args[1] + NEWLINE);
            return;
        }
        const unsigned last = args.size() > 1 ? static_cast<unsigned>(std::strtoul(args[1].c_str(), nullptr, 10)) : 32u;
        session.SendResponse(DeviceState::ToText(DeviceState::IsaJournal(context, last)));
        return;
    }

    std::string action;
    int slot = 0;
    uint32_t address = 0;
    int value = -1;
    auto parseValue = [&](const std::string& text) {
        uint32_t v = 0;
        if (!IsaAccess::ParseAddress(text, v) || v > 0xFF)
            return false;
        value = static_cast<int>(v);
        return true;
    };

    bool ok = true;
    if (sub == "io" || sub == "mem")
    {
        ok = args.size() >= 3 && IsaAccess::ParseAddress(args[2], address);
        slot = ok ? std::atoi(args[1].c_str()) : 0;
        if (ok && args.size() > 3)
            ok = parseValue(args[3]);
        action = sub + (value >= 0 ? "_write" : "_read");
    }
    else if (sub == "peek")
    {
        ok = args.size() >= 3 && IsaAccess::ParseAddress(args[2], address);
        slot = ok ? std::atoi(args[1].c_str()) : 0;
        action = args.size() > 3 && args[3] == "mem" ? "mem_peek" : "io_peek";
    }
    else if (sub == "reset")
        action = "reset";
    else if (sub == "latch")
    {
        ok = args.size() >= 2 && parseValue(args[1]);
        action = "latch";
    }
    else
    {
        session.SendResponse("Unknown isa subcommand '" + sub + "'. Use 'isa help'." + std::string(NEWLINE));
        return;
    }
    if (!ok)
    {
        session.SendResponse("Usage: see 'isa help'" + std::string(NEWLINE));
        return;
    }

    StateNode result;
    std::string error;
    if (!IsaAccess::Execute(context, action, slot, address, value, "CLI isa", result, error))
    {
        session.SendResponse("Error: " + error + NEWLINE);
        return;
    }
    session.SendResponse(DeviceState::ToText(result));
}
