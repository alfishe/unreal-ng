/* Copyright (C) 2015 the ownShell authors and contributors
 * <see AUTHORS file>
 *
 * This module is part of ownShell and is released under
 * the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#ifndef _OWNSHELL_I_APP_H
#define _OWNSHELL_I_APP_H

#include "shell_except.h"
#include "shell_env.h"
#include "shell_component.h"
#include <vector>

using namespace std;

namespace ownshell {

class ShellHooks
{
    public:
        ShellHooks(ShellEnv* env, void* user_data) {
            env_ = env;
            user_data_ = user_data;
        };
        virtual ~ShellHooks() {};
        virtual void on_error(runtime_error* error, ShellComponent * component) {
            error = error;
            component = component;
        };
        virtual void on_info(string msg, ShellComponent * component) {
            msg = msg;
            component = component;
        };
        virtual void on_critical(string msg) {
            msg = msg;
        };
    protected:
        ShellEnv* env_;
        void * user_data_;
};

/**
 * A ShellApp implements an interactive shell
 */
class ShellApp
{
    public:
        ShellApp(ShellEnv* env, string name, string prompt, ShellComponent* root);
        ~ShellApp();
        void loop(void);
        void setExitCommand(string name) { exit_cmd_ = name; };
        void setHooks(ShellHooks* hooks) { hooks_ = hooks; };
        void setHelpCommand(string name) { help_cmd_ = name; };
        void setTopHelp(string msg) { top_help_msg_ = msg; };
        void setWelcomeBanner(string banner) { welcome_banner_ = banner; };
        string getMan(string format);

    private:
        ShellEnv* env_;
        ShellComponent* root_;
        string name_;
        string welcome_banner_;
        string prompt_;
        string exit_cmd_;
        string help_cmd_;
        string top_help_msg_;
        ShellHooks* hooks_;

        void displayWelcomeBanner(void);
        void displayPrompt(void);
        void displayError(string error);
        void displayInfo(string msg);
        void displayHelp(std::vector<string> tokens);
        string getTopHelp(void);
        vector<string> getCmdLineTokens(void);
};

} // namespace ownshell
#endif
