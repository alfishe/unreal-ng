/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_INFO_H
#define _OWNSHELL_I_INFO_H

#include <string>

#define OWNSHELL_NAME "ownShell Library"
#define OWNSHELL_VERSION_MAJOR 0
#define OWNSHELL_VERSION_MINOR 2
#define OWNSHELL_VERSION_PATCH 0
#define OWNSHELL_VERSION_EXTRA ""

using namespace std;

namespace ownshell {

/**
 *  ShellInfo provides information about the library
 */

class ShellInfo
{
    public:
        static string getVersion();
        static string getName();
};

} // namespace ownshell

#endif
