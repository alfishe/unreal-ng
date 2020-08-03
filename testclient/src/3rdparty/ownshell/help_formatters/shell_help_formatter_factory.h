/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_HELP_FORMATTER_FACTORY_H
#define _OWNSHELL_I_HELP_FORMATTER_FACTORY_H

#include <string>
#include "shell_help_formatter.h"

using namespace std;

namespace ownshell {

/**
 *  ShellHelpFormatterFactory creates ShellFormatters
 */

class ShellHelpFormatterFactory
{
    public:
        ShellHelpFormatterFactory() {};
        virtual ~ShellHelpFormatterFactory() {};

        static ShellHelpFormatter* createFormatterFromFormat(string format);
};

} // namespace ownshell

#endif
