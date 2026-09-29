// CMOS clock - 'rtc' / 'cmos' command and 'state rtc'. Same core calls every
// interface uses: DeviceState::Rtc for the report, RtcAccess for cells.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/io/rtc/rtcaccess.h>
#include <emulator/state/devicestate.h>

#include <cstdio>
#include <sstream>

#include "cli-processor.h"

namespace
{
/// Decimal, 0x.. / #.. / ..h hex; false when not a number in [0, max]
bool ParseRtcNumber(const std::string& text, unsigned max, unsigned& out)
{
    std::string digits = text;
    int base = 10;
    if (digits.size() > 2 && (digits.compare(0, 2, "0x") == 0 || digits.compare(0, 2, "0X") == 0))
    {
        digits = digits.substr(2);
        base = 16;
    }
    else if (digits.size() > 1 && (digits[0] == '#' || digits[0] == '$'))
    {
        digits = digits.substr(1);
        base = 16;
    }
    else if (digits.size() > 1 && (digits.back() == 'h' || digits.back() == 'H'))
    {
        digits.pop_back();
        base = 16;
    }
    if (digits.empty())
        return false;
    try
    {
        size_t used = 0;
        const unsigned long value = std::stoul(digits, &used, base);
        if (used != digits.size() || value > max)
            return false;
        out = static_cast<unsigned>(value);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
}  // namespace

std::string CLIProcessor::RtcReportText(EmulatorContext* context)
{
    std::stringstream ss;
    ss << "CMOS clock" << NEWLINE << "==========" << NEWLINE << DeviceState::ToText(DeviceState::Rtc(context));
    return ss.str();
}

void CLIProcessor::HandleRtc(const ClientSession& session, const std::vector<std::string>& args)
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
        session.SendResponse(RtcReportText(context));
        return;
    }

    const std::string& sub = args[0];
    if (sub == "help")
    {
        std::stringstream ss;
        ss << "CMOS clock (MC146818 / DS12887, ATM3 / ZX-Evo, Profi, Scorpion with SMUC):" << NEWLINE;
        ss << "  rtc                          - Report: time, registers A-D, alarms, every cell" << NEWLINE;
        ss << "  rtc read <start> [count]     - Read cells as the guest reads them (no side effects)" << NEWLINE;
        ss << "  rtc write <start> <b> [b..]  - Write cells like the guest (time registers set the clock)" << NEWLINE;
        ss << "  Numbers: decimal, 0x.., #.. or ..h. 'cmos' is an alias of 'rtc'." << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    std::string error;
    if (sub == "read")
    {
        unsigned start = 0;
        unsigned count = 1;
        if (args.size() < 2 || !ParseRtcNumber(args[1], 255, start) ||
            (args.size() > 2 && !ParseRtcNumber(args[2], 256, count)))
        {
            session.SendResponse("Usage: rtc read <start> [count]" + std::string(NEWLINE));
            return;
        }

        std::vector<uint8_t> bytes;
        if (!RtcAccess::Read(context, start, count, bytes, error))
        {
            session.SendResponse("Error: " + error + NEWLINE);
            return;
        }
        std::stringstream ss;
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            char cell[8];
            if (i % 16 == 0)
            {
                if (i)
                    ss << NEWLINE;
                std::snprintf(cell, sizeof(cell), "%02X:", unsigned(start + i));
                ss << cell;
            }
            std::snprintf(cell, sizeof(cell), " %02X", bytes[i]);
            ss << cell;
        }
        ss << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    if (sub == "write")
    {
        unsigned start = 0;
        std::vector<uint8_t> bytes;
        bool ok = args.size() >= 3 && ParseRtcNumber(args[1], 255, start);
        for (size_t i = 2; ok && i < args.size(); ++i)
        {
            unsigned value = 0;
            ok = ParseRtcNumber(args[i], 255, value);
            bytes.push_back(static_cast<uint8_t>(value));
        }
        if (!ok)
        {
            session.SendResponse("Usage: rtc write <start> <byte> [byte ...]" + std::string(NEWLINE));
            return;
        }
        if (!RtcAccess::Write(context, start, bytes, "CLI rtc write", error))
        {
            session.SendResponse("Error: " + error + NEWLINE);
            return;
        }
        session.SendResponse("Wrote " + std::to_string(bytes.size()) + " cell(s) from " + std::to_string(start) +
                             NEWLINE);
        return;
    }

    session.SendResponse("Unknown rtc subcommand '" + sub + "'. Try 'rtc help'." + std::string(NEWLINE));
}
