/**
 * @file cli-processor-profiler.cpp
 * @brief Profiler command handlers for the CLI processor
 *
 * Handles commands:
 * - profiler opcode start|stop|clear|status|counters|trace|save
 */

#include "cli-processor.h"

#include <iomanip>
#include <sstream>

#include "base/featuremanager.h"
#include "emulator/cpu/opcode_profiler.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

/// @brief Handle profiler commands
/// Usage:
///   profiler opcode start       - Start opcode capture
///   profiler opcode stop        - Stop opcode capture
///   profiler opcode clear       - Clear all profiler data
///   profiler opcode status      - Show profiler status
///   profiler opcode counters [N] - Show top N opcodes (default 50)
///   profiler opcode trace [N]   - Show last N trace entries (default 100)
///   profiler opcode save <file> - Save profiler data to file
void CLIProcessor::HandleProfiler(const ClientSession& session, const std::vector<std::string>& args)
{
    if (args.empty())
    {
        ShowProfilerHelp(session);
        return;
    }

    // Get the emulator
    std::string errorMessage;
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select' or 'list' to manage emulators." + std::string(NEWLINE));
        return;
    }

    auto* context = emulator->GetContext();
    if (!context || !context->pCore)
    {
        session.SendResponse("Emulator context not available." + std::string(NEWLINE));
        return;
    }

    Z80* z80 = context->pCore->GetZ80();
    if (!z80)
    {
        session.SendResponse("Z80 CPU not available." + std::string(NEWLINE));
        return;
    }

    OpcodeProfiler* profiler = z80->GetOpcodeProfiler();
    if (!profiler)
    {
        session.SendResponse("OpcodeProfiler not available." + std::string(NEWLINE));
        return;
    }

    // Parse subcommand
    std::string subcommand = args[0];
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

    if (subcommand == "opcode")
    {
        if (args.size() < 2)
        {
            ShowProfilerOpcodeHelp(session);
            return;
        }

        std::string action = args[1];
        std::transform(action.begin(), action.end(), action.begin(), ::tolower);

        if (action == "start")
        {
            HandleProfilerOpcodeStart(session, context, profiler);
        }
        else if (action == "stop")
        {
            HandleProfilerOpcodeStop(session, profiler);
        }
        else if (action == "clear")
        {
            HandleProfilerOpcodeClear(session, profiler);
        }
        else if (action == "status")
        {
            HandleProfilerOpcodeStatus(session, profiler);
        }
        else if (action == "counters")
        {
            size_t limit = 50;
            if (args.size() >= 3)
            {
                try { limit = std::stoul(args[2]); } catch (...) { limit = 50; }
            }
            HandleProfilerOpcodeCounters(session, profiler, limit);
        }
        else if (action == "trace")
        {
            size_t count = 100;
            if (args.size() >= 3)
            {
                try { count = std::stoul(args[2]); } catch (...) { count = 100; }
            }
            HandleProfilerOpcodeTrace(session, profiler, count);
        }
        else if (action == "save")
        {
            if (args.size() < 3)
            {
                session.SendResponse("Usage: profiler opcode save <file-path>" + std::string(NEWLINE));
                return;
            }
            HandleProfilerOpcodeSave(session, profiler, args[2]);
        }
        else
        {
            ShowProfilerOpcodeHelp(session);
        }
    }
    else
    {
        ShowProfilerHelp(session);
    }
}

// Helper methods (defined in the header as needed)

void CLIProcessor::ShowProfilerHelp(const ClientSession& session)
{
    std::ostringstream oss;
    oss << "Profiler Commands:" << NEWLINE;
    oss << "  profiler opcode <action>  - Z80 opcode profiling" << NEWLINE;
    oss << NEWLINE;
    oss << "Type 'profiler opcode' for opcode-specific help." << NEWLINE;
    session.SendResponse(oss.str());
}

void CLIProcessor::ShowProfilerOpcodeHelp(const ClientSession& session)
{
    std::ostringstream oss;
    oss << "Opcode Profiler Commands:" << NEWLINE;
    oss << "  profiler opcode start        - Start capture session (clears data)" << NEWLINE;
    oss << "  profiler opcode stop         - Stop capture (data remains)" << NEWLINE;
    oss << "  profiler opcode clear        - Clear all counters and trace" << NEWLINE;
    oss << "  profiler opcode status       - Show profiler status" << NEWLINE;
    oss << "  profiler opcode counters [N] - Show top N opcodes (default: 50)" << NEWLINE;
    oss << "  profiler opcode trace [N]    - Show last N trace entries (default: 100)" << NEWLINE;
    oss << "  profiler opcode save <file>  - Export data to YAML file" << NEWLINE;
    oss << NEWLINE;
    oss << "Note: Enable feature first with 'feature opcodeprofiler on'" << NEWLINE;
    session.SendResponse(oss.str());
}

