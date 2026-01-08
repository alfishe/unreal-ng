// CLI Settings and Feature Commands
// Extracted from cli-processor.cpp - 2026-01-08

#include "cli-processor.h"

#include <base/featuremanager.h>
#include <emulator/emulator.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>

#include <iostream>
#include <sstream>

// HandleSetting - lines 3137-3388
void CLIProcessor::HandleSetting(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the selected emulator
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    // Get emulator context
    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Unable to access emulator context.") + NEWLINE);
        return;
    }

    CONFIG& config = context->config;

    // If no arguments, show all settings (list)
    if (args.empty())
    {
        std::stringstream ss;
        ss << "Current Settings:" << NEWLINE;
        ss << "==================" << NEWLINE;
        ss << NEWLINE;

        ss << "I/O Acceleration:" << NEWLINE;
        ss << "  fast_tape     = " << (config.tape_traps ? "on" : "off") << "  (Fast tape loading)" << NEWLINE;
        ss << "  fast_disk     = " << (config.wd93_nodelay ? "on" : "off") << "  (Fast disk I/O - no WD1793 delays)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Disk Interface:" << NEWLINE;
        ss << "  trdos_present = " << (config.trdos_present ? "on" : "off") << "  (TR-DOS Beta Disk interface)"
           << NEWLINE;
        ss << "  trdos_traps   = " << (config.trdos_traps ? "on" : "off") << "  (TR-DOS traps)" << NEWLINE;
        ss << NEWLINE;

        ss << "Performance & Speed:" << NEWLINE;
        ss << "  speed         = ";
        if (config.turbo_mode)
            ss << "unlimited";
        else
            ss << (int)config.speed_multiplier << "x";
        ss << "  (CPU speed multiplier: 1, 2, 4, 8, 16, unlimited)" << NEWLINE;
        ss << "  turbo_audio   = " << (config.turbo_mode_audio ? "on" : "off") << "  (Enable audio in turbo mode)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Use: setting <name> <value>  to change a setting" << NEWLINE;
        ss << "Example: setting fast_tape on" << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    // Get setting name
    std::string settingName = args[0];
    std::transform(settingName.begin(), settingName.end(), settingName.begin(), ::tolower);

    // Handle special commands
    if (settingName == "list")
    {
        // Redirect to show all settings (same as no arguments)
        std::stringstream ss;
        ss << "Current Settings:" << NEWLINE;
        ss << "==================" << NEWLINE;
        ss << NEWLINE;

        ss << "I/O Acceleration:" << NEWLINE;
        ss << "  fast_tape     = " << (config.tape_traps ? "on" : "off") << "  (Fast tape loading)" << NEWLINE;
        ss << "  fast_disk     = " << (config.wd93_nodelay ? "on" : "off") << "  (Fast disk I/O - no WD1793 delays)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Disk Interface:" << NEWLINE;
        ss << "  trdos_present = " << (config.trdos_present ? "on" : "off") << "  (TR-DOS Beta Disk interface)"
           << NEWLINE;
        ss << "  trdos_traps   = " << (config.trdos_traps ? "on" : "off") << "  (TR-DOS traps)" << NEWLINE;
        ss << NEWLINE;

        ss << "Performance & Speed:" << NEWLINE;
        ss << "  speed         = ";
        if (config.turbo_mode)
            ss << "unlimited";
        else
            ss << (int)config.speed_multiplier << "x";
        ss << "  (CPU speed multiplier: 1, 2, 4, 8, 16, unlimited)" << NEWLINE;
        ss << "  turbo_audio   = " << (config.turbo_mode_audio ? "on" : "off") << "  (Enable audio in turbo mode)"
           << NEWLINE;
        ss << NEWLINE;

        ss << "Use: setting <name> <value>  to change a setting" << NEWLINE;
        ss << "Example: setting fast_tape on" << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    // If only setting name provided, show current value
    if (args.size() == 1)
    {
        std::stringstream ss;

        if (settingName == "fast_tape")
        {
            ss << "fast_tape = " << (config.tape_traps ? "on" : "off") << NEWLINE;
            ss << "Description: Fast tape loading (bypasses audio emulation)" << NEWLINE;
        }
        else if (settingName == "fast_disk")
        {
            ss << "fast_disk = " << (config.wd93_nodelay ? "on" : "off") << NEWLINE;
            ss << "Description: Fast disk I/O (removes WD1793 controller delays)" << NEWLINE;
        }
        else if (settingName == "trdos_present")
        {
            ss << "trdos_present = " << (config.trdos_present ? "on" : "off") << NEWLINE;
            ss << "Description: Enable Beta128 TR-DOS disk interface" << NEWLINE;
        }
        else if (settingName == "trdos_traps")
        {
            ss << "trdos_traps = " << (config.trdos_traps ? "on" : "off") << NEWLINE;
            ss << "Description: Use TR-DOS traps for faster disk operations" << NEWLINE;
        }
        else if (settingName == "speed" || settingName == "max_cpu_speed")
        {
            ss << "speed = ";
            if (config.turbo_mode)
                ss << "unlimited" << NEWLINE;
            else
                ss << (int)config.speed_multiplier << "x" << NEWLINE;
            ss << "Description: Maximum CPU speed multiplier (1, 2, 4, 8, 16, unlimited)" << NEWLINE;
        }
        else if (settingName == "turbo_audio")
        {
            ss << "turbo_audio = " << (config.turbo_mode_audio ? "on" : "off") << NEWLINE;
            ss << "Description: Enable audio generation in turbo mode (high pitch)" << NEWLINE;
        }
        else
        {
            ss << "Error: Unknown setting '" << settingName << "'" << NEWLINE;
            ss << "Use 'setting' to see all available settings" << NEWLINE;
        }

        session.SendResponse(ss.str());
        return;
    }

    // Setting name and value provided - change the setting
    std::string value = args[1];
    std::string valueLower = value;
    std::transform(valueLower.begin(), valueLower.end(), valueLower.begin(), ::tolower);

    // Apply the setting
    std::stringstream ss;

    // Handle non-boolean settings first
    if (settingName == "speed" || settingName == "max_cpu_speed")
    {
        if (valueLower == "unlimited" || valueLower == "max")
        {
            emulator->EnableTurboMode(config.turbo_mode_audio);
            ss << "Setting changed: speed = unlimited (Turbo Mode)" << NEWLINE;
        }
        else
        {
            try
            {
                int m = std::stoi(valueLower);
                if (m == 1 || m == 2 || m == 4 || m == 8 || m == 16)
                {
                    emulator->DisableTurboMode();
                    emulator->SetSpeedMultiplier(m);
                    ss << "Setting changed: speed = " << m << "x" << NEWLINE;
                }
                else
                {
                    ss << "Error: Invalid speed multiplier " << m << ". Use 1, 2, 4, 8, 16, or unlimited" << NEWLINE;
                }
            }
            catch (...)
            {
                ss << "Error: Invalid value '" << value << "'. Use 1, 2, 4, 8, 16, or unlimited" << NEWLINE;
            }
        }
        session.SendResponse(ss.str());
        return;
    }

    // Parse boolean value for remaining settings
    bool boolValue = false;
    if (valueLower == "on" || valueLower == "1" || valueLower == "true" || valueLower == "yes")
    {
        boolValue = true;
    }
    else if (valueLower == "off" || valueLower == "0" || valueLower == "false" || valueLower == "no")
    {
        boolValue = false;
    }
    else
    {
        session.SendResponse(std::string("Error: Invalid value '") + value +
                             "'. Use: on/off, true/false, 1/0, or yes/no" + NEWLINE);
        return;
    }

    if (settingName == "fast_tape")
    {
        config.tape_traps = boolValue ? 1 : 0;
        ss << "Setting changed: fast_tape = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "Fast tape loading is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else if (settingName == "fast_disk")
    {
        config.wd93_nodelay = boolValue;
        ss << "Setting changed: fast_disk = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "Fast disk I/O is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else if (settingName == "trdos_present")
    {
        config.trdos_present = boolValue;
        ss << "Setting changed: trdos_present = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "TR-DOS interface is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
        ss << "Note: Restart emulator for this change to take effect" << NEWLINE;
    }
    else if (settingName == "trdos_traps")
    {
        config.trdos_traps = boolValue;
        ss << "Setting changed: trdos_traps = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "TR-DOS traps are now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else if (settingName == "turbo_audio")
    {
        config.turbo_mode_audio = boolValue;
        if (config.turbo_mode)
        {
            // Re-enable turbo with/without audio to apply immediately
            emulator->EnableTurboMode(boolValue);
        }
        ss << "Setting changed: turbo_audio = " << (boolValue ? "on" : "off") << NEWLINE;
        ss << "Audio in turbo mode is now " << (boolValue ? "enabled" : "disabled") << NEWLINE;
    }
    else
    {
        ss << "Error: Unknown setting '" << settingName << "'" << NEWLINE;
        ss << "Use 'setting' to see all available settings" << NEWLINE;
    }

    session.SendResponse(ss.str());
}

// HandleFeature - lines 2729-2873
void CLIProcessor::HandleFeature(const ClientSession& session, const std::vector<std::string>& args)
{
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse("No emulator selected. Use 'select <id>' or 'status' to see available emulators.");
        return;
    }
    auto* featureManager = emulator->GetFeatureManager();
    if (!featureManager)
    {
        session.SendResponse("FeatureManager not available for this emulator.");
        return;
    }

    std::ostringstream out;

    if (!args.empty() && args[0] == "save")
    {
        featureManager->saveToFile("features.ini");
        out << "Feature settings saved to features.ini." << NEWLINE;
        session.SendResponse(out.str());
        return;
    }

    // Feature command logic (moved from FeatureManager)
    if (args.empty() || (args.size() == 1 && args[0].empty()))
    {
        // Print all features in a table
        const int name_width = 15;
        const int state_width = 7;
        const int mode_width = 10;
        const int desc_width = 60;
        const std::string separator =
            "----------------------------------------------------------------------------------------------------------"
            "--------";

        out << separator << NEWLINE;
        out << "| " << std::left << std::setw(name_width) << "Name"
            << "| " << std::left << std::setw(state_width) << "State"
            << "| " << std::left << std::setw(mode_width) << "Mode"
            << "| " << std::left << "Description" << NEWLINE;
        out << separator << NEWLINE;

        for (const auto& f : featureManager->listFeatures())
        {
            std::string state_str = f.enabled ? Features::kStateOn : Features::kStateOff;
            std::string mode_str = f.mode.empty() ? "" : f.mode;

            out << "| " << std::left << std::setw(name_width) << f.id << "| " << std::left << std::setw(state_width)
                << state_str << "| " << std::left << std::setw(mode_width) << mode_str << "| " << std::left
                << f.description << NEWLINE;
        }
        out << separator << NEWLINE;

        session.SendResponse(out.str());
        return;
    }
    else if (args.size() == 2)
    {
        const std::string& featureName = args[0];
        const std::string& action = args[1];

        if (action == Features::kStateOn)
        {
            if (featureManager->setFeature(featureName, true))
            {
                out << "Feature '" << featureName << "' enabled." << NEWLINE;
                session.SendResponse(out.str());
                return;
            }
            else
            {
                out << "Error: Unknown feature '" << featureName << "'." << NEWLINE;
                out << "Available features:" << NEWLINE;
                for (const auto& f : featureManager->listFeatures())
                {
                    out << "  " << f.id;
                    if (!f.alias.empty())
                    {
                        out << " (alias: " << f.alias << ")";
                    }
                    out << NEWLINE;
                }
            }
        }
        else if (action == Features::kStateOff)
        {
            if (featureManager->setFeature(featureName, false))
            {
                out << "Feature '" << featureName << "' disabled." << NEWLINE;
                session.SendResponse(out.str());
                return;
            }
            else
            {
                out << "Error: Unknown feature '" << featureName << "'." << NEWLINE;
                out << "Available features:" << NEWLINE;
                for (const auto& f : featureManager->listFeatures())
                {
                    out << "  " << f.id;
                    if (!f.alias.empty())
                    {
                        out << " (alias: " << f.alias << ")";
                    }
                    out << NEWLINE;
                }
            }
        }
        else
        {
            out << "Invalid action. Use 'on' or 'off'." << NEWLINE;
        }
    }
    else if (args.size() == 3 && args[1] == "mode")
    {
        std::string feature = args[0];
        std::string mode = args[2];
        if (featureManager->setMode(feature, mode))
        {
            out << "Feature '" << feature << "' mode set to '" << mode << "'" << NEWLINE;
            session.SendResponse(out.str());
            return;
        }
        else
        {
            out << "Error: Unknown feature '" << feature << "'." << NEWLINE;
            out << "Available features:" << NEWLINE;
            for (const auto& f : featureManager->listFeatures())
            {
                out << "  " << f.id;
                if (!f.alias.empty())
                {
                    out << " (alias: " << f.alias << ")";
                }
                out << NEWLINE;
            }
        }
    }

    // Usage/help output - only shown for errors or invalid commands
    out << "Usage:" << NEWLINE << "  feature <feature> on|off" << NEWLINE << "  feature <feature> mode <mode>"
        << NEWLINE << "  feature save" << NEWLINE << "  feature" << NEWLINE;
    session.SendResponse(out.str());
}

