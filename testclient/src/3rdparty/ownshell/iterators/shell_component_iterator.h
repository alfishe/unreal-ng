/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_COMPONENT_ITERATOR_H
#define _OWNSHELL_I_COMPONENT_ITERATOR_H

#include "../shell_component.h"
using namespace std;

namespace ownshell {

/**
 *  ShellComponentIterator is an interface to be implemented by concrete iterators to traverse ShellComponent agregates
 */

class ShellComponentIterator
{
    public:
        ShellComponentIterator(ShellComponent* component) { root_ = component;
                                                            name_ = component->getName();};
        virtual ~ShellComponentIterator() {};

        virtual ShellComponent* next() = 0;
        virtual bool hasNext() = 0;
        virtual void reset() = 0;
        string getName() { return name_; };
        virtual ShellComponent* getRootChildAt(unsigned int rank) { return root_->getChildAt(rank); };
        virtual unsigned int getRootChildrenNb() { return root_->getChildrenNb(); };
        virtual unsigned int getPosition() { return 0; };
        virtual void incPosition() {};
    protected:
        ShellComponent* root_;
        string name_;
};

} // namespace ownshell

#endif
