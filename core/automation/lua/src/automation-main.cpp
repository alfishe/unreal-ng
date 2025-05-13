#include <iostream>
#include <sol/sol.hpp>
#include <cassert>
#include <emulator/emulator.h>

int main()
{
    {
        std::cout << "=== opening a state ===" << std::endl;

        sol::state lua;
        // open some common libraries
        lua.open_libraries(sol::lib::base, sol::lib::package);
        lua.script("print('bark bark bark!')");

        std::cout << std::endl;
    }

    {
        sol::state lua;
        int x = 0;
        lua.set_function("beep", [&x]
        {
            std::cout << "Wow!!!" << std::endl;
            x++;
        });
        lua.script("beep()");
        assert(x == 1);
    }

    Emulator emulator;
    bool result = emulator.Init();
    if (result)
    {
        emulator.Start();
    }
}