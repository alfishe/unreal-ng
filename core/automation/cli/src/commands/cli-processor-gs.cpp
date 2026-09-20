// General Sound card state - 'state audio gs' command (GS design §11.4).
// Same chip getters the WebAPI /state/audio/gs endpoint serves.

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/sound/chips/soundchip_gs.h>
#include <emulator/sound/soundmanager.h>

#include <iomanip>
#include <sstream>

#include "cli-processor.h"

void CLIProcessor::HandleStateAudioGS(const ClientSession& session, EmulatorContext* context, const std::string& optionArg)
{
    std::stringstream ss;
    ss << "General Sound Device State" << NEWLINE;
    ss << "==========================" << NEWLINE;
    ss << NEWLINE;

    SoundManager* soundManager = context->pSoundManager;
    SoundChip_GeneralSound* gs = soundManager ? soundManager->getGeneralSound() : nullptr;

    if (!gs)
    {
        ss << "Status: Not fitted" << NEWLINE;
        ss << NEWLINE;
        ss << "The General Sound card is disabled on this machine." << NEWLINE;
        ss << "Enable it with [SOUND] GSType=Z80 (plus [ROM] GSROM pointing at" << NEWLINE;
        ss << "the 32 KB firmware) and restart the emulator." << NEWLINE;

        session.SendResponse(ss.str());
        return;
    }

    const uint8_t status = gs->getStatusRaw();
    const bool verbose = optionArg == "--verbose" || optionArg == "verbose" || optionArg == "-v";

    ss << "Device: General Sound (Z80 coprocessor @ 12 MHz, 4 x 8-bit DAC)" << NEWLINE;
    ss << "ROM:    " << (gs->isROMLoaded() ? "Loaded (32 KB)" : "Missing (zero-filled)") << NEWLINE;
    ss << "RAM:    " << gs->getRamSizeKB() << " KB" << NEWLINE;
    ss << "Page:   " << (int)gs->getMPAG() << " (MPAG banking latch)" << NEWLINE;
    ss << NEWLINE;

    ss << "Mailbox (host ports #B3/#BB):" << NEWLINE;
    ss << "  Status:          0x" << std::hex << std::setw(2) << std::setfill('0') << (int)status << NEWLINE;
    ss << "  Command Pending: " << ((status & 0x01) ? "Yes" : "No") << " (bit0)" << NEWLINE;
    ss << "  Data Pending:    " << ((status & 0x80) ? "Yes" : "No") << " (bit7)" << NEWLINE;
    ss << "  Command from ZX: 0x" << std::hex << std::setw(2) << (int)gs->getCommandFromHost() << NEWLINE;
    ss << "  Data from ZX:    0x" << std::hex << std::setw(2) << (int)gs->getDataFromHost() << NEWLINE;
    ss << "  Data to ZX:      0x" << std::hex << std::setw(2) << (int)gs->getDataToHost() << NEWLINE;
    ss << std::dec;
    ss << NEWLINE;

    ss << "DAC Channels:" << NEWLINE;
    for (int i = 0; i < 4; i++)
    {
        ss << "  Channel " << (i + 1) << ": Sample 0x" << std::hex << std::setw(2) << (int)gs->getChannelSample(i)
           << std::dec << "  Volume " << (int)gs->getChannelVolume(i) << "/63" << NEWLINE;
    }
    ss << NEWLINE;

    if (verbose)
    {
        ss << "Coprocessor (Z80ex):" << NEWLINE;
        ss << "  PC: 0x" << std::hex << std::setw(4) << gs->getCPUReg(regPC) << NEWLINE;
        ss << "  SP: 0x" << std::hex << std::setw(4) << gs->getCPUReg(regSP) << NEWLINE;
        ss << "  AF: 0x" << std::hex << std::setw(4) << gs->getCPUReg(regAF) << NEWLINE;
        ss << std::dec;
        ss << "  Halted: " << (gs->isCPUHalted() ? "Yes" : "No") << NEWLINE;
        ss << NEWLINE;
    }
    else
    {
        ss << "Use 'state audio gs --verbose' for coprocessor registers" << NEWLINE;
    }

    session.SendResponse(ss.str());
}
