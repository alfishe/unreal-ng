/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_default_help_formatter.h"

namespace ownshell {


string ShellHelpDefaultFormatter::formatTopHelp(string top_help)
{
    return top_help;
}

string ShellHelpDefaultFormatter::formatTitle(string title)
{
    return title;
}

string ShellHelpDefaultFormatter::formatSubTitle()
{
    return "\n\nModules ([+]) and commands:\n";
}

string ShellHelpDefaultFormatter::formatModuleHelp(string name, string description)
{
    string help = "\t[+] ";
    help += name + ": " + description + "\n";
    return help;
}

string ShellHelpDefaultFormatter::formatModuleCmdHelp(string name, string description)
{
    string help = "\t  - ";
    help += name + ": " + description + "\n";
    return help;
}

string ShellHelpDefaultFormatter::formatCmdHelp(string cmd_help)
{
    return cmd_help;
}

string ShellHelpDefaultFormatter::formatWarning(string warning)
{
    return "/!\\ " + warning + "\n\n";
}


} // namespace ownshell
