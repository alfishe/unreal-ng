#pragma once

/// @file mousedevicestatus.h
/// @brief What one mouse device of the machine is and what it gives the guest
/// (docs/inprogress/2026-10-03-mouse-api-routing/design.md §3).
///
/// Every mouse device (IMouseSink) describes itself in this one shape; the
/// automation surfaces (WebAPI, MCP, CLI, Lua, Python) print it without knowing
/// the device. Sections that do not apply are left out (`has*` = false).
///
/// Worked example (Sprinter, DSS 1.71 running, the mouse moved 5 right):
///   id "sprinter", kind SerialMicrosoft, wired, fitted, in use (SIO B read
///   this second); ports #FADF #FF / #FBDF 36 / #FFDF 85 (the PLD's Kempston
///   view of the same counters); serial: one packet #40 #05 #00 in flight with
///   1 byte sent, receiver 1 215 baud (in tune), SIO B FIFO holds #40.

#include <cstdint>
#include <string>

enum class MouseDeviceKind : uint8_t
{
    Kempston,         ///< the Kempston interface: counters at #FADF / #FBDF / #FFDF
    SerialMicrosoft,  ///< a Microsoft serial mouse on a UART (Sprinter: SIO B), plus the board's Kempston view
    Ps2Avr,           ///< a PS/2 mouse read by the ZX-Evo AVR, which shows Kempston registers
    AmxPio,           ///< the AMX mouse: quadrature strobes into a Z80 PIO, one IM 2 interrupt per mickey
};

struct MouseDeviceStatus
{
    std::string id;           ///< stable name automation selects it by: "kempston", "sprinter", "evo-ps2", "amx"
    std::string name;         ///< one line for people
    MouseDeviceKind kind = MouseDeviceKind::Kempston;
    /// The machine's ports read this device. A Kempston interface object on a machine whose ports read
    /// the board mouse instead (Sprinter, ZX-Evo, TS-Conf) is not wired: it is not listed
    bool wired = true;
    bool fitted = false;      ///< a program can read it (config / board)
    bool inUse = false;       ///< a program read it lately (IMouseSink::IsMouseInUse)
    bool wheel = false;       ///< the guest sees wheel steps
    uint8_t buttons = 3;      ///< buttons the guest can tell apart (2: left, right; 3: + middle)

    /// The three registers at the Kempston addresses as a program's IN reads them now
    bool hasPorts = false;
    uint8_t portButtons = 0xFF;  ///< #FADF
    uint8_t portX = 0xFF;        ///< #FBDF
    uint8_t portY = 0xFF;        ///< #FFDF

    /// The device's own counters (X + right, Y + up) and buttons (active low: D0 left, D1 right, D2 middle)
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t buttonMask = 0xFF;

    /// Serial mouse: the line and the receiver
    bool hasSerial = false;
    struct Serial
    {
        uint32_t baud = 0;               ///< the mouse's line rate (1 200)
        double receiverBaud = 0.0;       ///< what the UART receives with now (0: no clock)
        bool receiverInTune = false;     ///< within the tolerance: characters arrive
        bool receiverEnabled = false;    ///< the UART's receiver is on (SIO WR3 bit 0)
        bool packetInFlight = false;     ///< a packet is on the wire
        uint8_t packet[3] = {0, 0, 0};   ///< the packet in flight (or the last one)
        uint8_t packetBytesSent = 3;     ///< of `packet`
        int pendingDx = 0;               ///< motion not yet in a packet (+ right)
        int pendingDy = 0;               ///< (+ up)
        uint64_t packetsSent = 0;        ///< since power-on (statistics, not machine state)
        uint64_t bytesReceived = 0;      ///< characters the UART took
        uint64_t framingErrors = 0;      ///< characters lost to a receive clock out of tune
        uint8_t fifo[3] = {0, 0, 0};     ///< the UART's receive FIFO
        uint8_t fifoCount = 0;
        bool overrun = false;            ///< the UART latched an overrun
    } serial;

    /// PS/2 mouse behind the AVR: the AVR applies each packet to its registers at once (no queue)
    bool hasPs2 = false;
    struct Ps2
    {
        bool connected = false;   ///< a mouse on the PS/2 port
        uint8_t resolution = 0;   ///< 0..3 = 1, 2, 4, 8 counts per mm (AVR RTC cell #FD)
    } ps2;

    /// AMX mouse: the interface's Z80 PIO (port A = X on #1F, port B = Y on #3F) and the motion not yet strobed
    bool hasPio = false;
    struct Pio
    {
        struct Port
        {
            uint8_t mode = 1;     ///< 0 output, 1 input, 2 bidirectional, 3 bit control
            uint8_t vector = 0;   ///< the IM 2 vector the port drives
            uint8_t data = 0;     ///< what IN of its data port returns now (bit 0 = the direction)
            bool ie = false;      ///< interrupts enabled
            bool ip = false;      ///< an interrupt pending
            bool ius = false;     ///< an interrupt under service (until RETI)
        } port[2];
        int pendingDx = 0;        ///< mickeys not yet strobed (+ right)
        int pendingDy = 0;        ///< (+ up)
        uint8_t buttonsPort = 0xFF;   ///< IN #DF: D7 left, D6 middle, D5 right, active low (released bits: #FF here)
        uint64_t strobes = 0;     ///< mickeys strobed since the card was built (statistics, not machine state)
    } pio;

    static const char* KindName(MouseDeviceKind kind)
    {
        switch (kind)
        {
            case MouseDeviceKind::Kempston:
                return "kempston";
            case MouseDeviceKind::SerialMicrosoft:
                return "serial-microsoft";
            case MouseDeviceKind::Ps2Avr:
                return "ps2-avr";
            case MouseDeviceKind::AmxPio:
                return "amx-pio";
        }
        return "unknown";
    }
};
