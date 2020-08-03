/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include "shell_except.h"
#include "shell_app.h"
#include "shell_module.h"
#include "iterators/shell_component_iterator.h"
#include "help_formatters/shell_help_formatter.h"
#include "help_formatters/shell_help_formatter_factory.h"
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <cstdlib>

namespace ownshell {

class DefaultShellHooks: public ShellHooks
{
    public:
        DefaultShellHooks(ShellEnv* env, void* user_data): ShellHooks(env, user_data) {};
        virtual void on_error(runtime_error* error, ShellComponent * component) {
            component = component; /* to avoid warnings */
            cerr << "Error: " << error->what() << endl ;
        };
        virtual void on_info(string msg, ShellComponent * component) {
            component = component; /* to avoid warnings */
            cerr << msg << endl ;
        };
        virtual void on_critical(string msg) {
            cerr << msg << endl ;
        };
};

ShellApp::ShellApp(ShellEnv* env, string name, string prompt, ShellComponent* root)
{
    env_ = env;
    name_ = name;
    prompt_ = prompt;
    root_ = new ShellModule(env, "/", "");
    root_->add(root);
    exit_cmd_ = "exit";
    help_cmd_ = "help";
    welcome_banner_ = "";
    top_help_msg_ = "";
    hooks_ = new DefaultShellHooks(env, (void *) NULL);
}

ShellApp::~ShellApp()
{
    if (root_)
        delete root_;
    if (hooks_)
        delete hooks_;
}

string ShellApp::getTopHelp(void)
{
    ShellHelpFormatter* formatter = env_->getHelpFormatter();
    if (top_help_msg_ != "")
        return formatter->formatTopHelp(top_help_msg_);
    else
        return formatter->formatTopHelp("*** " + name_ + " help ***\n"\
               "Commands must provide one (or more!) <module> name(s) and a <command> name such as:\n"\
               "    device list \n"\
               "    extra utilities gettime \n"\
               "    device output set dev1 ON \n"\
               "To display module commands, type help <module 1>...<module N>\n"\
               "To display command help, type help <module 1>...<module N> <command>");
}

void ShellApp::displayPrompt(void)
{
    cout << prompt_ << " " ;
}

void ShellApp::displayWelcomeBanner(void)
{
    if (welcome_banner_ != "")
        cout << welcome_banner_ << endl ;
}

vector<string> ShellApp::getCmdLineTokens(void)
{
    string full_cmd;
    getline(cin, full_cmd);
    string buf;
    stringstream ss(full_cmd);
    vector<string> tokens;

    while (ss >> buf) {
        tokens.push_back(buf);
    }
    return tokens;
}

void ShellApp::displayHelp(vector<string> tokens)
{
    string help;
    ShellComponent* component;

    /* help was typed without any additional arg, display general help */
    if (tokens.size() == 1) {
        help = getTopHelp();
        help += root_->getHelp();
    }
    else {
        try {
            component = root_->findComponentFromTokens(tokens);
            help = component->getHelp();
        } catch (shell_except& e) {
            hooks_->on_error(&e, component);
            hooks_->on_info("Type help for general help", component);
        } catch (...) {
            hooks_->on_critical("help not found. Type help for general help");
        };
    }
    cout << help << endl;
}

void ShellApp::loop(void)
{
    displayWelcomeBanner();
    while (1) {

        displayPrompt();
        vector<string> tokens = getCmdLineTokens();

        /* Check for exit command */
        if ((tokens.size() >= 1) && (tokens[0] == exit_cmd_))
            exit(0);

        /* Check for help commands */
        if ((tokens.size() >= 1) && (tokens[0] == help_cmd_)) {
            displayHelp(tokens);
            continue;
        }

        try {
            ShellComponent* component = root_->findComponentFromTokens(tokens);
            /* We now need to separate args from module(s)/cmd path
             * Just play with number of parents */
            unsigned int nb = component->getParentsNb();
            vector<string> args(tokens.begin() + nb, tokens.end());
            cout << component->run(args) << endl;
        } catch (runtime_error e) {
            hooks_->on_error(&e, (ShellComponent*) NULL);
        }
    };
}

string ShellApp::getMan(string format)
{
    string man;
    ShellHelpFormatter* formatter_backup = env_->getHelpFormatter();
    ShellHelpFormatter* formatter  = ShellHelpFormatterFactory::createFormatterFromFormat(format);
    env_->setHelpFormatter(formatter);

    ShellComponentIterator* it = root_->createIterator();
    ShellComponent * component;

    man = getTopHelp() + "\n";
    while (it->hasNext()) {
        component = it->next();
        if (!component->getChildrenNb()) { /* only get leafs which are actual commands
                                         (and not modules containers)*/
            man += component->getFullPath() + ": " + component->getHelp() + "\n";
        }
    }
    env_->setHelpFormatter(formatter_backup);
    delete formatter;

    return man;
}

} // namespace ownshell
