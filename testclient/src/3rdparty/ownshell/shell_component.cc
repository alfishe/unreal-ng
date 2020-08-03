/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_except.h"
#include "shell_component.h"

namespace ownshell {

ShellComponent::ShellComponent(ShellEnv* env, string name, string description)
{
    env_ = env;
    name_ = name;
    description_ = description;
    parent_ = NULL;
}

string ShellComponent::getHelp(void)
{
    return getDescription();
}

unsigned int ShellComponent::getChildrenNb(void)
{
    return 0;
}

void ShellComponent::add(ShellComponent* component)
{
    component = component;
    throw shell_except_unsupported("Operation not supported");
}

void ShellComponent::remove(ShellComponent* component)
{
    component = component;
    throw shell_except_unsupported("Operation not supported");
}

ShellComponent* ShellComponent::findComponentFromTokens(vector<string> tokens)
{
    tokens = tokens;
    throw shell_except_not_found("Component not found");
}

ShellComponent* ShellComponent::findComponentByName(string name)
{
    name = name;
    throw shell_except_not_found("Component not found");
}

string ShellComponent::run(vector<string> args)
{
    args = args;
    throw shell_except_unsupported("Operation not supported");
}

unsigned int ShellComponent::getParentsNb()
{
    if (!parent_) {
        return 0;
    } else {
        return parent_->getParentsNb() + 1;
    }
}

string ShellComponent::getFullPath()
{
    list<string> names;
    string full_path;
    ShellComponent* current = this;

    do {
        names.push_back(current->getName());
        current = current->getParent();
    } while(current->getParent() != NULL);

    for(std::list<string>::iterator it = names.begin(); it != names.end(); ++it) {
        full_path += *it + " ";
    }
    full_path.erase(full_path.begin() + full_path.size() - 1); /* remove last space */
    return full_path;
}

ShellComponent* ShellComponent::getChildAt(unsigned int rank)
{
    rank = rank;
    return NULL;
}

ShellComponentIterator* ShellComponent::createIterator()
{
    throw shell_except_unsupported("Cannot create iterator with component base class");
}


} // namespace ownshell
