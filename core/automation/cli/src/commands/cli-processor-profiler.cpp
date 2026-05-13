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
#include "emulator/memory/memoryaccesstracker.h"

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
        else if (action == "pause")
        {
            profiler->Pause();
            session.SendResponse("Opcode profiler paused. Data retained." + std::string(NEWLINE));
        }
        else if (action == "resume")
        {
            profiler->Resume();
            session.SendResponse("Opcode profiler resumed." + std::string(NEWLINE));
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
    else if (subcommand == "memory" || subcommand == "mem")
    {
        HandleProfilerMemory(session, args);
    }
    else if (subcommand == "calltrace" || subcommand == "ct")
    {
        HandleProfilerCalltrace(session, args);
    }
    else if (subcommand == "all")
    {
        HandleProfilerAll(session, args);
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
    oss << "  profiler opcode <action>    - Z80 opcode profiling" << NEWLINE;
    oss << "  profiler memory <action>    - Memory access profiling (alias: mem)" << NEWLINE;
    oss << "  profiler calltrace <action> - Call trace profiling (alias: ct)" << NEWLINE;
    oss << "  profiler all <action>       - Control all profilers at once" << NEWLINE;
    oss << NEWLINE;
    oss << "Actions: start, pause, resume, stop, clear, status" << NEWLINE;
    oss << NEWLINE;
    oss << "Type 'profiler <type>' for type-specific help." << NEWLINE;
    session.SendResponse(oss.str());
}

void CLIProcessor::ShowProfilerOpcodeHelp(const ClientSession& session)
{
    std::ostringstream oss;
    oss << "Opcode Profiler Commands:" << NEWLINE;
    oss << "  profiler opcode start        - Start capture session (clears data)" << NEWLINE;
    oss << "  profiler opcode pause        - Pause capture (data retained)" << NEWLINE;
    oss << "  profiler opcode resume       - Resume paused capture" << NEWLINE;
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

// ============================================================================
// Memory Profiler Commands
// ============================================================================

static const char* SessionStateToString(ProfilerSessionState state)
{
    switch (state)
    {
        case ProfilerSessionState::Stopped: return "STOPPED";
        case ProfilerSessionState::Capturing: return "CAPTURING";
        case ProfilerSessionState::Paused: return "PAUSED";
        default: return "UNKNOWN";
    }
}

void CLIProcessor::HandleProfilerMemory(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected." + std::string(NEWLINE));
        return;
    }

    auto* context = emulator->GetContext();
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;

    if (!tracker)
    {
        session.SendResponse("Memory access tracker not available." + std::string(NEWLINE));
        return;
    }

    if (args.size() < 2)
    {
        std::ostringstream oss;
        oss << "Memory Profiler Commands:" << NEWLINE;
        oss << "  profiler memory start  - Start capture session" << NEWLINE;
        oss << "  profiler memory pause  - Pause capture" << NEWLINE;
        oss << "  profiler memory resume - Resume capture" << NEWLINE;
        oss << "  profiler memory stop   - Stop capture" << NEWLINE;
        oss << "  profiler memory clear  - Clear data" << NEWLINE;
        oss << "  profiler memory status - Show status" << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }

    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "start")
    {
        if (context->pFeatureManager)
        {
            context->pFeatureManager->setFeature(Features::kDebugMode, true);
            context->pFeatureManager->setFeature(Features::kMemoryTracking, true);
            tracker->UpdateFeatureCache();
        }
        tracker->StartMemorySession();
        session.SendResponse("Memory profiler started." + std::string(NEWLINE));
    }
    else if (action == "pause")
    {
        tracker->PauseMemorySession();
        session.SendResponse("Memory profiler paused." + std::string(NEWLINE));
    }
    else if (action == "resume")
    {
        tracker->ResumeMemorySession();
        session.SendResponse("Memory profiler resumed." + std::string(NEWLINE));
    }
    else if (action == "stop")
    {
        tracker->StopMemorySession();
        session.SendResponse("Memory profiler stopped." + std::string(NEWLINE));
    }
    else if (action == "clear")
    {
        tracker->ClearMemoryData();
        session.SendResponse("Memory profiler data cleared." + std::string(NEWLINE));
    }
    else if (action == "status")
    {
        std::ostringstream oss;
        oss << "Memory Profiler Status:" << NEWLINE;
        oss << "  Session State: " << SessionStateToString(tracker->GetMemorySessionState()) << NEWLINE;
        oss << "  Capturing:     " << (tracker->IsMemoryCapturing() ? "YES" : "NO") << NEWLINE;
        session.SendResponse(oss.str());
    }
    else
    {
        session.SendResponse("Unknown memory profiler action: " + action + std::string(NEWLINE));
    }
}

