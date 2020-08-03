/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_HELP_FORMATTER_H
#define _OWNSHELL_I_HELP_FORMATTER_H

#include <string>

using namespace std;

namespace ownshell {

/**
 *  ShellHelpFormatter is an interface to be implemented to format help
 */

class ShellHelpFormatter
{
    public:
        ShellHelpFormatter(string name) { name_ = name; };
        virtual ~ShellHelpFormatter() {};

        virtual string formatTopHelp(string top_help) = 0;
        virtual string formatTitle(string title) = 0;
        virtual string formatSubTitle() = 0;
        virtual string formatModuleHelp(string name, string description) = 0;
        virtual string formatModuleCmdHelp(string name, string description) = 0;
        virtual string formatCmdHelp(string cmd_help) = 0;
        virtual string formatWarning(string warning) = 0;
    protected:
        string name_;
};

} // namespace ownshell

#endif
