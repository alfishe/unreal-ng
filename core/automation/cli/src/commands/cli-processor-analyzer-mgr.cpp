// CLI Analyzer Manager Commands
// Unified interface to control all analyzers via AnalyzerManager

#include "cli-processor.h"

#include <common/stringhelper.h>
#include <debugger/debugmanager.h>
#include <debugger/analyzers/analyzermanager.h>
#include <debugger/analyzers/trdos/trdosanalyzer.h>
#include <emulator/emulatorcontext.h>

#include <iostream>
#include <sstream>
#include <iomanip>

void CLIProcessor::HandleAnalyzer(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pDebugManager)
    {
        session.SendResponse(std::string("Error: Debug manager not available.") + NEWLINE);
        return;
    }

    AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
    if (!manager)
    {
        session.SendResponse(std::string("Error: Analyzer manager not initialized.") + NEWLINE);
        return;
    }

    // No arguments - show help
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Analyzer commands:" << NEWLINE;
        ss << "  analyzer list                        - List all registered analyzers" << NEWLINE;
        ss << "  analyzer activate <name>             - Activate analyzer and start new session" << NEWLINE;
        ss << "  analyzer deactivate <name>           - Deactivate analyzer and close session" << NEWLINE;
        ss << "  analyzer pause <name>                - Pause event capture (keep session open)" << NEWLINE;
        ss << "  analyzer resume <name>               - Resume event capture" << NEWLINE;
        ss << "  analyzer status [<name>]             - Show analyzer status" << NEWLINE;
        ss << "  analyzer <name> events [--limit=N]   - Get semantic events" << NEWLINE;
        ss << "  analyzer <name> raw fdc [--limit=N]  - Get raw FDC events" << NEWLINE;
        ss << "  analyzer <name> raw breakpoints      - Get raw breakpoint events" << NEWLINE;
        ss << "  analyzer <name> clear                - Clear event buffer" << NEWLINE;
        ss << "" << NEWLINE;
        ss << "Legacy aliases:" << NEWLINE;
        ss << "  analyzer enable <name>               - Alias for 'activate'" << NEWLINE;
        ss << "  analyzer disable <name>              - Alias for 'deactivate'" << NEWLINE;
        session.SendResponse(ss.str());
        return;
    }

    std::string subcommand = args[0];

    // ==================== list ====================
    if (subcommand == "list")
    {
        auto analyzers = manager->getRegisteredAnalyzers();
        
        std::stringstream ss;
        ss << "Registered analyzers:" << NEWLINE;
        
        if (analyzers.empty())
        {
            ss << "  (none)" << NEWLINE;
        }
        else
        {
            for (const auto& name : analyzers)
            {
                bool active = manager->isActive(name);
                ss << "  " << std::left << std::setw(12) << name 
                   << (active ? "(enabled)" : "(disabled)") << NEWLINE;
            }
        }
        
        session.SendResponse(ss.str());
        return;
    }

    // ==================== activate|enable <name> ====================
    if (subcommand == "activate" || subcommand == "enable")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: analyzer activate requires a name.") + NEWLINE);
            return;
        }

        std::string name = args[1];
        bool success = manager->activate(name);
        
        if (success)
        {
            // Also clear buffers on activation to start fresh session
            if (name == "trdos")
            {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(manager->getAnalyzer(name));
                if (trdos) trdos->clear();
            }
            session.SendResponse("Analyzer '" + name + "' activated (new session started)." + NEWLINE);
        }
        else
        {
            session.SendResponse("Error: Failed to activate '" + name + "'. Is it registered?" + NEWLINE);
        }
        return;
    }

    // ==================== deactivate|disable <name> ====================
    if (subcommand == "deactivate" || subcommand == "disable")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: analyzer deactivate requires a name.") + NEWLINE);
            return;
        }

        std::string name = args[1];
        bool success = manager->deactivate(name);
        
        if (success)
        {
            session.SendResponse("Analyzer '" + name + "' deactivated (session closed)." + NEWLINE);
        }
        else
        {
            session.SendResponse("Error: Failed to deactivate '" + name + "'." + NEWLINE);
        }
        return;
    }

    // ==================== pause <name> ====================
    if (subcommand == "pause")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: analyzer pause requires a name.") + NEWLINE);
            return;
        }

        std::string name = args[1];
        // For now, pause = deactivate but keep data
        // TODO: Implement proper pause (stop capture without deactivating breakpoints)
        bool success = manager->deactivate(name);
        
        if (success)
        {
            session.SendResponse("Analyzer '" + name + "' paused (session data preserved)." + NEWLINE);
        }
        else
        {
            session.SendResponse("Error: Failed to pause '" + name + "'." + NEWLINE);
        }
        return;
    }

    // ==================== resume <name> ====================
    if (subcommand == "resume")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: analyzer resume requires a name.") + NEWLINE);
            return;
        }

        std::string name = args[1];
        // Resume = reactivate without clearing
        bool success = manager->activate(name);
        
        if (success)
        {
            session.SendResponse("Analyzer '" + name + "' resumed." + NEWLINE);
        }
        else
        {
            session.SendResponse("Error: Failed to resume '" + name + "'." + NEWLINE);
        }
        return;
    }

    // ==================== status [<name>] ====================
    if (subcommand == "status")
    {
        std::stringstream ss;
        
        if (args.size() >= 2)
        {
            // Status for specific analyzer
            std::string name = args[1];
            
            if (!manager->hasAnalyzer(name))
            {
                session.SendResponse("Error: Unknown analyzer '" + name + "'." + NEWLINE);
                return;
            }
            
            bool active = manager->isActive(name);
            ss << "Analyzer: " << name << NEWLINE;
            ss << "Status: " << (active ? "enabled" : "disabled") << NEWLINE;
            
            // TRDOSAnalyzer specific stats
            if (name == "trdos")
            {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(manager->getAnalyzer(name));
                if (trdos)
                {
                    ss << "State: ";
                    switch (trdos->getState())
                    {
                        case TRDOSAnalyzerState::IDLE: ss << "IDLE"; break;
                        case TRDOSAnalyzerState::IN_TRDOS: ss << "IN_TRDOS"; break;
                        case TRDOSAnalyzerState::IN_COMMAND: ss << "IN_COMMAND"; break;
                        case TRDOSAnalyzerState::IN_SECTOR_OP: ss << "IN_SECTOR_OP"; break;
                        case TRDOSAnalyzerState::IN_CUSTOM: ss << "IN_CUSTOM"; break;
                        default: ss << "UNKNOWN"; break;
                    }
                    ss << NEWLINE;
                    ss << "Events: " << trdos->getEventCount() << NEWLINE;
                    ss << "Total produced: " << trdos->getTotalEventsProduced() << NEWLINE;
                    ss << "Evicted: " << trdos->getTotalEventsEvicted() << NEWLINE;
                }
            }
        }
        else
        {
            // Status for all analyzers
            auto analyzers = manager->getRegisteredAnalyzers();
            for (const auto& name : analyzers)
            {
                bool active = manager->isActive(name);
                ss << name << ": " << (active ? "enabled" : "disabled") << NEWLINE;
            }
        }
        
        session.SendResponse(ss.str());
        return;
    }

    // ==================== <name> events / clear ====================
    // Check if first arg is an analyzer name
    if (manager->hasAnalyzer(subcommand))
    {
        std::string analyzerName = subcommand;
        
        if (args.size() < 2)
        {
            session.SendResponse("Error: Missing subcommand for '" + analyzerName + "'." + NEWLINE +
                               "Usage: analyzer " + analyzerName + " events|clear" + NEWLINE);
            return;
        }

        std::string action = args[1];

        // ==================== <name> events ====================
        if (action == "events")
        {
            if (analyzerName == "trdos")
            {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(manager->getAnalyzer(analyzerName));
                if (!trdos)
                {
                    session.SendResponse(std::string("Error: TRDOSAnalyzer not available.") + NEWLINE);
                    return;
                }

                // Parse --limit option
                size_t limit = 50;  // Default
                for (size_t i = 2; i < args.size(); i++)
                {
                    if (args[i].substr(0, 8) == "--limit=")
                    {
                        limit = std::stoul(args[i].substr(8));
                    }
                }

                auto events = trdos->getEvents();
                std::stringstream ss;
                
                size_t start = (events.size() > limit) ? events.size() - limit : 0;
                for (size_t i = start; i < events.size(); i++)
                {
                    ss << events[i].format() << NEWLINE;
                }
                
                if (events.empty())
                {
                    ss << "(no events captured)" << NEWLINE;
                }
                else if (start > 0)
                {
                    ss << "... (" << start << " earlier events not shown, use --limit=N)" << NEWLINE;
                }
                
                session.SendResponse(ss.str());
            }
            else
            {
                session.SendResponse("Error: events not implemented for '" + analyzerName + "'." + NEWLINE);
            }
            return;
        }

        // ==================== <name> raw fdc/breakpoints ====================
        if (action == "raw")
        {
            if (args.size() < 3)
            {
                session.SendResponse("Error: analyzer " + analyzerName + " raw requires a subcommand (fdc|breakpoints)." + NEWLINE);
                return;
            }

            std::string rawType = args[2];

            if (analyzerName == "trdos")
            {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(manager->getAnalyzer(analyzerName));
                if (!trdos)
                {
                    session.SendResponse(std::string("Error: TRDOSAnalyzer not available.") + NEWLINE);
                    return;
                }

                std::stringstream ss;

                if (rawType == "fdc")
                {
                    // Parse --limit option
                    size_t limit = 50;
                    for (size_t i = 3; i < args.size(); i++)
                    {
                        if (args[i].substr(0, 8) == "--limit=")
                        {
                            limit = std::stoul(args[i].substr(8));
                        }
                    }

                    auto events = trdos->getRawFDCEvents();
                    size_t start = (events.size() > limit) ? events.size() - limit : 0;

                    ss << "Raw FDC Events (" << events.size() << " total):" << NEWLINE;
                    for (size_t i = start; i < events.size(); i++)
                    {
                        const auto& e = events[i];
                        ss << "[" << std::setw(10) << e.tstate << "] "
                           << "Port=" << std::hex << std::setw(2) << std::setfill('0') << (int)e.port << std::dec
                           << " " << (e.direction ? "OUT" : "IN")
                           << " Val=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)e.value << std::dec
                           << " PC=0x" << std::hex << std::setw(4) << std::setfill('0') << e.pc << std::dec
                           << NEWLINE;
                    }

                    if (events.empty())
                    {
                        ss << "(no raw FDC events captured)" << NEWLINE;
                    }
                    else if (start > 0)
                    {
                        ss << "... (" << start << " earlier events not shown, use --limit=N)" << NEWLINE;
                    }
                }
                else if (rawType == "breakpoints")
                {
                    auto events = trdos->getRawBreakpointEvents();
                    ss << "Raw Breakpoint Events (" << events.size() << " total):" << NEWLINE;

                    for (const auto& e : events)
                    {
                        ss << "[" << std::setw(10) << e.tstate << "] "
                           << "BP=0x" << std::hex << std::setw(4) << std::setfill('0') << e.address
                           << " PC=0x" << std::setw(4) << std::setfill('0') << e.pc
                           << " SP=0x" << std::setw(4) << std::setfill('0') << e.sp << std::dec
                           << NEWLINE;
                    }

                    if (events.empty())
                    {
                        ss << "(no raw breakpoint events captured)" << NEWLINE;
                    }
                }
                else
                {
                    session.SendResponse("Error: Unknown raw type '" + rawType + "'. Use 'fdc' or 'breakpoints'." + NEWLINE);
                    return;
                }

                session.SendResponse(ss.str());
            }
            else
            {
                session.SendResponse("Error: raw data not implemented for '" + analyzerName + "'." + NEWLINE);
            }
            return;
        }

        // ==================== <name> clear ====================
        if (action == "clear")
        {
            if (analyzerName == "trdos")
            {
                TRDOSAnalyzer* trdos = dynamic_cast<TRDOSAnalyzer*>(manager->getAnalyzer(analyzerName));
                if (trdos)
                {
                    trdos->clear();
                    session.SendResponse(std::string("Events cleared.") + NEWLINE);
                }
                else
                {
                    session.SendResponse(std::string("Error: TRDOSAnalyzer not available.") + NEWLINE);
                }
            }
            else
            {
                session.SendResponse("Error: clear not implemented for '" + analyzerName + "'." + NEWLINE);
            }
            return;
        }

        session.SendResponse("Error: Unknown action '" + action + "' for analyzer '" + analyzerName + "'." + NEWLINE);
        return;
    }

    // Unknown subcommand
    session.SendResponse("Error: Unknown analyzer command: " + subcommand + NEWLINE +
                        "Use 'analyzer' to see available commands." + NEWLINE);
}
