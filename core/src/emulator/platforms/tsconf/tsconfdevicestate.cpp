// DeviceState::TsConf - the TS-Conf report every automation interface renders
// (declared in emulator/state/devicestate.h; lives here so the shared state
// code names no TS-Conf type)

#include "stdafx.h"

#include "emulator/state/devicestate.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

namespace
{
    const char* Lck128Name(TsConfLck128 mode)
    {
        switch (mode)
        {
            case TsConfLck128::Mode512K: return "512K";
            case TsConfLck128::Mode128K: return "128K";
            case TsConfLck128::Auto: return "auto";
            default: return "1024K";
        }
    }

    const char* DmaTaskName(uint8_t device)
    {
        switch (device)
        {
            case 0x1: return "RAM to RAM";
            case 0x2: return "SPI to RAM";
            case 0x3: return "IDE to RAM";
            case 0x4: return "fill";
            case 0x6: return "blit add (BLT2)";
            case 0x7: return "wait port";
            case 0x9: return "blit (BLT1)";
            case 0xA: return "RAM to SPI";
            case 0xB: return "RAM to IDE";
            case 0xC: return "RAM to CRAM";
            case 0xD: return "RAM to SFILE";
            default: return "undefined";
        }
    }

    StateNode Unavailable(const char* description)
    {
        StateNode n = StateNode::Object();
        n["available"] = false;
        n["description"] = description;
        return n;
    }

    uint16_t Nine(uint8_t low, uint8_t high) { return static_cast<uint16_t>(low | ((high & 1u) << 8)); }
}  // namespace

namespace DeviceState
{

StateNode TsConf(EmulatorContext* context)
{
    auto* decoder = context ? dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder) : nullptr;
    if (!decoder)
        return Unavailable("Not a TS-Conf machine");

    const TsConfState& ts = decoder->GetState();
    const uint8_t* r = ts.regs;
    StateNode ret = StateNode::Object();
    ret["available"] = true;

    // Memory
    {
        const uint8_t memConfig = r[TsConfReg::MemConfig];
        StateNode m = StateNode::Object();
        m["mem_config"] = int(memConfig);
        m["window0_mode"] = (memConfig & TsConfMemConfig::W0NoMap) ? "normal" : "mapped";
        m["window0_source"] = (ts.vdos || (memConfig & TsConfMemConfig::W0Ram)) ? "RAM" : "ROM";
        m["window0_writable"] = ts.vdos || ((memConfig & TsConfMemConfig::W0Ram) && (memConfig & TsConfMemConfig::W0We));
        m["rom128"] = (memConfig & TsConfMemConfig::Rom128) ? "BASIC-48" : "BASIC-128";
        StateNode pages = StateNode::Array();
        for (uint8_t w = 0; w < 4; w++)
            pages.push(int(ts.Page(w)));
        m["pages"] = pages;
        m["lck128"] = Lck128Name(ts.Lck128());
        m["lock48"] = ts.lock48 != 0;
        m["dos"] = ts.dos != 0;
        m["vdos"] = ts.vdos != 0;
        m["fdd_virt"] = int(r[TsConfReg::FddVirt]);
        m["cache_config"] = int(r[TsConfReg::CacheConfig]);
        m["fm_window"] = ts.FmEnabled() ? int(ts.FmBase()) : -1;
        ret["memory"] = m;
    }