void CLIProcessor::HandleProfilerOpcodeStart(const ClientSession& session, EmulatorContext* context, OpcodeProfiler* profiler)
{
    // Check if feature is enabled
    if (context->pFeatureManager && !context->pFeatureManager->isEnabled(Features::kOpcodeProfiler))
    {
        session.SendResponse("Error: OpcodeProfiler feature is disabled. Enable with 'feature opcodeprofiler on'" + std::string(NEWLINE));
        return;
    }

    profiler->Start();
    session.SendResponse("Opcode profiler started. Previous data cleared." + std::string(NEWLINE));
}

void CLIProcessor::HandleProfilerOpcodeStop(const ClientSession& session, OpcodeProfiler* profiler)
{
    profiler->Stop();
    session.SendResponse("Opcode profiler stopped. Data available for retrieval." + std::string(NEWLINE));
}

void CLIProcessor::HandleProfilerOpcodeClear(const ClientSession& session, OpcodeProfiler* profiler)
{
    profiler->Clear();
    session.SendResponse("Opcode profiler data cleared." + std::string(NEWLINE));
}

void CLIProcessor::HandleProfilerOpcodeStatus(const ClientSession& session, OpcodeProfiler* profiler)
{
    auto status = profiler->GetStatus();

    std::ostringstream oss;
    oss << "Opcode Profiler Status:" << NEWLINE;
    oss << "  Capturing:         " << (status.capturing ? "YES" : "NO") << NEWLINE;
    oss << "  Total Executions:  " << status.totalExecutions << NEWLINE;
    oss << "  Trace Buffer:      " << status.traceSize << " / " << status.traceCapacity << " entries" << NEWLINE;
    session.SendResponse(oss.str());
}

void CLIProcessor::HandleProfilerOpcodeCounters(const ClientSession& session, OpcodeProfiler* profiler, size_t limit)
{
    auto status = profiler->GetStatus();
    auto topOpcodes = profiler->GetTopOpcodes(limit);

    if (topOpcodes.empty())
    {
        session.SendResponse("No opcode executions recorded." + std::string(NEWLINE));
        return;
    }

    std::ostringstream oss;
    oss << "Opcode Profile (capturing: " << (status.capturing ? "YES" : "NO")
        << ", total: " << status.totalExecutions << ")" << NEWLINE;
    oss << std::string(52, '-') << NEWLINE;
    oss << std::left << std::setw(20) << "Opcode"
        << std::right << std::setw(15) << "Count"
        << std::setw(10) << "%" << NEWLINE;
    oss << std::string(52, '-') << NEWLINE;

    for (const auto& op : topOpcodes)
    {
        double pct = status.totalExecutions > 0 ? (100.0 * op.count / status.totalExecutions) : 0.0;
        oss << std::left << std::setw(20) << op.mnemonic
            << std::right << std::setw(15) << op.count
            << std::setw(9) << std::fixed << std::setprecision(2) << pct << "%" << NEWLINE;
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleProfilerOpcodeTrace(const ClientSession& session, OpcodeProfiler* profiler, size_t count)
{
    auto trace = profiler->GetRecentTrace(count);

    if (trace.empty())
    {
        session.SendResponse("No trace entries recorded." + std::string(NEWLINE));
        return;
    }

    std::ostringstream oss;
    oss << "Recent Opcode Trace (newest first):" << NEWLINE;
    oss << std::string(70, '-') << NEWLINE;
    oss << std::left << std::setw(6) << "Idx"
        << std::setw(8) << "PC"
        << std::setw(10) << "Prefix"
        << std::setw(8) << "Opcode"
        << std::setw(8) << "Flags"
        << std::setw(6) << "A"
        << std::setw(10) << "Frame"
        << "T-state" << NEWLINE;
    oss << std::string(70, '-') << NEWLINE;

    for (size_t i = 0; i < trace.size(); ++i)
    {
        const auto& t = trace[i];
        oss << std::left << std::setw(6) << ("-" + std::to_string(i))
            << std::right << std::hex << std::uppercase
            << "0x" << std::setw(4) << std::setfill('0') << t.pc << "  "
            << "0x" << std::setw(4) << t.prefix << "  "
            << "0x" << std::setw(2) << static_cast<int>(t.opcode) << "    "
            << "0x" << std::setw(2) << static_cast<int>(t.flags) << "  "
            << "0x" << std::setw(2) << static_cast<int>(t.a) << "  "
            << std::dec << std::setfill(' ')
            << std::setw(8) << t.frame << "  "
            << t.tState << NEWLINE;
    }

    session.SendResponse(oss.str());
}

void CLIProcessor::HandleProfilerOpcodeSave(const ClientSession& session, OpcodeProfiler* profiler, const std::string& path)
{
    if (profiler->SaveToFile(path))
    {
        session.SendResponse("Profiler data saved to: " + path + std::string(NEWLINE));
    }
    else
    {
        session.SendResponse("Failed to save profiler data to: " + path + std::string(NEWLINE));
    }
}
