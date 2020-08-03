/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_COMPONENT_NULL_ITERATOR_H
#define _OWNSHELL_I_COMPONENT_NULL_ITERATOR_H

#include "shell_component_iterator.h"

using namespace std;

namespace ownshell {

class ShellComponentNullIterator : public ShellComponentIterator
{
    public:
        ShellComponentNullIterator(ShellComponent* component) : ShellComponentIterator(component) {};
        virtual ~ShellComponentNullIterator() {};

        virtual ShellComponent* next() { return NULL; };
        virtual bool hasNext() { return 0; };
        virtual void reset() {};
};

} // namespace ownshell

#endif
