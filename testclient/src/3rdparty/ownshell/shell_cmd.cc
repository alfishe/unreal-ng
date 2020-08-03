/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_cmd.h"
#include "help_formatters/shell_help_formatter.h"
#include "iterators/shell_component_null_iterator.h"

namespace ownshell {

string ShellCmd::getHelp(void)
{
    ShellHelpFormatter* formatter = env_->getHelpFormatter();
    if (detailed_help_ != "")
        return formatter->formatCmdHelp(detailed_help_);
    else
        return formatter->formatCmdHelp(getDescription());
}

string ShellCmd::run(vector<string> args)
{
    args = args;
    return "";
}

ShellComponentIterator* ShellCmd::createIterator()
{
    return new ShellComponentNullIterator(this);
}


} // namespace ownshell
