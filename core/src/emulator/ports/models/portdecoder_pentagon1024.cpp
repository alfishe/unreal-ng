#include "stdafx.h"
#include "portdecoder_pentagon1024.h"

#include "common/collectionhelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"

/// region <Constructors / Destructors>

PortDecoder_Pentagon1024::PortDecoder_Pentagon1024(EmulatorContext* context)
    : PortDecoder_Pentagon512(context)
{
}

/// endregion </Constructors / Destructors>

/// region <Interface methods>

void PortDecoder_Pentagon1024::reset()
{
    PortDecoder_Pentagon512::reset();

    EmulatorState& state = _context->emulatorState;
    state.pEFF7 = 0x00;  // Extended memory enabled by default (bit 2 = 0)
}

uint8_t PortDecoder_Pentagon1024::DecodePortIn(uint16_t port, uint16_t pc)
{
    uint8_t result = 0xFF;

    if (IsPort_EFF7(port))
    {
        result = _context->emulatorState.pEFF7;
        PortDecodeDisposition disp;
        disp.decodedPort = 0xEFF7;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortInComplete(port, result, pc, disp);
        return result;
    }

    return PortDecoder_Pentagon512::DecodePortIn(port, pc);
}

void PortDecoder_Pentagon1024::DecodePortOut(uint16_t port, uint8_t value, uint16_t pc)
{
    if (IsPort_EFF7(port))
    {
        Port_EFF7_Out(port, value, pc);

        PortDecodeDisposition disp;
        disp.decodedPort = 0xEFF7;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortOutComplete(port, value, pc, disp);
        return;
    }

    // Intercept #7FFD to use our override with 6-bit bank selection and conditional lock
    if (IsPort_7FFD(port))
    {
        Port_7FFD_Out(port, value, pc);

        PortDecodeDisposition disp;
        disp.decodedPort = 0x7FFD;
        disp.wasDecoded = true;
        disp.wasHandledInline = true;
        OnPortOutComplete(port, value, pc, disp);
        return;
    }

    PortDecoder_Pentagon512::DecodePortOut(port, value, pc);
}

/// endregion </Interface methods>

/// region <Helper methods>

bool PortDecoder_Pentagon1024::IsPort_EFF7(uint16_t port)
{
    // Pentagon 1024 port #EFF7 decoding:
    // The article says "A3, A12, IOWR, and RES" but practical decode is simpler.
    // Match low byte = 0xF7 (A3=0 distinguishes from 0xFF Beta128 system port).
    // Full port: 0xEFF7 = 1110'1111'1111'0111
    // A3=0 in 0xF7, A12=0 in 0xEFF7
    //
    // Simple decode: low byte matches 0xF7 pattern
    static const uint16_t port_EFF7_mask  = 0x00FF;
    static const uint16_t port_EFF7_match = 0x00F7;

    return (port & port_EFF7_mask) == port_EFF7_match;
}

void PortDecoder_Pentagon1024::Port_EFF7_Out(uint16_t port, uint8_t value, uint16_t pc)
{
    EmulatorState& state = _context->emulatorState;
    uint8_t prevValue = state.pEFF7;
    state.pEFF7 = value;

    // Bit 2 controls extended memory access - remap if it changed
    bool extendedMemoryChanged = ((prevValue ^ value) & 0x04) != 0;
    if (extendedMemoryChanged && _memory)
    {
        // Re-apply current 7FFD value with new EFF7 state
        switchRAMPage(state.p7FFD);
        _memory->UpdateZ80Banks();
    }

    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0xEFF7, port, value, pc, Dump_EFF7_value(value).c_str()));
    }
}

