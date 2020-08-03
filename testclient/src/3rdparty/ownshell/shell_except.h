/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_EXCEPT_H
#define _OWNSHELL_I_EXCEPT_H

#include <stdexcept>

using namespace std;

namespace ownshell {

/**
  shell_except is the base class for all Shell library exceptions
*/
class shell_except: public runtime_error
{
    public:
        shell_except(const char *w): runtime_error(w) {}
};

class shell_except_not_found: public shell_except
{
    public:
        shell_except_not_found(const char *w): shell_except(w) {}
};

class shell_except_already: public shell_except
{
    public:
        shell_except_already(const char *w): shell_except(w) {}
};

class shell_except_unsupported: public shell_except
{
    public:
        shell_except_unsupported(const char *w): shell_except(w) {}
};

} //namespace ownshell
#endif
