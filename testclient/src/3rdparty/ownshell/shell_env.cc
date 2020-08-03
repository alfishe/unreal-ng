/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_except.h"
#include "shell_env.h"
#include "help_formatters/shell_default_help_formatter.h"

namespace ownshell {

class ShellEnvDataEntry
{
    public:
        string name_;
        void* data_;
        ShellEnvDataEntry(string name, void* data);
};

ShellEnvDataEntry::ShellEnvDataEntry(string name, void* data)
{
    name_ = name;
    data_ = data;
}

ShellEnv::ShellEnv(string name)
{
    formatter_ = new ShellHelpDefaultFormatter();
    name_ = name;
}

ShellEnv::~ShellEnv()
{
    if (formatter_)
        delete formatter_;
}

void ShellEnv::addEntry(string name, void* entry)
{
    entries_.push_back(new ShellEnvDataEntry(name, entry));
}

void ShellEnv::removeEntry(string name)
{
    for (list<ShellEnvDataEntry *>::iterator it = entries_.begin(); it != entries_.end(); ) {
        if ((*it)->name_ == name) {
            it = entries_.erase(it);
            break;
        } else {
            ++it;
        }
    }
}

void * ShellEnv::getEntry(string name)
{
    void * found = 0;
    ShellEnvDataEntry * entry;
    for (list<ShellEnvDataEntry *>::iterator it = entries_.begin(); it != entries_.end(); ++it) {
        entry = (*it);
        if (entry->name_ == name)
            found = entry->data_;
    }
    if (!found)
        throw shell_except_not_found("Environment entry not found");
    return found;
}

unsigned int ShellEnv::getEntriesNumber(void)
{
    return entries_.size();
}

} // namespace ownshell
