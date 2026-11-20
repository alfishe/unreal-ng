#include "stdafx.h"

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "emulator/state/devicestate.h"

namespace
{
std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
std::string Hex10(unsigned value) { return StringHelper::Format("0x%03X", value & 0x3FFu); }

const char* VideoModeName(uint8_t mode)
{
    switch (mode & 0x07)
    {
        case 0: return "ega_320x200";
        case 2: return "hires_640x200";
        case 3: return "zx";
        case 6: return "text_80x25";
        default: return "reserved";
    }
}
}  // namespace

namespace DeviceState
{

StateNode P2666Paging(EmulatorContext* context)
{
    StateNode ret = StateNode::Object();
    auto* board = context ? dynamic_cast<PortDecoderP2666*>(context->pPortDecoder) : nullptr;
    if (!board || !context->pMemory)
    {
        ret["available"] = false;
        ret["description"] = "Not a Pentagon 2.666 LE machine";
        return ret;
    }

    // Built from the latches; nothing here goes through a port read
    const EmulatorState& state = context->emulatorState;
    const P2666State& p = state.p2666;
    ret["available"] = true;

    StateNode x77 = StateNode::Object();
    x77["value"] = Hex8(p.pxx77);
    x77["video_mode"] = VideoModeName(p.pxx77);
    x77["turbo1_28mhz"] = (p.pxx77 & 0x08) != 0;
    x77["cpm"] = p.cpm != 0;
    x77["palette_blocked_in_dos"] = p.pen2 != 0;
    ret["port_xx77"] = x77;

    StateNode eff7 = StateNode::Object();
    eff7["value"] = Hex8(state.pEFF7);
    eff7["p16"] = (state.pEFF7 & PortDecoderP2666::kEff7P16) != 0;
    eff7["mode128"] = (state.pEFF7 & PortDecoderP2666::kEff7Lock128) != 0;
    eff7["ram0"] = (state.pEFF7 & PortDecoderP2666::kEff7Ram0) != 0;
    eff7["turbo0_3_5mhz"] = (state.pEFF7 & PortDecoderP2666::kEff7Slow) != 0;
    eff7["cmos"] = (state.pEFF7 & PortDecoderP2666::kEff7Cmos) != 0;
    ret["port_eff7"] = eff7;

    const uint8_t ratio = board->SelectedRatio();
    ret["cpu_mhz"] = ratio == 8 ? "28" : ratio == 4 ? "14" : "3.5";
    ret["dos"] = (state.flags & CF_TRDOS) != 0;
    ret["dos_kept_in_ram_bf0"] = p.bf0 != 0;
    ret["paging_locked"] = board->IsPagingLocked();

    // The active map is #7FFD bit 4; each window: Pentagon mode (bit 7) or a dispatcher page (RAM bit 6, else ROM)
    const uint8_t map = (state.p7FFD & 0x10) ? 1 : 0;
    ret["map"] = static_cast<int64_t>(map);
    StateNode windows = StateNode::Array();
    for (uint8_t w = 0; w < 4; w++)
    {
        const uint16_t reg = p.window[map][w];
        StateNode window = StateNode::Object();
        window["register"] = Hex10(reg);
        window["kind"] = (reg & 0x80) ? "pentagon" : (reg & 0x40) ? "ram" : "rom";
        window["page"] = static_cast<int64_t>(board->WindowPage(w));
        windows.push(window);
    }
    ret["windows"] = windows;

    StateNode other = StateNode::Array();
    for (uint8_t w = 0; w < 4; w++)
        other.push(Hex10(p.window[map ^ 1][w]));
    ret["other_map"] = other;

    // The palette words as written (A15..8 : D7..0, HR §6.2)
    StateNode palette = StateNode::Array();
    for (uint16_t word : p.palette)
        palette.push(StringHelper::Format("0x%04X", word));
    ret["palette"] = palette;

    ret["border_bright"] = p.border3 != 0;
    ret["covox"] = Hex8(p.covox);
    return ret;
}

}  // namespace DeviceState
