/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_help_formatter_factory.h"
#include "shell_default_help_formatter.h"
#include "../shell_except.h"

namespace ownshell {

ShellHelpFormatter* ShellHelpFormatterFactory::createFormatterFromFormat(string format)
{
    if (format == "txt") {
        return new ShellHelpDefaultFormatter();
    } else {
        throw shell_except_not_found("Format not supported");
    }
}

} // namespace ownshell
