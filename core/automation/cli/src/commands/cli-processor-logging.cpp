// CLI Logging Control Commands
// Per-module log level management via ModuleLogger

#include <common/modulelogger.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "cli-processor.h"

void CLIProcessor::HandleLogging(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    EmulatorContext* context = emulator->GetContext();
    if (!context || !context->pModuleLogger)
    {
        session.SendResponse(std::string("Error: ModuleLogger not available.") + NEWLINE);
        return;
    }

    ModuleLogger* logger = context->pModuleLogger;
    std::ostringstream out;

    // No arguments: show full logger state
    if (args.empty())
    {
        LoggerLevel globalLevel = logger->GetLevel();
        out << "Logging State" << NEWLINE;
        out << "=============" << NEWLINE;
        out << "Global level: " << ModuleLogger::GetLevelApiName(static_cast<int>(globalLevel)) << NEWLINE;
        out << NEWLINE;

        const int nameW = 14;
        const int enabledW = 9;
        const int levelW = 10;
        const int maskW = 8;
        const std::string sep = std::string(nameW + enabledW + levelW + maskW + 10, '-');

        out << sep << NEWLINE;
        out << "| " << std::left << std::setw(nameW) << "Module"
            << "| " << std::left << std::setw(enabledW) << "Enabled"
            << "| " << std::left << std::setw(levelW) << "Level"
            << "| " << std::left << std::setw(maskW) << "Mask"
            << "|" << NEWLINE;
        out << sep << NEWLINE;

        const LoggerSettings& settings = logger->GetSettings();
        for (int m = 1; m < MODULE_COUNT; m++)
        {
            const char* name = ModuleLogger::GetModuleApiName(m);
            if (!name)
                continue;

            bool enabled = (settings.modules & (1u << m)) != 0;
            uint8_t rawLevel = settings.moduleLevels[m];
            std::string levelStr = (rawLevel == 0) ? "inherit" : ModuleLogger::GetLevelApiName(rawLevel);

            char maskBuf[8];
            snprintf(maskBuf, sizeof(maskBuf), "0x%04X", settings.submodules[m]);

            out << "| " << std::left << std::setw(nameW) << name
                << "| " << std::left << std::setw(enabledW) << (enabled ? "on" : "off")
                << "| " << std::left << std::setw(levelW) << levelStr
                << "| " << std::left << std::setw(maskW) << maskBuf
                << "|" << NEWLINE;
        }
        out << sep << NEWLINE;

        session.SendResponse(out.str());
        return;
    }

    // Parse subcommands
    std::string subcmd = args[0];
    std::transform(subcmd.begin(), subcmd.end(), subcmd.begin(), ::tolower);

    // logging level <level>
    if (subcmd == "level")
    {
        if (args.size() < 2)
        {
            LoggerLevel cur = logger->GetLevel();
            out << "Global level: " << ModuleLogger::GetLevelApiName(static_cast<int>(cur)) << NEWLINE;
            session.SendResponse(out.str());
            return;
        }

        std::string levelName = args[1];
        std::transform(levelName.begin(), levelName.end(), levelName.begin(), ::tolower);
        int levelId = ModuleLogger::LevelNameToId(levelName.c_str());
        if (levelId < 0)
        {
            out << "Error: Unknown level '" << args[1] << "'" << NEWLINE;
            out << "Valid levels:";
            for (int i = 0; i < ModuleLogger::GetLevelCount(); i++)
            {
                const char* n = ModuleLogger::GetLevelApiName(i);
                if (n) out << " " << n;
            }
            out << NEWLINE;
            session.SendResponse(out.str());
            return;
        }

        logger->SetLoggingLevel(static_cast<LoggerLevel>(levelId));
        out << "Global level set to: " << ModuleLogger::GetLevelApiName(levelId) << NEWLINE;
        session.SendResponse(out.str());
        return;
    }

    // logging modules
    if (subcmd == "modules")
    {
        out << "Available modules:" << NEWLINE;
        for (int m = 1; m < MODULE_COUNT; m++)
        {
            const char* name = ModuleLogger::GetModuleApiName(m);
            const char* display = ModuleLogger::GetModuleName(m);
            if (name && display)
                out << "  " << std::left << std::setw(14) << name << " (" << display << ")" << NEWLINE;
        }
        session.SendResponse(out.str());
        return;
    }

    // logging levels
    if (subcmd == "levels")
    {
        out << "Available levels:" << NEWLINE;
        for (int i = 0; i < ModuleLogger::GetLevelCount(); i++)
        {
            const char* name = ModuleLogger::GetLevelApiName(i);
            const char* display = ModuleLogger::GetLevelName(i);
            if (name && display)
                out << "  " << std::left << std::setw(10) << name << " (" << display << ")" << NEWLINE;
        }
        session.SendResponse(out.str());
        return;
    }

    // logging module <name> [on|off] | [level <level>] | [mask <value>]
    if (subcmd == "module")
    {
        if (args.size() < 2)
        {
            out << "Usage: logging module <name> [on|off|level <level>|mask <hex>]" << NEWLINE;
            session.SendResponse(out.str());
            return;
        }

        std::string modName = args[1];
        std::transform(modName.begin(), modName.end(), modName.begin(), ::tolower);
        int modId = ModuleLogger::ModuleNameToId(modName.c_str());
        if (modId < 0)
        {
            out << "Error: Unknown module '" << args[1] << "'" << NEWLINE;
            out << "Use 'logging modules' to list available modules." << NEWLINE;
            session.SendResponse(out.str());
            return;
        }

        auto module = static_cast<PlatformModulesEnum>(modId);
        const LoggerSettings& settings = logger->GetSettings();

        // No action = show state
        if (args.size() == 2)
        {
            bool enabled = (settings.modules & (1u << modId)) != 0;
            uint8_t rawLevel = settings.moduleLevels[modId];
            std::string levelStr = (rawLevel == 0) ? "inherit" : ModuleLogger::GetLevelApiName(rawLevel);

            out << "Module: " << ModuleLogger::GetModuleApiName(modId) << NEWLINE;
            out << "  Enabled: " << (enabled ? "on" : "off") << NEWLINE;
            out << "  Level:   " << levelStr << NEWLINE;
            char maskBuf[8];
            snprintf(maskBuf, sizeof(maskBuf), "0x%04X", settings.submodules[modId]);
            out << "  Mask:    " << maskBuf << NEWLINE;
            session.SendResponse(out.str());
            return;
        }

        std::string action = args[2];
        std::transform(action.begin(), action.end(), action.begin(), ::tolower);

        // on/off
        if (action == "on" || action == "off")
        {
            bool enable = (action == "on");
            uint16_t mask = settings.submodules[modId];
            logger->SetModuleState(module, enable, mask);
            out << "Module '" << ModuleLogger::GetModuleApiName(modId) << "' "
                << (enable ? "enabled" : "disabled") << NEWLINE;
            session.SendResponse(out.str());
            return;
        }

        // level <name>
        if (action == "level")
        {
            if (args.size() < 4)
            {
                out << "Usage: logging module " << modName << " level <trace|debug|info|warning|error|none|inherit>"
                    << NEWLINE;
                session.SendResponse(out.str());
                return;
            }

            std::string lvl = args[3];
            std::transform(lvl.begin(), lvl.end(), lvl.begin(), ::tolower);

            if (lvl == "inherit" || lvl == "0")
            {
                logger->SetModuleLogLevel(module, static_cast<LoggerLevel>(0));
                out << "Module '" << ModuleLogger::GetModuleApiName(modId) << "' level set to: inherit" << NEWLINE;
            }
            else
            {
                int levelId = ModuleLogger::LevelNameToId(lvl.c_str());
                if (levelId < 0)
                {
                    out << "Error: Unknown level '" << args[3] << "'" << NEWLINE;
                    session.SendResponse(out.str());
                    return;
                }
                logger->SetModuleLogLevel(module, static_cast<LoggerLevel>(levelId));
                out << "Module '" << ModuleLogger::GetModuleApiName(modId) << "' level set to: "
                    << ModuleLogger::GetLevelApiName(levelId) << NEWLINE;
            }
            session.SendResponse(out.str());
            return;
        }

        // mask <hex>
        if (action == "mask")
        {
            if (args.size() < 4)
            {
                out << "Usage: logging module " << modName << " mask <hex-value>" << NEWLINE;
                session.SendResponse(out.str());
                return;
            }

            std::string maskStr = args[3];
            try
            {
                unsigned long maskVal = std::stoul(maskStr, nullptr, 0);  // auto-detect base
                if (maskVal > 0xFFFF)
                {
                    out << "Error: Mask must be 0x0000-0xFFFF" << NEWLINE;
                    session.SendResponse(out.str());
                    return;
                }

                bool enabled = (settings.modules & (1u << modId)) != 0;
                logger->SetModuleState(module, enabled, static_cast<uint16_t>(maskVal));

                char maskBuf[8];
                snprintf(maskBuf, sizeof(maskBuf), "0x%04X", static_cast<unsigned>(maskVal));
                out << "Module '" << ModuleLogger::GetModuleApiName(modId) << "' mask set to: " << maskBuf << NEWLINE;
            }
            catch (...)
            {
                out << "Error: Invalid hex value '" << maskStr << "'" << NEWLINE;
            }
            session.SendResponse(out.str());
            return;
        }

        // Unknown action
        out << "Usage: logging module <name> [on|off|level <level>|mask <hex>]" << NEWLINE;
        session.SendResponse(out.str());
        return;
    }

    // Unknown subcommand
    out << "Usage:" << NEWLINE;
    out << "  logging                             - Show full logger state" << NEWLINE;
    out << "  logging level <level>               - Set global log level" << NEWLINE;
    out << "  logging module <name>               - Show module state" << NEWLINE;
    out << "  logging module <name> on|off         - Enable/disable module" << NEWLINE;
    out << "  logging module <name> level <level>  - Set per-module level" << NEWLINE;
    out << "  logging module <name> mask <hex>     - Set submodule bitmask" << NEWLINE;
    out << "  logging modules                     - List available modules" << NEWLINE;
    out << "  logging levels                      - List available levels" << NEWLINE;
    session.SendResponse(out.str());
}
