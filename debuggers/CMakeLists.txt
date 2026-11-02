# The debuggers of unreal-ng (README.md): the Qt debugger and the terminal debuggers
if (UNREAL_DEBUGGER_QT)
    if (TARGET unrealng::core)
        message(STATUS ">>> Building the Qt debugger (unreal-debugger)")
        add_subdirectory(qt)
    else ()
        message(WARNING "The Qt debugger is skipped: it needs the core (unrealng::core)")
    endif ()
endif ()

if (UNREAL_TUI_DEBUGGERS)
    message(STATUS ">>> Building the terminal debuggers (unreal-tui-debugger, unreal-tui-debugger-tsconf)")
    add_subdirectory(tui)
endif ()
