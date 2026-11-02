// POC 041: the helpers of ecat's emulator/utils.h its debugger calls
#pragma once

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "emulator/core.h"

inline std::string hex_str(unsigned int value, int width)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*X", width, value);
    return std::string(buf);
}

inline std::string oct_str(unsigned int value, int width)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%0*o", width, value);
    return std::string(buf);
}

// ecat's number syntax: $ hex, & octal, # binary, _ decimal, a K suffix; else default_base (hex when 0)
inline unsigned int parse_numeric_value(std::string str, unsigned int default_base = 0)
{
    if (str.empty()) throw std::invalid_argument("Empty numeric value");
    std::string s = str;
    for (char &c : s) c = (char)toupper((unsigned char)c);
    int base = default_base ? (int)default_base : 16;
    switch (s[0]) {
    case '$': base = 16; s.erase(0, 1); break;
    case '&': base = 8; s.erase(0, 1); break;
    case '#': base = 2; s.erase(0, 1); break;
    case '_': base = 10; s.erase(0, 1); break;
    default: break;
    }
    if (s.empty()) throw std::invalid_argument("Invalid numeric value: " + str);
    unsigned int mult = 1;
    if (s.back() == 'K') { mult = 1024; s.pop_back(); }
    char * end = nullptr;
    const unsigned long v = strtoul(s.c_str(), &end, base);
    if (end == nullptr || *end != 0) throw std::invalid_argument("Invalid numeric value: " + str);
    return (unsigned int)v * mult;
}