// ============================================================================
// Call Trace Profiler Commands
// ============================================================================

void CLIProcessor::HandleProfilerCalltrace(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected." + std::string(NEWLINE));
        return;
    }

    auto* context = emulator->GetContext();
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;

    if (!tracker)
    {
        session.SendResponse("Memory access tracker not available." + std::string(NEWLINE));
        return;
    }

    if (args.size() < 2)
    {
        std::ostringstream oss;
        oss << "Call Trace Profiler Commands:" << NEWLINE;
        oss << "  profiler calltrace start  - Start capture session" << NEWLINE;
        oss << "  profiler calltrace pause  - Pause capture" << NEWLINE;
        oss << "  profiler calltrace resume - Resume capture" << NEWLINE;
        oss << "  profiler calltrace stop   - Stop capture" << NEWLINE;
        oss << "  profiler calltrace clear  - Clear data" << NEWLINE;
        oss << "  profiler calltrace status - Show status" << NEWLINE;
        oss << NEWLINE;
        oss << "Alias: profiler ct <action>" << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }

    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "start")
    {
        if (context->pFeatureManager)
        {
            context->pFeatureManager->setFeature(Features::kDebugMode, true);
            context->pFeatureManager->setFeature(Features::kCallTrace, true);
            tracker->UpdateFeatureCache();
        }
        tracker->StartCalltraceSession();
        session.SendResponse("Call trace profiler started." + std::string(NEWLINE));
    }
    else if (action == "pause")
    {
        tracker->PauseCalltraceSession();
        session.SendResponse("Call trace profiler paused." + std::string(NEWLINE));
    }
    else if (action == "resume")
    {
        tracker->ResumeCalltraceSession();
        session.SendResponse("Call trace profiler resumed." + std::string(NEWLINE));
    }
    else if (action == "stop")
    {
        tracker->StopCalltraceSession();
        session.SendResponse("Call trace profiler stopped." + std::string(NEWLINE));
    }
    else if (action == "clear")
    {
        tracker->ClearCalltraceData();
        session.SendResponse("Call trace profiler data cleared." + std::string(NEWLINE));
    }
    else if (action == "status")
    {
        std::ostringstream oss;
        oss << "Call Trace Profiler Status:" << NEWLINE;
        oss << "  Session State: " << SessionStateToString(tracker->GetCalltraceSessionState()) << NEWLINE;
        oss << "  Capturing:     " << (tracker->IsCalltraceCapturing() ? "YES" : "NO") << NEWLINE;
        auto* buffer = tracker->GetCallTraceBuffer();
        if (buffer)
        {
            oss << "  Entry Count:   " << buffer->GetCount() << NEWLINE;
            oss << "  Capacity:      " << buffer->GetCapacity() << NEWLINE;
        }
        session.SendResponse(oss.str());
    }
    else
    {
        session.SendResponse("Unknown call trace profiler action: " + action + std::string(NEWLINE));
    }
}

// ============================================================================
// Unified (All) Profiler Commands
// ============================================================================

