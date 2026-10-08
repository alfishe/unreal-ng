find_package(PkgConfig REQUIRED)
pkg_check_modules(DBUS REQUIRED IMPORTED_TARGET dbus-1)
steamdeck_poc(p18-logind-sleep NOCORE main.cpp)
target_link_libraries(p18-logind-sleep PRIVATE PkgConfig::DBUS)