/// Port #7FFD (Memory) handler override for Pentagon 1024K.
/// Key difference from Pentagon 128: when extended memory is enabled (#EFF7 bit 2 = 0),
/// bit 5 of #7FFD is NOT the paging lock - it's the 6th page bit (pb5/A18).
/// This is why Pentagon 1024 can access 64 pages (1MB) through #7FFD alone.
void PortDecoder_Pentagon1024::Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc)
{
    EmulatorState& state = _context->emulatorState;
    Memory& memory = *_context->pMemory;

    uint8_t screenNumber = (value & 0b0000'1000) >> 3;  // Bit 3: 0 = Normal (Bank 5), 1 = Shadow (Bank 7)

    // When extended memory is enabled, bit 5 is a page bit, not the lock.
    // Per Born Dead #10: "bit 5 (the 48K lock) is free on machines with the extension:
    // the lock only latches while the extension is disabled (#EFF7 bit 2 = 1)"
    bool extendedMemoryPresent = (state.pEFF7 & 0x04) == 0;
    bool isPagingDisabled = !extendedMemoryPresent && (value & 0b0010'0000);

    // Capture previous screen selection before p7FFD is updated
    uint8_t prevScreenNumber = (_state->p7FFD & 0b00001000) >> 3;

    // Cache port value - must happen before UpdateZ80Banks()
    state.p7FFD = value;

    if (!_7FFD_Locked)
    {
        switchRAMPage(value);
        memory.UpdateZ80Banks();

        _7FFD_Locked = isPagingDisabled;
    }

    // Detect if screen switch requested
    if (prevScreenNumber != screenNumber && _screen != nullptr)
    {
        SpectrumScreenEnum screen = screenNumber ? SCREEN_SHADOW : SCREEN_NORMAL;
        _screen->SetActiveScreen(screen);
    }

    if (!key_exists(_loggingMutePorts, port))
    {
        MLOGDEBUG(DumpPortValue(0x7FFD, port, value, pc));
        MLOGDEBUG(memory.DumpMemoryBankInfo());
    }
}

/// Pentagon 1024K RAM page switching.
/// Uses 6-bit bank index when extended memory enabled: bits [0:2] + bit 5 + [6:7] from #7FFD.
/// When extended memory disabled (#EFF7 bit 2 = 1): only 3-bit bank (128K mode).
///
/// Per Born Dead #10 and UMT memory tester:
/// - pb0..pb2 → #7FFD bits 0-2
/// - pb3 → #7FFD bit 6
/// - pb4 → #7FFD bit 7
/// - pb5 → #7FFD bit 5 (when extension enabled)
/// Total: 64 pages (0-63) = 1024KB
void PortDecoder_Pentagon1024::switchRAMPage(uint8_t value)
{
    Memory& memory = *_context->pMemory;
    EmulatorState& state = _context->emulatorState;

    // #EFF7 bit 2: memory above 128K latch (0 = present, 1 = absent)
    bool extendedMemoryPresent = (state.pEFF7 & 0x04) == 0;

    if (!extendedMemoryPresent)
    {
        // Extended memory disabled - use only 3-bit bank selection (128K mode)
        uint8_t bankRAM = value & 0b0000'0111;
        memory.SetRAMPageToBank3(bankRAM);
        return;
    }

    // Extended memory enabled - use 6-bit bank selection (Pentagon 1024 specific)
    // Bits [0:2] from 7FFD → bits 0-2 of bank (pb0..pb2)
    // Bits [6:7] from 7FFD → bits 3-4 of bank (pb3, pb4)
    // Bit 5 from 7FFD → bit 5 of bank (pb5) - the key Pentagon 1024 feature!
    uint8_t bankRAM = value & 0b0000'0111;           // Bits [0:2] from 7FFD → pb0..pb2
    bankRAM |= ((value & 0b1100'0000) >> 3);         // Bits [6:7] from 7FFD → pb3, pb4
    bankRAM |= (value & 0b0010'0000);                // Bit 5 from 7FFD → pb5

    memory.SetRAMPageToBank3(bankRAM);
}

/// endregion </Helper methods>

/// region <Debug>

std::string PortDecoder_Pentagon1024::Dump_EFF7_value(uint8_t value)
{
    // Bit assignments per Born Dead #10 article
    bool a4b = (value & 0x01) != 0;           // Bit 0: attribute per byte
    bool mode512x192 = (value & 0x02) != 0;   // Bit 1: 512x192 mode
    bool extMemAbsent = (value & 0x04) != 0;  // Bit 2: memory above 128K (0=present)
    bool gigaScreen = (value & 0x10) != 0;    // Bit 4: GigaScreen
    bool glukCmos = (value & 0x80) != 0;      // Bit 7: Gluk CMOS

    return StringHelper::Format("ExtMem128K+: %s; a4b: %d; 512x192: %d; GigaScr: %d; Gluk: %d",
                                extMemAbsent ? "off" : "on",
                                a4b, mode512x192, gigaScreen, glukCmos);
}

/// endregion </Debug>
