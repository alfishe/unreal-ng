# cmake -DIN=<binary> -DOUT=<header> -DSYM=<symbol> -P embed.cmake
# Writes `static const unsigned char SYM[] = {...}; static const size_t SYM_size = N;`
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" len)
math(EXPR size "${len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "#pragma once\n#include <cstddef>\nstatic const unsigned char ${SYM}[] = {${bytes}};\nstatic const size_t ${SYM}_size = ${size};\n")
