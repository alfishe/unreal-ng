// DeviceState::TsConf - the TS-Conf report every automation interface renders
// (declared in emulator/state/devicestate.h; lives here so the shared state
// code names no TS-Conf type)

#include "stdafx.h"

#include <cstdio>

#include "emulator/state/devicestate.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconftsu.h"
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

    /// The active descriptors the TSU processes (the ones behind the third LEAP are never reached)
    int ProcessedActiveSprites(const TsConfState& ts)
    {
        uint32_t bounds[4];
        TsConfTsu::LayerBounds(ts, bounds);
        int active = 0;
        for (uint32_t d = 0; d < bounds[3]; d++)
            active += (ts.sfile[d * 3] & 0x2000) ? 1 : 0;
        return active;
    }

    uint16_t Nine(uint8_t low, uint8_t high) { return static_cast<uint16_t>(low | ((high & 1u) << 8)); }

    /// A DMA address as its three registers hold it (AL word bits 6:0 in bits 7:1, AH word bits 12:7,
    /// AX word bits 20:13; TsConfDma::WriteAddress), as a byte address
    uint32_t DmaAddress(uint8_t low, uint8_t high, uint8_t page)
    {
        const uint32_t word = (static_cast<uint32_t>(page) << 13) | (static_cast<uint32_t>(high & 0x3F) << 7) | (low >> 1);
        return word * 2;
    }
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

    // The emulated firmware build ([MISC] TS_VDAC, hardware-spec §0.1)
    {
        const uint8_t vdac = decoder->VdacVersion();
        StateNode b = StateNode::Object();
        b["vdac_ver"] = int(vdac);
        b["vdac"] = vdac == 0 ? "none (2-bit DAC + PWM)" : (vdac == 1 ? "3-bit" : (vdac == 2 ? "4-bit" : (vdac == 3 ? "5-bit" : "VDAC2")));
        b["blt2"] = vdac != 0;
        ret["build"] = b;
    }

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
        m["fm_maps"] = int(r[TsConfReg::FMaps]);  // raw: bit 4 FM_EN, bits 3:0 the window's address A15:12
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
        tsu["active_sprites"] = ProcessedActiveSprites(ts);
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
        // The tile graphics pages the line is drawn with: copies of T0_G_PAGE / T1_G_PAGE taken at the
        // line start, so a write shows from the next line on (tsu.tile0_page / tile1_page: the registers)
        line["t0_gpage"] = int(ts.latT0GPage);
        line["t1_gpage"] = int(ts.latT1GPage);
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
        // As the CPU last wrote them (source / destination above are the live counters)
        d["programmed_source"] = static_cast<uint64_t>(DmaAddress(r[TsConfReg::DmaSAl], r[TsConfReg::DmaSAh], r[TsConfReg::DmaSAx]));
        d["programmed_destination"] =
            static_cast<uint64_t>(DmaAddress(r[TsConfReg::DmaDAl], r[TsConfReg::DmaDAh], r[TsConfReg::DmaDAx]));
        const uint8_t ctrl = r[TsConfReg::DmaCtrl];
        StateNode c = StateNode::Object();
        c["raw"] = int(ctrl);
        c["device"] = int(((ctrl >> 4) & 0x08) | (ctrl & 0x07));  // DDEV: {bit 7, bits 2:0}
        c["opt"] = (ctrl & 0x40) != 0;                              // BLT2 saturation
        c["s_align"] = (ctrl & 0x20) != 0;
        c["d_align"] = (ctrl & 0x10) != 0;
        c["a_sz"] = (ctrl & 0x08) != 0;                             // 512-byte blocks
        d["ctrl"] = c;
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
        ret["sys_config"] = int(r[TsConfReg::SysConfig]);
        ret["cache_en"] = (r[TsConfReg::SysConfig] & 0x04) != 0;
        StateNode sd = StateNode::Object();
        sd["present"] = decoder->GetSdCard().present();
        sd["selected"] = decoder->GetZController().IsSelected();
        ret["sd"] = sd;
    }

    // The register file as the CPU last wrote it (#00-#47, register = port #xxAF's high byte): the
    // write-only registers have no other way out, and a debugger board shows them raw
    {
        StateNode regs = StateNode::Array();
        for (size_t i = 0; i < TsConfReg::kCount; i++)
            regs.push(int(r[i]));
        ret["regs"] = regs;
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
    uint32_t bounds[4];
    TsConfTsu::LayerBounds(ts, bounds);
    for (uint32_t d = 0; d < 85; d++)
    {
        const uint16_t w0 = ts.sfile[d * 3];
        const uint16_t w1 = ts.sfile[d * 3 + 1];
        const uint16_t w2 = ts.sfile[d * 3 + 2];
        StateNode sp = StateNode::Object();
        sp["index"] = int(d);
        sp["active"] = (w0 & 0x2000) != 0;
        sp["leap"] = (w0 & 0x4000) != 0;
        // "ended": behind the third LEAP, never processed
        sp["layer"] = d < bounds[1] ? "s0" : (d < bounds[2] ? "s1" : (d < bounds[3] ? "s2" : "ended"));
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
    }
    ret["active_sprites"] = ProcessedActiveSprites(ts);
    ret["sprites"] = sprites;

    // CRAM: the 256 palette cells with their colors (no-VDAC curve, as drawn)
    StateNode cram = StateNode::Array();
    for (uint32_t i = 0; i < 256; i++)
    {
        const uint32_t rgba = ScreenTSConf::CramToRgba(ts.cram[i], context->config.ts_vdac);
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