    // Video
    {
        static const char* const kModes[4] = {"ZX", "16C", "256C", "TXT"};
        static const char* const kGeometry[4] = {"256x192", "320x200", "320x240", "360x288"};
        const uint8_t vConfig = r[TsConfReg::VConfig];
        StateNode v = StateNode::Object();
        v["v_config"] = int(vConfig);
        v["mode"] = kModes[vConfig & 0x03];
        v["geometry"] = kGeometry[vConfig >> 6];
        v["nogfx"] = (vConfig & 0x20) != 0;
        v["notsu"] = (vConfig & 0x10) != 0;
        v["gfxovr"] = (vConfig & 0x08) != 0;
        v["v_page"] = int(r[TsConfReg::VPage]);
        v["pal_sel"] = int(r[TsConfReg::PalSel]);
        v["border"] = int(r[TsConfReg::Border]);
        v["gx_offset"] = int(Nine(r[TsConfReg::GXOffsL], r[TsConfReg::GXOffsH]));
        v["gy_offset"] = int(Nine(r[TsConfReg::GYOffsL], r[TsConfReg::GYOffsH]));
        const uint8_t tConfig = r[TsConfReg::TConfig];
        StateNode tsu = StateNode::Object();
        tsu["t_config"] = int(tConfig);
        tsu["sprites"] = (tConfig & 0x80) != 0;
        tsu["tiles1"] = (tConfig & 0x40) != 0;
        tsu["tiles0"] = (tConfig & 0x20) != 0;
        tsu["window_360"] = (tConfig & 0x01) != 0;
        tsu["tilemap_page"] = int(r[TsConfReg::TMapPage]);
        tsu["tile0_page"] = int(r[TsConfReg::T0GPage]);
        tsu["tile1_page"] = int(r[TsConfReg::T1GPage]);
        tsu["sprite_page"] = int(r[TsConfReg::SGPage]);
        int active = 0;
        for (uint32_t d = 0; d < 85; d++)
            active += (ts.sfile[d * 3] & 0x2000) ? 1 : 0;
        tsu["active_sprites"] = active;
        v["tsu"] = tsu;

        // The line the engine is on and the registers it is displayed with
        StateNode line = StateNode::Object();
        const uint32_t current = ts.engNextLine ? ts.engNextLine - 1u : 0u;
        const TsConfLine& set = decoder->GetEngine().Line(current);
        line["line"] = int(current);
        line["v_config"] = int(set.vConfig);
        line["v_page"] = int(set.vPage);
        line["row"] = int(set.cntRow);
        line["dram_video"] = int(set.videoCost);
        line["dram_tsu"] = int(set.tsuCost);
        v["line"] = line;
        ret["video"] = v;
    }

    // Interrupts
    {
        StateNode i = StateNode::Object();
        i["int_mask"] = int(r[TsConfReg::IntMask]);
        StateNode pending = StateNode::Array();
        if (ts.intPending & TsConfInt::Frame)
            pending.push("frame");
        if (ts.intPending & TsConfInt::Line)
            pending.push("line");
        if (ts.intPending & TsConfInt::Dma)
            pending.push("dma");
        i["pending"] = pending;
        i["frame_line"] = int(Nine(r[TsConfReg::VsIntL], r[TsConfReg::VsIntH]));
        i["frame_tact"] = int(r[TsConfReg::HsInt]);
        ret["interrupts"] = i;
    }

    // DMA
    {
        StateNode d = StateNode::Object();
        const bool busy = ts.dmaFlags & TsConfDmaFlag::Active;
        d["busy"] = busy;
        d["task"] = DmaTaskName(ts.dmaDevice);
        d["source"] = static_cast<uint64_t>(ts.dmaSrc) * 2;
        d["destination"] = static_cast<uint64_t>(ts.dmaDst) * 2;
        d["words_per_block"] = int(r[TsConfReg::DmaLen]) + 1;
        d["blocks"] = int(r[TsConfReg::DmaNum]) + 1;
        if (busy)
        {
            d["words_left_in_block"] = int(ts.dmaBurst) + 1;
            d["blocks_left"] = int(ts.dmaBlocks & 0xFF) + 1;
        }
        ret["dma"] = d;
    }

    // Clock and SD card
    {
        static const char* const kClock[4] = {"3.5 MHz", "7 MHz", "14 MHz", "14 MHz"};
        ret["cpu_clock"] = kClock[r[TsConfReg::SysConfig] & 0x03];
        StateNode sd = StateNode::Object();
        sd["present"] = decoder->GetSdCard().present();
        sd["selected"] = decoder->GetZController().IsSelected();
        ret["sd"] = sd;
    }
    return ret;
}

}  // namespace DeviceState
