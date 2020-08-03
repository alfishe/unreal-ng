/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_HELP_DEFAULT_FORMATTER_H
#define _OWNSHELL_I_HELP_DEFAULT_FORMATTER_H

#include <string>
#include "shell_help_formatter.h"

using namespace std;

namespace ownshell {

/**
 *  ShellHelpDefaultFormatter is a simple text formatter implementing the ShellHelpFormatter interface
 */

class ShellHelpDefaultFormatter: public ShellHelpFormatter
{
    public:
        ShellHelpDefaultFormatter() : ShellHelpFormatter("default") {};
        virtual ~ShellHelpDefaultFormatter() {};

        virtual string formatTopHelp(string top_help);
        virtual string formatTitle(string title);
        virtual string formatSubTitle();
        virtual string formatModuleHelp(string name, string description);
        virtual string formatModuleCmdHelp(string name, string description);
        virtual string formatCmdHelp(string cmd_help);
        virtual string formatWarning(string warning);
};

} // namespace ownshell

#endif
