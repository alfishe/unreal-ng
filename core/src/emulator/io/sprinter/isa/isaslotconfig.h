#pragma once

/// @file isaslotconfig.h
/// @brief What the Sprinter's two ISA-8 slots hold: the `[ISA]` keys of the machine config
/// (docs/inprogress/2026-10-02-sprinter-isa/tdd.md §5, network cards: 2026-10-02-sprinter-network/tdd.md §12).
///
/// ```ini
/// [ISA]
/// Slot1=ZXBUS            ; ISA slot 1 (J6, page #D4 / #D0): the ZX-bus adapter, the GS / NeoGS of [SOUND] GSType on it
/// Slot2=NE2000           ; ISA slot 2 (J7, page #D6 / #D2)
/// Slot2Chip=RTL8019AS    ; NE2000: RTL8019AS | UM9003 | NE1000
/// Slot2Base=0x300        ; NE2000: 0x200..0x3E0 in steps of 0x20 (in an INI '#' starts a comment: 0x300 or 300h)
/// Slot2Irq=3             ; written into the card's EEPROM (one IRQ line per slot)
/// Slot2Mac=auto          ; auto = 02:53:50:00:<instance>:<slot> (SPRINTERESP: the ESP's station MAC, auto =
///                        ; 5C:CF:7F:5A:<instance>:<slot>)
/// Slot1=SPRINTERESP      ; the SprinterESP Wi-Fi card: TL16C550C at #3E8 (fixed), IRQ 3 (wired), an ESP-12F
/// Slot1Peer=AT           ; what the 16550 is wired to (ComPortSpec): AT (default, the ESP), ESPNET, LOOPBACK,
///                        ; TCP:host:port, SERIAL:device[,baud] (a real ESP on a USB adapter), NONE
/// ```
///
/// Slot numbers are 1 and 2 everywhere a person reads them (config, reports, automation), as on the board and in
/// the Peters Plus notes ("ISA1", "ISA2"); programs call them slot 0 / 1 (pages #D4 / #D6).

#include <cstdint>
#include <string>

namespace sprinterisa
{

/// A card kind a slot can hold. The numbers are stored in the TTD blob (SprinterIsa, id 33): never renumber
enum class CardKind : uint8_t
{
    None = 0,
    ZxBus = 1,        ///< the ZX-bus adapter with the General Sound / NeoGS (ISA phase I2)
    Ram = 2,          ///< ISA RAM (ISA phase I3)
    Ne2000 = 3,       ///< NE2000-class Ethernet (RTL8019AS default; network phase SN1)
    El3c509b = 4,     ///< 3Com EtherLink III (network phase SN5)
    SprinterEsp = 5,  ///< SprinterESP Wi-Fi: a 16550 + an ESP8266 at #3E8 (network phase SN3)
    Modem = 6,        ///< ISA Hayes modem (network phase SN4)
    Dual16552 = 7,    ///< SprinterSerial: two 16550s (network phase SN4)
};

/// NE2000 variants (network tdd §6.4)
enum class Ne2000Chip : uint8_t
{
    Rtl8019as = 0,
    Um9003 = 1,
    Ne1000 = 2,
};

constexpr int kSlots = 2;

/// One slot's settings (plain fields: the struct lives in CONFIG, which is copied as bytes)
struct SlotConfig
{
    uint8_t kind;        ///< CardKind
    uint8_t chip;        ///< NE2000: Ne2000Chip
    uint16_t base;       ///< I/O base of a network card (NE2000: #300)
    uint8_t irq;         ///< the IRQ the card's configuration names (informational: one line per slot)
    uint8_t macAuto;     ///< 1: 02:53:50:00:<instance>:<slot>
    uint8_t mac[6];
    char peer[64];       ///< UART cards (SPRINTERESP): ComPortSpec text of the line's other end; empty = AT
};

/// The SprinterESP card's fixed resources (rev 1.0.5 schematic: 74HC30 + 74HC27 decode A13-A3 = #3E8 >> 3, INTR to
/// ISA IRQ3, a 14.7456 MHz crystal at the TL16C550C's XIN)
constexpr uint16_t kSprinterEspBase = 0x3E8;
constexpr uint8_t kSprinterEspIrq = 3;
constexpr uint32_t kSprinterEspUartClock = 14745600;

/// The configured slots ([ISA] section)
struct IsaConfig
{
    SlotConfig slot[kSlots];   ///< not "slots": a Qt macro, and the GUI includes CONFIG
};

/// The owner's default population (2026-10-02): slot 1 = the ZX-bus adapter with the General Sound of [SOUND] GSType
/// (the NeoGS in the Sprinter config; ISA phase I2), slot 2 = the NE2000 (RTL8019AS at #300, IRQ 3, automatic MAC)
IsaConfig DefaultConfig();

/// "NONE", "ZXBUS", "RAM", "NE2000", "EL3C509B", "SPRINTERESP", "MODEM", "DUAL16552"
const char* KindName(CardKind kind);
/// The lower-case form automation reports ("none", "zxbus", "ne2000", ...)
std::string KindKey(CardKind kind);
/// Case-insensitive; false for an unknown name
bool ParseKind(const std::string& text, CardKind& kind);

const char* ChipName(Ne2000Chip chip);
bool ParseChip(const std::string& text, Ne2000Chip& chip);

/// "#300", "0x300", "300h" or "768"; NE2000 bases #200..#3E0 in steps of #20
bool ParseNe2000Base(const std::string& text, uint16_t& base);

/// "auto" or "aa:bb:cc:dd:ee:ff" (also '-' separated)
bool ParseMac(const std::string& text, SlotConfig& slot);

/// The MAC a card in `slot` (0-based) of emulator instance `instance` uses: the configured one, or
/// 02:53:50:00:<instance>:<slot + 1> (locally administered, "SP"; network open question Q3)
void EffectiveMac(const SlotConfig& slot, int slotIndex, uint8_t instance, uint8_t out[6]);

/// Whether this build has the card (the phases that are not built yet refuse the kind with a reason)
bool KindAvailable(CardKind kind, std::string* why = nullptr);

}  // namespace sprinterisa