void CLIProcessor::HandleProfilerAll(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected." + std::string(NEWLINE));
        return;
    }

    auto* context = emulator->GetContext();
    auto* z80 = context && context->pCore ? context->pCore->GetZ80() : nullptr;
    auto* memory = context && context->pMemory ? context->pMemory : nullptr;
    auto* tracker = memory ? &memory->GetAccessTracker() : nullptr;
    auto* opcodeProfiler = z80 ? z80->GetOpcodeProfiler() : nullptr;

    if (args.size() < 2)
    {
        std::ostringstream oss;
        oss << "Unified Profiler Commands (all profilers at once):" << NEWLINE;
        oss << "  profiler all start  - Start all profilers" << NEWLINE;
        oss << "  profiler all pause  - Pause all profilers" << NEWLINE;
        oss << "  profiler all resume - Resume all profilers" << NEWLINE;
        oss << "  profiler all stop   - Stop all profilers" << NEWLINE;
        oss << "  profiler all clear  - Clear all profiler data" << NEWLINE;
        oss << "  profiler all status - Show status of all profilers" << NEWLINE;
        session.SendResponse(oss.str());
        return;
    }

    std::string action = args[1];
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "start")
    {
        if (context->pFeatureManager)
        {
            context->pFeatureManager->setFeature(Features::kDebugMode, true);
            context->pFeatureManager->setFeature(Features::kMemoryTracking, true);
            context->pFeatureManager->setFeature(Features::kCallTrace, true);
            context->pFeatureManager->setFeature(Features::kOpcodeProfiler, true);
            if (tracker) tracker->UpdateFeatureCache();
            if (z80) z80->UpdateFeatureCache();
        }
        if (tracker)
        {
            tracker->StartMemorySession();
            tracker->StartCalltraceSession();
        }
        if (opcodeProfiler) opcodeProfiler->Start();
        session.SendResponse("All profilers started." + std::string(NEWLINE));
    }
    else if (action == "pause")
    {
        if (tracker)
        {
            tracker->PauseMemorySession();
            tracker->PauseCalltraceSession();
        }
        if (opcodeProfiler) opcodeProfiler->Pause();
        session.SendResponse("All profilers paused." + std::string(NEWLINE));
    }
    else if (action == "resume")
    {
        if (tracker)
        {
            tracker->ResumeMemorySession();
            tracker->ResumeCalltraceSession();
        }
        if (opcodeProfiler) opcodeProfiler->Resume();
        session.SendResponse("All profilers resumed." + std::string(NEWLINE));
    }
    else if (action == "stop")
    {
        if (tracker)
        {
            tracker->StopMemorySession();
            tracker->StopCalltraceSession();
        }
        if (opcodeProfiler) opcodeProfiler->Stop();
        session.SendResponse("All profilers stopped." + std::string(NEWLINE));
    }
    else if (action == "clear")
    {
        if (tracker)
        {
            tracker->ClearMemoryData();
            tracker->ClearCalltraceData();
        }
        if (opcodeProfiler) opcodeProfiler->Clear();
        session.SendResponse("All profiler data cleared." + std::string(NEWLINE));
    }
    else if (action == "status")
    {
        std::ostringstream oss;
        oss << "All Profilers Status:" << NEWLINE;
        oss << std::string(40, '-') << NEWLINE;
        
        if (tracker)
        {
            oss << "Memory:    " << SessionStateToString(tracker->GetMemorySessionState()) << NEWLINE;
            oss << "CallTrace: " << SessionStateToString(tracker->GetCalltraceSessionState()) << NEWLINE;
        }
        if (opcodeProfiler)
        {
            oss << "Opcode:    " << SessionStateToString(opcodeProfiler->GetSessionState()) << NEWLINE;
        }
        session.SendResponse(oss.str());
    }
    else
    {
        session.SendResponse("Unknown action: " + action + std::string(NEWLINE));
    }
}

