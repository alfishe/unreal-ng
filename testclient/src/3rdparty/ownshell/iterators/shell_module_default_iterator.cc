/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_module_default_iterator.h"

//#define __LOCAL_DEFAULT_IT_DEBUG

#ifdef __LOCAL_DEFAULT_IT_DEBUG
#include <iostream>
#endif

using namespace std;

namespace ownshell {

ShellModuleDefaultIterator::~ShellModuleDefaultIterator()
{
    /* delete remaining children iterators when destroying top one */
    for(std::list<ShellComponentIterator*>::iterator it = iterators_.begin(); it != iterators_.end(); ++it) {
        if (*it != this)
            delete *it;
    }
}

void ShellModuleDefaultIterator::reset()
{
    position_ = 0;
    iterators_.push_back(this);
}

unsigned int ShellModuleDefaultIterator::getPosition()
{
    return position_;
}

void ShellModuleDefaultIterator::incPosition()
{
    position_++;
}

ShellComponent* ShellModuleDefaultIterator::next()
{
    ShellComponent* current;

    if (hasNext()) {
        ShellComponentIterator* it = iterators_.back();
#ifdef __LOCAL_DEFAULT_IT_DEBUG
        cout << "next with position: " << it->getPosition() << endl;
        cout << "iterator: " << it->getName() << endl;
#endif
        current = it->getRootChildAt(it->getPosition());
        if (current->getChildrenNb()) {
#ifdef __LOCAL_DEFAULT_IT_DEBUG
            cout << "Adding iterator for : " << current->getName() << endl;
#endif
            iterators_.push_back(current->createIterator());
        }
        it->incPosition();
#ifdef __LOCAL_DEFAULT_IT_DEBUG
        cout << "Found component: " << current->getName() << endl << endl;
#endif
        return current;
    }
    return NULL;
}


bool ShellModuleDefaultIterator::hasNext()
{
    if (iterators_.empty())
        return false;
    else {
        ShellComponentIterator* it = iterators_.back();
#ifdef __LOCAL_DEFAULT_IT_DEBUG
        cout << "hasNext with position: " << it->getPosition() << endl;
        cout << "iterator: " << it->getName() << endl;
        cout << "children number: " << it->getRootChildrenNb() << endl;
#endif
        if (it->getPosition() < it->getRootChildrenNb()) {
            return true;
        } else {
#ifdef __LOCAL_DEFAULT_IT_DEBUG
            cout << "Removing iterator: " << it->getName() << endl;
#endif
            delete iterators_.back();
            iterators_.pop_back();
            return false;
        }
    }
}

} // namespace ownshell
