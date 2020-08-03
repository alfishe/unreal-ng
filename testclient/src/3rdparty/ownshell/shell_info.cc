/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_info.h"
#include<sstream>

namespace ownshell {

template <typename T>
string to_string(T value)
{
    ostringstream os;
    os << value;
    return os.str();
}

string ShellInfo::getVersion(void)
{
    string version;
    version = to_string(OWNSHELL_VERSION_MAJOR);
    version += ".";
    version += to_string(OWNSHELL_VERSION_MINOR);
    version += ".";
    version += to_string(OWNSHELL_VERSION_PATCH);
    version += OWNSHELL_VERSION_EXTRA;

    return version;
}

string ShellInfo::getName(void)
{
    return OWNSHELL_NAME;
}

} //namespace ownshell
