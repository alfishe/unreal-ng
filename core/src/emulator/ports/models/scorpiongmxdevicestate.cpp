#include "stdafx.h"

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_scorpiongmx.h"
#include "emulator/state/devicestate.h"

namespace
{
std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
}  // namespace

namespace DeviceState
{

StateNode GmxPaging(EmulatorContext* context)
{
    StateNode ret = StateNode::Object();
    const auto* gmx = context ? dynamic_cast<const PortDecoder_ScorpionGMX*>(context->pPortDecoder) : nullptr;
    if (!gmx || !context->pMemory)
    {
        ret["available"] = false;
        ret["description"] = "Not a Scorpion GMX machine";
        return ret;
    }

    // Built from the latches, not through the read-backs: IN #78FD shifts the magic byte (GMX design section 2.2)
    const EmulatorState& state = context->emulatorState;
    const GmxState& card = state.gmx;
    const uint8_t p7EFD = state.scorpion.p7EFD;
    ret["available"] = true;

    StateNode config = StateNode::Object();
    config["value"] = Hex8(card.p00);
    config["scheme"] = static_cast<int64_t>(card.p00 & 0x07);
    config["fixrom"] = (card.p00 & 0x10) != 0;
    config["blkext"] = (card.p00 & 0x20) != 0;
    config["blkex2"] = (card.p00 & 0x40) != 0;
    config["pentagon_int"] = (card.p00 & 0x80) != 0;
    config["reconf_pending"] = gmx->IsReconfigurationPending();
    config["firmware_booting"] = (gmx->BootState() & PortDecoder_ScorpionGMX::BOOT_FIRMWARE_RUNNING) != 0;
    config["autostart_waits"] = gmx->IsAutostartDeferred();
    ret["port_00"] = config;

    StateNode plane = StateNode::Object();
    plane["value"] = Hex8(p7EFD);
    plane["rom_plane"] = static_cast<int64_t>(gmx->Plane());
    plane["turbo"] = (p7EFD & 0x80) != 0;
    plane["video_640x200"] = (p7EFD & 0x08) != 0;
    plane["magic_blocked"] = (p7EFD & 0x04) != 0;
    ret["port_7efd"] = plane;

    ret["port_78fd"] = Hex8(card.p78FD);
    ret["page_8000"] = static_cast<int64_t>((card.p78FD & 0x7F) ^ 0x02);
    ret["port_dffd"] = Hex8(state.pDFFD);
    ret["page_c000"] = static_cast<int64_t>(context->pMemory->GetRAMPageForBank3());
    ret["port_7afd"] = Hex8(card.p7AFD);
    ret["port_7cfd"] = Hex8(card.p7CFD);
    ret["scroll_start"] = static_cast<int64_t>(((card.p7CFD & 0x3F) << 8) | (card.p7AFD & 0xF0));
    ret["trdos_page_1ffd"] = (state.p1FFD & 0x04) != 0;

    StateNode magic = StateNode::Object();
    magic["frozen"] = card.magicLock != 0;
    magic["frozen_7afd"] = Hex8(card.lock7AFD);
    magic["frozen_7efd"] = Hex8(card.lock7EFD);
    magic["shift"] = Hex8(card.magicShift);
    ret["magic"] = magic;
    return ret;
}

}  // namespace DeviceState
