/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_MODULE_H
#define _OWNSHELL_I_MODULE_H

#include <string>
#include <map>
#include "shell_env.h"
#include "shell_component.h"

using namespace std;

namespace ownshell {

/**
 *
 * A ShellModule is a composite (includes ShellComponents)
 */

class ShellModule: public ShellComponent
{
    public:
        ShellModule(ShellEnv* env, string name, string description) : ShellComponent(env, name, description){};
        virtual ~ShellModule() {};

        virtual string run(vector<string> args);
        virtual string getHelp();

        virtual void add(ShellComponent * component);
        virtual void remove(ShellComponent * component);
        unsigned int getChildrenNb(void);
        virtual ShellComponent* findComponentFromTokens(vector<string> tokens);
        virtual ShellComponent* findComponentByName(string name);
        virtual ShellComponent* getChildAt(unsigned int rank);

        virtual ShellComponentIterator* createIterator();
    private:
        map<string, ShellComponent* > children_;
        virtual ShellComponent* findComponent(ShellComponent * component);
};

} // namespace ownshell
#endif
