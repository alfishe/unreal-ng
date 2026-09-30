// DeviceState::TsConf - the TS-Conf report every automation interface renders
// (declared in emulator/state/devicestate.h; lives here so the shared state
// code names no TS-Conf type)

#include "stdafx.h"

#include <cstdio>

#include "emulator/state/devicestate.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/video/tsconf/screentsconf.h"

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

StateNode TsConfTsu(EmulatorContext* context)
{
    auto* decoder = context ? dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder) : nullptr;
    if (!decoder)
        return Unavailable("Not a TS-Conf machine");

    const TsConfState& ts = decoder->GetState();
    const uint8_t* r = ts.regs;
    const uint8_t tConfig = r[TsConfReg::TConfig];
    const uint8_t palSel = r[TsConfReg::PalSel];
    auto hex = [](unsigned value, int digits) {
        char text[8];
        std::snprintf(text, sizeof(text), "%0*X", digits, value);
        return std::string(text);
    };

    StateNode ret = StateNode::Object();
    ret["available"] = true;
    ret["t_config"] = int(tConfig);
    ret["sprites_on"] = (tConfig & 0x80) != 0;
    ret["window_360"] = (tConfig & 0x01) != 0;
    ret["tilemap_page"] = int(r[TsConfReg::TMapPage]);
    ret["sprite_page"] = int(r[TsConfReg::SGPage]);

    // Tile layers T0 / T1 (hs §4.4): graphics page, offsets, palette bits, tile-0 drawing
    StateNode layers = StateNode::Array();
    for (uint32_t layer = 0; layer < 2; layer++)
    {
        const uint32_t x = TsConfReg::T0XOffsL + layer * 4u;
        StateNode t = StateNode::Object();
        t["name"] = layer ? "t1" : "t0";
        t["enabled"] = (tConfig & (layer ? 0x40 : 0x20)) != 0;
        t["draw_tile_zero"] = (tConfig & (layer ? 0x08 : 0x04)) != 0;
        t["graphics_page"] = int(r[layer ? TsConfReg::T1GPage : TsConfReg::T0GPage]);
        t["x_offset"] = int(r[x] | ((r[x + 1] & 1u) << 8));
        t["y_offset"] = int(r[x + 2] | ((r[x + 3] & 1u) << 8));
        t["palette"] = int((palSel >> (layer ? 6 : 4)) & 0x03);  // PAL_SEL [5:4] / [7:6]: CRAM bank bits 7:6
        t["map_offset"] = int(layer * 128);                      // within each 256-byte map row
        layers.items.push_back(t);
    }
    ret["tile_layers"] = layers;

    // Sprites: every SFILE descriptor, the layer its LEAP position puts it in
    StateNode sprites = StateNode::Array();
    uint32_t layer = 0;
    int active = 0;
    for (uint32_t d = 0; d < 85; d++)
    {
        const uint16_t w0 = ts.sfile[d * 3];
        const uint16_t w1 = ts.sfile[d * 3 + 1];
        const uint16_t w2 = ts.sfile[d * 3 + 2];
        StateNode sp = StateNode::Object();
        sp["index"] = int(d);
        sp["active"] = (w0 & 0x2000) != 0;
        sp["leap"] = (w0 & 0x4000) != 0;
        sp["layer"] = layer == 0 ? "s0" : (layer == 1 ? "s1" : "s2");
        sp["x"] = int(w1 & 0x1FF);
        sp["y"] = int(w0 & 0x1FF);
        sp["width"] = int((((w1 >> 9) & 0x07) + 1) * 8);
        sp["height"] = int((((w0 >> 9) & 0x07) + 1) * 8);
        sp["x_flip"] = (w1 & 0x8000) != 0;
        sp["y_flip"] = (w0 & 0x8000) != 0;
        sp["tile"] = int(w2 & 0x0FFF);
        sp["bitmap_x"] = int((w2 & 0x3F) * 8);   // in the 512x512 sheet at sprite_page
        sp["bitmap_y"] = int(((w2 >> 6) & 0x3F) * 8);
        sp["palette"] = int(w2 >> 12);           // CRAM bank bits 7:4
        StateNode words = StateNode::Array();
        for (uint16_t w : {w0, w1, w2})
            words.items.push_back(StateNode(hex(w, 4)));
        sp["words"] = words;
        sprites.items.push_back(sp);
        active += (w0 & 0x2000) ? 1 : 0;
        if ((w0 & 0x4000) && layer < 2)
            layer++;  // LEAP: the next descriptor starts the next sprite layer
    }
    ret["active_sprites"] = active;
    ret["sprites"] = sprites;

    // CRAM: the 256 palette cells with their colors (no-VDAC curve, as drawn)
    StateNode cram = StateNode::Array();
    for (uint32_t i = 0; i < 256; i++)
    {
        const uint32_t rgba = ScreenTSConf::CramToRgba(ts.cram[i]);
        StateNode c = StateNode::Object();
        c["index"] = int(i);
        c["value"] = hex(ts.cram[i], 4);
        c["rgb"] = "#" + hex(rgba & 0xFF, 2) + hex((rgba >> 8) & 0xFF, 2) + hex((rgba >> 16) & 0xFF, 2);
        cram.items.push_back(c);
    }
    ret["cram"] = cram;
    return ret;
}

}  // namespace DeviceState
