#include "emulator/io/serial/esp/atdialect.h"

#include <cstdio>

namespace atdialect
{

std::string ErrCodeLine(uint32_t code)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "ERR CODE:0x%08x\r\n", static_cast<unsigned>(code));
    return buf;
}

const Traits& TraitsOf(EspModule::Firmware firmware)
{
    static const Traits nonOs = [] {
        Traits t;
        return t;
    }();
    // ESP32 AT 2.2.0: the forms this module always gave it (SYSSTORE, SYSLOG; the 2.x CWMODE)
    static const Traits esp32 = [] {
        Traits t;
        t.sysStore = true;
        t.sysLog = true;
        return t;
    }();
    // ESP8266 ESP-AT 2.2.2.0 (Espressif's build; the Sprinter kit's own image is one)
    static const Traits at222 = [] {
        Traits t;
        t.suffixForms = false;
        t.sysStore = true;
        t.sysLog = true;
        t.errCodes = true;
        t.joinErrorForm = true;
        t.joinQuery2x = true;
        t.cwState = true;
        t.cipState = true;
        t.quotedDomain = true;
        t.lapAllFields = true;
        t.ipdOneShot = true;
        t.recvLenBlanks = true;
        t.ping2x = true;
        t.recvData2x = true;
        return t;
    }();
    // "2.2.1" as the Sprinter kit describes its binary: ESP-AT 2.2.1 replies, the _CUR / _DEF tokens, no
    // SYSSTORE, no passive receive, no CIPSTATE (2.2.2.0 added it)
    static const Traits at221 = [] {
        Traits t = at222;
        t.suffixForms = true;
        t.sysStore = false;
        t.passiveReceive = false;
        t.cipState = false;
        return t;
    }();
    switch (firmware)
    {
        case EspModule::Firmware::Esp32At220: return esp32;
        case EspModule::Firmware::Esp8266NonOs174: return nonOs;
        case EspModule::Firmware::Esp8266At221: return at221;
        case EspModule::Firmware::Esp8266At222: return at222;
    }
    return esp32;
}

std::string Identity(EspModule::Firmware firmware, Flash flash)
{
    switch (firmware)
    {
        case EspModule::Firmware::Esp8266At221:
            return "AT version:2.2.1.0(f6fe5ac - ESP8266 - Jun 22 2021 06:45:02)\r\nSDK version:v3.4-22-g967752e2\r\n"
                   "compile time(6800286):Aug  4 2021 17:20:05\r\nBin version:2.2.1(ESP8266_1MB)\r\n";
        case EspModule::Firmware::Esp8266At222:
            return std::string("AT version:2.2.2.0(b3d4a5c - ESP8266 - Jul 28 2026 12:00:00)\r\nSDK version:v3.4-22-g967752e2\r\n"
                               "compile time(6800286):Jul 28 2026 12:00:00\r\nBin version:2.2.2(") +
                   (flash == Flash::OneMb ? "ESP8266_1MB" : "ESP8266_2MB") + ")\r\n";
        case EspModule::Firmware::Esp8266NonOs174:
            return "AT version:1.7.4.0(May 11 2020 19:13:04)\r\nSDK version:3.0.4(9532ceb)\r\n"
                   "compile time:May 27 2020 10:12:17\r\nBin version(Wroom 02):1.7.4\r\n";
        case EspModule::Firmware::Esp32At220: break;
    }
    return "AT version:2.2.0.0(c6fa6bf - ESP32 - Jul  2 2021 06:44:05)\r\nSDK version:v4.2.2-76-gefa6eca\r\n"
           "compile time(3a696ba):Jul  2 2021 11:54:43\r\nBin version:2.2.0(WROOM-32)\r\n";
}

}  // namespace atdialect
