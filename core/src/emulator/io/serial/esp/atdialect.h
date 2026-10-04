#pragma once

/// @file atdialect.h
/// @brief What differs between the AT firmware builds AtModule emulates (network tdd §8.3, ZiFi tdd §7.1):
/// which commands a build has, the reply forms it uses, and ESP-AT's error codes. One table, so a reply form
/// is decided in one place and the tests can walk it.
///
/// Sources (Espressif, primary):
///  - the command reference of ESP8266 ESP-AT 2.2 (it covers 2.2.0.0 to 2.2.2.0):
///    https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/index.html
///  - its "AT Command Set Comparison" (no _CUR / _DEF forms in ESP-AT, except UART_CUR / UART_DEF):
///    https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/AT_Command_Set_Comparison.html
///  - the release notes (2.1.0.0 adds AT+SYSSTORE / AT+SYSLOG, 2.2.0.0 quotes +CIPDOMAIN and adds AT+CWSTATE,
///    2.2.2.0 adds AT+CIPSTATE): https://github.com/espressif/esp-at/releases
///  - the error code layout: components/at/include/esp_at_core.h at v2.2.1.0_esp8266
///    https://github.com/espressif/esp-at/blob/v2.2.1.0_esp8266/components/at/include/esp_at_core.h
///  - the 1 MB build (ESP-01 / ESP-01S) is the default one without OTA: module_config/module_esp8266_1mb
///    https://github.com/espressif/esp-at/blob/v2.2.1.0_esp8266/module_config/module_esp8266_1mb/sdkconfig.defaults
///
/// The ESP8266-AT221 preset is what the Sprinter ESP Network Kit calls "2.2.1": its README lists the command
/// tokens of that binary (the _CUR / _DEF forms, no AT+SYSSTORE, no passive receive), unlike Espressif's
/// v2.2.1.0 release. Kept as the kit describes it (the kit's 2.2.1 profile depends on it); the other replies
/// are ESP-AT 2.2's. The ESP32 preset keeps its earlier forms (out of scope here).

#include <cstdint>
#include <string>

#include "emulator/io/serial/esp/espmodule.h"

namespace atdialect
{

/// ESP-AT error code: (module 0x01 << 24) | (subcategory << 16) | extension (esp_at_core.h ESP_AT_ERROR_NO)
enum Subcategory : uint8_t
{
    kSubNoTerminator = 0x02,
    kSubNoAt = 0x03,
    kSubParaLength = 0x04,
    kSubParaType = 0x05,
    kSubParaNum = 0x06,
    kSubParaInvalid = 0x07,
    kSubParaParseFail = 0x08,
    kSubUnsupported = 0x09,
    kSubExecFail = 0x0A,
    kSubProcessing = 0x0B,
    kSubOpError = 0x0C,
};
constexpr uint32_t ErrorCode(uint8_t subcategory, uint16_t extension = 0)
{
    return (0x01u << 24) | (static_cast<uint32_t>(subcategory) << 16) | extension;
}
/// "ERR CODE:0x01090000\r\n": what AT+SYSLOG=1 prints before ERROR (AT Messages: "ERR CODE:<0x%08x>")
std::string ErrCodeLine(uint32_t code);

/// What one build has and how it answers
struct Traits
{
    bool suffixForms = true;     ///< AT+X_CUR / AT+X_DEF (NonOS AT; ESP-AT only has UART_CUR / UART_DEF)
    bool sysStore = false;       ///< AT+SYSSTORE (ESP-AT 2.1.0.0+)
    bool sysLog = false;         ///< AT+SYSLOG (ESP-AT 2.1.0.0+)
    bool errCodes = false;       ///< AT+SYSLOG=1 prints "ERR CODE:" before ERROR
    bool passiveReceive = true;  ///< AT+CIPRECVMODE / CIPRECVDATA / CIPRECVLEN
    bool joinErrorForm = false;  ///< a failed CWJAP ends "+CWJAP:<code>" ERROR (NonOS: FAIL), with <jap_timeout>
    bool joinQuery2x = false;    ///< AT+CWJAP? has pci_en, reconn_interval, listen_interval, scan_mode, pmf
    bool cwState = false;        ///< AT+CWSTATE? (ESP-AT 2.2.0.0+)
    bool cipState = false;       ///< AT+CIPSTATE? (ESP-AT 2.2.2.0+)
    bool quotedDomain = false;   ///< +CIPDOMAIN:"a.b.c.d" (ESP-AT 2.2.0.0+)
    bool lapAllFields = false;   ///< +CWLAP also has pairwise / group cipher, bgn, wps
    bool ipdOneShot = false;     ///< passive mode: no further +IPD until AT+CIPRECVDATA read the last one
    bool recvLenBlanks = false;  ///< +CIPRECVLEN: a link that is not open is an empty field
    bool ping2x = false;         ///< +PING:<ms> / +PING:TIMEOUT (NonOS: +<ms> / +timeout)
    bool recvData2x = false;     ///< +CIPRECVDATA:<len>,<data> (NonOS: +CIPRECVDATA,<len>:<data>)
};
const Traits& TraitsOf(EspModule::Firmware firmware);

/// The flash a module carries: the ESP8266 ESP-AT 1 MB build has no OTA (AT+CIUPDATE is not there) and says
/// so in AT+GMR's "Bin version"
enum class Flash : uint8_t
{
    TwoMbOrMore = 0,
    OneMb = 1,
};

/// AT+GMR's four lines (without the final OK)
std::string Identity(EspModule::Firmware firmware, Flash flash);

}  // namespace atdialect
