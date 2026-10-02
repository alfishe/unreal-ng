#include "emulator/io/network/networkspec.h"

#include <cctype>
#include <sstream>

namespace networkspec
{
bool ParseCards(const std::string& text, uint8_t& mask, std::string& error)
{
    mask = 0;
    std::stringstream in(text);
    std::string item;
    while (std::getline(in, item, ','))
    {
        std::string t;
        for (char c : item)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
                t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        if (t.empty() || t == "NONE")
            continue;
        if (t == "ZXNETUSB")
            mask |= kCardZxNetUsb;
        else if (t == "ZXWIFI" || t == "ZX-WIFI")
            mask |= kCardZxWifi;
        else if (t == "ATM2IOESP" || t == "ATM2-IO-ESP")
            mask |= kCardAtm2IoEsp;
        else
        {
            error = "unknown card '" + item + "' (NONE | ZXNETUSB | ZXWIFI | ATM2IOESP, a list with ',')";
            return false;
        }
    }
    return true;
}

std::string CardsToString(uint8_t mask)
{
    std::string out;
    if (mask & kCardZxNetUsb)
        out = "ZXNETUSB";
    if (mask & kCardZxWifi)
        out += std::string(out.empty() ? "" : ",") + "ZXWIFI";
    if (mask & kCardAtm2IoEsp)
        out += std::string(out.empty() ? "" : ",") + "ATM2IOESP";
    return out.empty() ? "NONE" : out;
}
}  // namespace networkspec
