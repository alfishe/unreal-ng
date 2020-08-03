/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_colored_help_formatter.h"

namespace ownshell {

#define COLOR_CYAN (string) "\033[0;36m"
#define COLOR_RED (string) "\033[0;31m"
#define COLOR_GREEN (string) "\033[0;32m"
#define COLOR_YELLOW (string) "\033[0;33m"
#define COLOR_BLUE (string) "\033[0;34m"
#define COLOR_PURPLE (string) "\033[0;35m"
#define COLOR_NORMAL (string) "\033[0m"

string ShellHelpColoredFormatter::backToNormal()
{
    return COLOR_NORMAL;
};

string ShellHelpColoredFormatter::formatTopHelp(string top_help)
{
    return COLOR_BLUE + top_help + backToNormal();
}

string ShellHelpColoredFormatter::formatTitle(string title)
{
    return COLOR_PURPLE + title + backToNormal();
}

string ShellHelpColoredFormatter::formatSubTitle()
{
    return COLOR_CYAN + "\n\nModules ([+]) and commands:\n" + backToNormal();
}

string ShellHelpColoredFormatter::formatModuleHelp(string name, string description)
{
    string help = "\t[+] ";
    help += COLOR_GREEN + name + COLOR_NORMAL + ": " + description + "\n";
    return help + backToNormal();
}

string ShellHelpColoredFormatter::formatModuleCmdHelp(string name, string description)
{
    string help = "\t  - ";
    help += COLOR_YELLOW + name + COLOR_NORMAL + + ": " + description + "\n";
    return help + backToNormal();
}

string ShellHelpColoredFormatter::formatCmdHelp(string cmd_help)
{
    return cmd_help + backToNormal();
}

string ShellHelpColoredFormatter::formatWarning(string warning)
{
    return COLOR_RED + warning + "\n\n" + backToNormal();
}


} // namespace ownshell
