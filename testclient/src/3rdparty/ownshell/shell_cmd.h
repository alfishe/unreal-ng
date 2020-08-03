/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_CMD_H
#define _OWNSHELL_I_CMD_H

#include <string>
#include <list>
#include "shell_component.h"
#include "shell_env.h"

using namespace std;

namespace ownshell {

/**
 *  ShellCmd is an interface that has to be implemented by concrete commands
 *  It is also a ShellComponent as it can be part of a composite
 */

class ShellCmd: public ShellComponent
{
    public:
        ShellCmd(ShellEnv* env, string name, string description, string help = "") : ShellComponent(env, name, description) {
            detailed_help_ = help;
        };
        virtual ~ShellCmd(){};
        virtual string getHelp(void);
        virtual string run(vector<string> args);

        virtual ShellComponentIterator* createIterator();
    private:
        string detailed_help_;

};

} // namespace ownshell

#endif
