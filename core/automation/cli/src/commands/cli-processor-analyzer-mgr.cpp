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
        ss << "  analyzer list                   - List all registered analyzers" << NEWLINE;
        ss << "  analyzer enable <name>          - Activate an analyzer" << NEWLINE;
        ss << "  analyzer disable <name>         - Deactivate an analyzer" << NEWLINE;
        ss << "  analyzer status [<name>]        - Show analyzer status" << NEWLINE;
        ss << "  analyzer <name> events [--limit=N]  - Get captured events" << NEWLINE;
        ss << "  analyzer <name> clear           - Clear event buffer" << NEWLINE;
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

    // ==================== enable <name> ====================
    if (subcommand == "enable")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: analyzer enable requires a name.") + NEWLINE);
            return;
        }

        std::string name = args[1];
        bool success = manager->activate(name);
        
        if (success)
        {
            session.SendResponse("Analyzer '" + name + "' activated." + NEWLINE);
        }
        else
        {
            session.SendResponse("Error: Failed to activate '" + name + "'. Is it registered?" + NEWLINE);
        }
        return;
    }

    // ==================== disable <name> ====================
    if (subcommand == "disable")
    {
        if (args.size() < 2)
        {
            session.SendResponse(std::string("Error: analyzer disable requires a name.") + NEWLINE);
            return;
        }

        std::string name = args[1];
        bool success = manager->deactivate(name);
        
        if (success)
        {
            session.SendResponse("Analyzer '" + name + "' deactivated." + NEWLINE);
        }
        else
        {
            session.SendResponse("Error: Failed to deactivate '" + name + "'." + NEWLINE);
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
