// DeviceState::NextDma / NextVideo / NextPalette / NextPorts / NextRegRead - the ZX Spectrum Next's debugger reports
// (declared in emulator/state/devicestate.h). Design: docs/inprogress/2026-10-07-zx-next/design-automation-coverage.md.
//
// R3: a report never has a side effect on the machine. It reads the models' state through const accessors, not the
// ports (a DMA read steps the read sequence, a palette or copper access increments an index).

#include "stdafx.h"

#include "emulator/state/devicestate.h"

#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/z80n/nextregtable.h"
#include "emulator/io/z80n/nextreportquery.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/video/screen.h"

namespace
{
StateNode Unavailable()
{
    StateNode n = StateNode::Object();
    n["available"] = false;
    n["description"] = "not a ZX Spectrum Next";
    return n;
}

PortDecoder_Next* NextDecoder(EmulatorContext* context)
{
    return context ? dynamic_cast<PortDecoder_Next*>(context->pPortDecoder) : nullptr;
}

std::string Hex8(unsigned value) { return StringHelper::Format("0x%02X", value & 0xFFu); }
std::string Hex16(unsigned value) { return StringHelper::Format("0x%04X", value & 0xFFFFu); }

const char* StepName(uint8_t step)
{
    return step == 0 ? "dec" : (step == 1 ? "inc" : "fixed");
}

StateNode DmaPortNode(const NextDma::PortView& port)
{
    StateNode n = StateNode::Object();
    n["address"] = Hex16(port.address);
    n["type"] = port.io ? "io" : "memory";
    n["step"] = StepName(port.step);
    n["timing"] = int(port.timing);
    return n;
}
}  // namespace

namespace DeviceState
{
StateNode NextDma(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    const ::NextDma& dma = decoder->Dma();
    NextMemory& memory = *static_cast<NextMemory*>(context->pMemory);

    StateNode n = StateNode::Object();
    n["available"] = true;
    n["mode"] = dma.Z80Compatible() ? "z80" : "zxn";
    n["enabled"] = dma.Active();
    n["transferring"] = dma.Active();
    n["waiting"] = dma.Waiting();
    n["end_of_block"] = dma.EndOfBlock();
    n["auto_restart"] = dma.AutoRestart();
    n["ce_wait"] = dma.CeWait();
    n["burst"] = dma.Mode() == 1 ? "continuous" : (dma.Mode() == 2 ? "burst" : (dma.Mode() == 0 ? "byte" : "reserved"));
    n["prescaler"] = int(dma.Prescaler());
    n["read_mask"] = Hex8(dma.ReadMask());
    n["read_seq"] = int(dma.ReadSequenceIndex());
    n["status"] = Hex8(dma.StatusByte());
    n["a"] = DmaPortNode(dma.PortA());
    n["b"] = DmaPortNode(dma.PortB());
    n["direction"] = dma.AToB() ? "a_to_b" : "b_to_a";
    n["block_length"] = int(dma.BlockLength());
    n["counter"] = int(dma.Counter());
    n["src"] = Hex16(dma.Source());
    n["dst"] = Hex16(dma.Destination());
    // What the source and the destination address hit NOW (memory ports): a transfer into ROM shows here
    const ::NextDma::PortView source = dma.AToB() ? dma.PortA() : dma.PortB();
    const ::NextDma::PortView destination = dma.AToB() ? dma.PortB() : dma.PortA();
    if (!source.io)
        n["src_kind"] = memory.SlotKind(dma.Source() >> 13);
    if (!destination.io)
        n["dst_kind"] = memory.SlotKind(dma.Destination() >> 13);
    n["holds_bus"] = dma.HoldsBus();
    n["wait_until_28"] = static_cast<uint64_t>(dma.WaitEnd());
    const bool nmiHold = decoder->DivMmc().NmiHold() || decoder->Multiface().NmiHold();
    n["dma_delay"] = decoder->Interrupts().DmaDelay(nmiHold);
    StateNode enables = StateNode::Object();
    uint8_t value = 0;
    const struct { uint8_t reg; const char* key; } kRegs[] = {{0xCC, "nr_cc"}, {0xCD, "nr_cd"}, {0xCE, "nr_ce"}};
    for (const auto& r : kRegs)
        enables[r.key] = Hex8(decoder->Interrupts().ReadNr(r.reg, value) ? value : decoder->Board().Stored(r.reg));
    n["interrupt_enables"] = std::move(enables);
    return n;
}

namespace
{
StateNode ClipNode(const NextVideoRegs& video, unsigned window)
{
    StateNode n = StateNode::Object();
    n["x1"] = int(video.Clip(window, 0));
    n["x2"] = int(video.Clip(window, 1));
    n["y1"] = int(video.Clip(window, 2));
    n["y2"] = int(video.Clip(window, 3));
    return n;
}

const char* TimingFamilyName(uint8_t timing)
{
    switch (timing)
    {
        case 1: return "48K";
        case 2: return "128K";
        case 3: return "+3";
        case 4: return "Pentagon";
        default: return "unknown";
    }
}
}  // namespace

StateNode NextVideo(EmulatorContext* context)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    const NextVideoRegs& video = board.Video();
    auto nr = [&](uint8_t reg) { return board.Stored(reg); };
    const EmulatorState& state = context->emulatorState;

    StateNode n = StateNode::Object();
    n["available"] = true;

    // NR #15: bits 4:2 the layer order (top layer first)
    static const char* const kOrders[8] = {"SLU", "LSU", "SUL", "LUS", "USL", "ULS", "blend", "blend"};
    const unsigned order = (nr(0x15) >> 2) & 7;
    StateNode layers = StateNode::Object();
    layers["value"] = int(order);
    layers["name"] = kOrders[order];
    layers["raw_nr_15"] = Hex8(nr(0x15));
    n["layer_order"] = std::move(layers);
    n["lores"] = (nr(0x15) & 0x80) != 0;

    StateNode ula = StateNode::Object();
    ula["enabled"] = (nr(0x68) & 0x80) == 0;
    const unsigned timex = video.PortFf() & 7;
    ula["mode"] = timex == 6 ? "hires" : (timex == 2 ? "hicolour" : ((timex & 1) ? "screen1" : "standard"));
    ula["port_ff"] = Hex8(video.PortFf());
    ula["shadow"] = (state.p7FFD & 0x08) != 0;
    ula["scroll_x"] = int(nr(0x26));
    ula["scroll_y"] = int(nr(0x27));
    ula["clip"] = ClipNode(video, 2);
    ula["palette"] = (video.PaletteControl() & 0x02) ? 2 : 1;
    ula["ulanext"] = (video.PaletteControl() & 0x01) != 0;
    ula["ulanext_format"] = Hex8(video.UlaNextFormat());
    ula["stencil"] = (nr(0x68) & 0x01) != 0;
    ula["ulaplus"] = (nr(0x68) & 0x08) != 0;
    n["ula"] = std::move(ula);

    StateNode lores = StateNode::Object();
    lores["enabled"] = (nr(0x15) & 0x80) != 0;
    lores["radastan"] = (nr(0x6A) & 0x20) != 0;
    lores["palette_offset"] = int(nr(0x6A) & 0x0F);
    lores["scroll_x"] = int(nr(0x32));
    lores["scroll_y"] = int(nr(0x33));
    n["lores_layer"] = std::move(lores);

    StateNode layer2 = StateNode::Object();
    layer2["enabled"] = video.Layer2Enabled();
    static const char* const kResolutions[4] = {"256x192x8", "320x256x8", "640x256x4", "reserved"};
    layer2["resolution"] = kResolutions[(nr(0x70) >> 4) & 3];
    layer2["bank"] = int(nr(0x12) & 0x7F);
    layer2["shadow_bank"] = int(nr(0x13) & 0x7F);
    layer2["palette_offset"] = int(nr(0x70) & 0x0F);
    layer2["palette"] = (video.PaletteControl() & 0x04) ? 2 : 1;
    layer2["scroll_x"] = int(nr(0x16) | ((nr(0x71) & 1) << 8));
    layer2["scroll_y"] = int(nr(0x17));
    layer2["clip"] = ClipNode(video, 0);
    StateNode map = StateNode::Object();
    map["write"] = video.Layer2MapWrite();
    map["read"] = video.Layer2MapRead();
    map["shadow"] = video.Layer2MapShadow();
    map["segment"] = int(video.Layer2MapSegment());
    map["bank_offset"] = int(video.Layer2Offset());
    layer2["cpu_map"] = std::move(map);
    n["layer2"] = std::move(layer2);

    const uint8_t control = nr(0x6B);
    StateNode tilemap = StateNode::Object();
    tilemap["enabled"] = (control & 0x80) != 0;
    tilemap["columns"] = (control & 0x40) ? 80 : 40;
    tilemap["attributes_in_map"] = (control & 0x20) == 0;
    tilemap["palette"] = (control & 0x10) ? 2 : 1;
    tilemap["text"] = (control & 0x08) != 0;
    tilemap["mode512"] = (control & 0x02) != 0;
    tilemap["on_top"] = (control & 0x01) != 0;
    tilemap["control"] = Hex8(control);
    const bool mapBank7 = (nr(0x6E) & 0x80) != 0;
    const bool tileBank7 = (nr(0x6F) & 0x80) != 0;
    tilemap["map_base"] = Hex8(nr(0x6E));
    tilemap["map_bank"] = mapBank7 ? 7 : 5;
    tilemap["map_offset"] = Hex16((nr(0x6E) & 0x3F) * 256u);
    tilemap["map_wraps_8k"] = mapBank7;  // bank 7 is an 8K RAM for the video side (zxnext.vhd)
    tilemap["tile_base"] = Hex8(nr(0x6F));
    tilemap["tile_bank"] = tileBank7 ? 7 : 5;
    tilemap["tile_offset"] = Hex16((nr(0x6F) & 0x3F) * 256u);
    tilemap["tile_wraps_8k"] = tileBank7;
    tilemap["default_attribute"] = Hex8(nr(0x6C));
    tilemap["transparent_index"] = int(nr(0x4C) & 0x0F);
    tilemap["scroll_x"] = int(nr(0x30) | ((nr(0x2F) & 3) << 8));
    tilemap["scroll_y"] = int(nr(0x31));
    tilemap["clip"] = ClipNode(video, 3);
    n["tilemap"] = std::move(tilemap);

    StateNode sprites = StateNode::Object();
    sprites["enabled"] = (nr(0x15) & 0x01) != 0;
    sprites["over_border"] = (nr(0x15) & 0x02) != 0;
    sprites["clip_over_border"] = (nr(0x15) & 0x20) != 0;
    sprites["zero_on_top"] = (nr(0x15) & 0x40) != 0;
    sprites["clip"] = ClipNode(video, 1);
    n["sprites"] = std::move(sprites);

    StateNode transparency = StateNode::Object();
    transparency["global"] = Hex8(nr(0x14));
    transparency["fallback"] = Hex8(nr(0x4A));
    transparency["sprites"] = Hex8(nr(0x4B));
    n["transparency"] = std::move(transparency);

    StateNode palettes = StateNode::Object();
    palettes["selected"] = int((video.PaletteControl() >> 4) & 7);
    palettes["control"] = Hex8(video.PaletteControl());
    n["palettes"] = std::move(palettes);

    Z80* z80 = context->pCore ? context->pCore->GetZ80() : nullptr;
    const uint32_t frameT = z80 ? state.CpuToBaseT(z80->t) : 0;
    const unsigned perLine = context->pScreen ? context->pScreen->GetTstatesPerLine() : 0;
    StateNode raster = StateNode::Object();
    raster["frame"] = static_cast<uint64_t>(state.frame_counter);
    raster["frame_t"] = static_cast<uint64_t>(frameT);
    raster["tstates_per_line"] = int(perLine);
    raster["vc"] = int(perLine ? frameT / perLine : 0);
    raster["hc"] = int(perLine ? (frameT % perLine) * 2 : 0);  // 7 MHz pixel counts
    raster["frame_tstates"] = int(context->pScreen ? context->pScreen->GetMaxFrameTiming() : 0);
    raster["line_interrupt"] = ((nr(0x22) & 0x02) != 0);
    raster["int_line"] = int(((nr(0x22) & 1) << 8) | nr(0x23));
    raster["current_line"] = int(decoder->Interrupts().CurrentLine());
    n["raster"] = std::move(raster);

    StateNode timing = StateNode::Object();
    timing["family"] = TimingFamilyName(board.Timing());
    timing["hz"] = (board.Timing() != 4 && (nr(0x05) & 0x04)) ? 60 : 50;
    n["timing"] = std::move(timing);
    return n;
}

StateNode NextPalette(EmulatorContext* context, const NextPaletteQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    const NextVideoRegs& video = board.Video();
    const unsigned selected = (video.PaletteControl() >> 4) & 7;

    StateNode n = StateNode::Object();
    n["available"] = true;
    StateNode sel = StateNode::Object();
    sel["palette"] = int(selected);
    sel["name"] = NextPaletteName(selected);
    sel["index"] = int(video.PaletteIndex());
    sel["auto_increment"] = (video.PaletteControl() & 0x80) == 0;
    sel["control"] = Hex8(video.PaletteControl());
    n["selected"] = std::move(sel);
    StateNode transparent = StateNode::Object();
    transparent["global"] = Hex8(board.Stored(0x14));
    transparent["sprites"] = Hex8(board.Stored(0x4B));
    transparent["tilemap"] = int(board.Stored(0x4C) & 0x0F);
    transparent["fallback"] = Hex8(board.Stored(0x4A));
    n["transparent"] = std::move(transparent);

    const unsigned first = query.first > 255 ? 255 : query.first;
    const unsigned last = query.last > 255 ? 255 : query.last;
    StateNode palettes = StateNode::Array();
    for (unsigned p = 0; p < NextVideoRegs::kPalettes; p++)
    {
        const bool wanted = query.palette == NextPaletteQuery::kAll ||
                            (query.palette == NextPaletteQuery::kSelected ? p == selected : static_cast<int>(p) == query.palette);
        if (!wanted)
            continue;
        StateNode palette = StateNode::Object();
        palette["palette"] = int(p);
        palette["name"] = NextPaletteName(p);
        StateNode entries = StateNode::Array();
        for (unsigned i = first; i <= last; i++)
        {
            const uint16_t entry = video.PaletteEntry(p, i);
            StateNode e = StateNode::Object();
            e["index"] = int(i);
            e["rgb9"] = StringHelper::Format("0x%03X", entry & 0x1FFu);
            e["red"] = int((entry >> 6) & 7);
            e["green"] = int((entry >> 3) & 7);
            e["blue"] = int(entry & 7);
            e["priority"] = (entry & 0x200) != 0;  // Layer 2 palettes: the priority bit of the second byte of NR #44
            entries.push(std::move(e));
        }
        palette["entries"] = std::move(entries);
        palettes.push(std::move(palette));
    }
    n["palettes"] = std::move(palettes);
    return n;
}

namespace
{
/// One bit of the internal port enable word (NR #82-#85, zxnext.vhd; nextreg.txt "Internal Port Decoding Enables")
struct EnableBit
{
    unsigned bit;
    const char* ports;
    bool enforced;  ///< the emulator gates the ports by this bit (the others decode regardless)
};

const EnableBit kEnableBits[] = {
    {0, "#FF", false},
    {1, "#7FFD", false},
    {2, "#DFFD", false},
    {3, "#1FFD", false},
    {4, "+3 floating bus", false},
    {5, "#6B zxnDMA", false},
    {6, "#1F Kempston / MD 1", false},
    {7, "#37 Kempston / MD 2", false},
    {8, "#E3 DivMMC control", false},
    {9, "Multiface (two variable ports)", true},
    {10, "#103B, #113B I2C", false},
    {11, "#E7, #EB SPI", false},
    {12, "#133B, #143B, #153B, #163B UART", false},
    {13, "#FADF, #FBDF, #FFDF mouse", false},
    {14, "#57, #5B, #303B sprites", false},
    {15, "#123B Layer 2", false},
    {16, "#FFFD, #BFFD AY", false},
    {17, "#0F, #1F, #4F, #5F DAC (Soundrive mode 1)", true},
    {18, "#F1, #F3, #F9, #FB DAC (Soundrive mode 2)", true},
    {19, "#3F, #5F DAC (Profi Covox stereo)", true},
    {20, "#0F, #4F DAC (Covox stereo)", true},
    {21, "#FB DAC mono (Pentagon / ATM)", true},
    {22, "#B3 DAC mono (GS Covox)", true},
    {23, "#DF DAC mono (SpecDrum), Kempston alias", true},
    {24, "#BF3B, #FF3B ULA+", false},
    {25, "#0B Z80 DMA", false},
    {26, "#EFF7 Pentagon 1024 memory", false},
    {27, "#183B-#1F3B Z80 CTC", false},
};

/// What answers a port. The order is the decoder's (PortDecoder_Next::DecodePortIn / DecodePortOut): the first rule that matches wins
struct PortRule
{
    const char* device;
    bool (*match)(uint16_t port, const PortDecoder_Next& decoder, const NextBoard& board);
    bool read;
    bool write;
    const char* decodedBy;
    int bit;           ///< the enable word bit (-1: none)
    int bit2;          ///< a second bit that also enables it (-1: none)
    const char* readEffect;
    const char* writeEffect;
    const char* channels;  ///< DAC rules: which channels the port feeds
};

uint8_t Low(uint16_t port) { return static_cast<uint8_t>(port); }

const PortRule kRules[] = {
    {"Multiface enable / disable",
     [](uint16_t p, const PortDecoder_Next& d, const NextBoard&) {
         NextMultiface& mf = const_cast<PortDecoder_Next&>(d).Multiface();
         return Low(p) == mf.EnablePort() || Low(p) == mf.DisablePort();
     },
     true, true, "A7:A0 = the Multiface's enable / disable port (NR #0A bits 7:6)", 9, -1,
     "the Multiface answers (page in) when it drives this read", "pages the Multiface in or out", nullptr},
    {"CTC channel", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x3B && (p >> 11) == 0x03; }, true, true,
     "A7:A0 = #3B, A15:A11 = 00011, A10:A8 = the channel", 27, -1, "the channel's down-counter (channels 4-7 read #FF)",
     "the channel's control word or time constant", nullptr},
    {"Layer 2 control", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return p == 0x123B; }, true, true, "A15:A0 = #123B", 15, -1,
     "the control byte as written", "bit 4 = 0: enable / mapping / segment; bit 4 = 1: bank offset; remaps the CPU's view of Layer 2", nullptr},
    {"AY register select",
     [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return (p & 0xC002) == 0xC000 && (p & 0xF002) != 0xD000; }, true, true,
     "A15 = 1, A14 = 1, A1 = 0 (not #DFFD)", 16, -1, "reads the selected AY register (a read of #FFFD returns the data)",
     "selects the AY register; with the turbosound pattern #9C+ also the chip", nullptr},
    {"Kempston joystick 1", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x1F; }, true, false,
     "A7:A0 = #1F", 6, 23, "000FUDLR, active high; no side effect", "", nullptr},
    {"DMA (zxnDMA)", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x6B; }, true, true, "A7:A0 = #6B", 5, -1,
     "the next value of the DMA read sequence (status, counter, addresses by the read mask); sets the zxn mode latch",
     "the next byte of the DMA's WR0-WR6 sequence; the zxn mode latch is set", nullptr},
    {"DMA (Z80 DMA)", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x0B; }, true, true, "A7:A0 = #0B", 25, -1,
     "the next value of the DMA read sequence; sets the Z80 mode latch", "the next byte of the DMA's WR0-WR6 sequence; the Z80 mode latch is set",
     nullptr},
    {"Sprites status / slot select", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return p == 0x303B; }, true, true, "A15:A0 = #303B", 14,
     -1, "bit 1 too many sprites per line, bit 0 collision; the read clears both", "selects the sprite (6:0) and the pattern half (7); resets the attribute byte index",
     nullptr},
    {"Timex video mode (#FF)",
     [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xFF; }, true, true,
     "A7:A0 = #FF", 0, -1, "NR #08 bit 2 = 1: the Timex mode; else the floating bus", "bits 5:0 = the Timex screen mode (alias of NR #69)", nullptr},
    {"DivMMC control", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xE3; }, true, true, "A7:A0 = #E3", 8, -1,
     "the control register", "CONMEM, MAPRAM, bank", nullptr},
    {"I2C", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return p == 0x103B || p == 0x113B; }, true, true, "#103B SCL, #113B SDA", 10, -1,
     "the line state", "drives the line", nullptr},
    {"NextREG select", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return p == 0x243B; }, true, true, "A15:A0 = #243B", -1, -1,
     "the selected register number", "selects the register #253B reads and writes", nullptr},
    {"NextREG data", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return p == 0x253B; }, true, true, "A15:A0 = #253B", -1, -1,
     "reads the selected register (the board counts the read)",
     "writes the register selected by #243B through the board's single write choke point; the NextREG journal sees it as source port", nullptr},
    {"SPI select", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xE7; }, false, true, "A7:A0 = #E7", 11, -1, "",
     "selects the SD card (or none)", nullptr},
    {"SPI data", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xEB; }, true, true, "A7:A0 = #EB", 11, -1,
     "the byte the card answered; starts the next exchange", "starts a byte exchange with the selected card (16 clocks)", nullptr},
    {"AY register data", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return (p & 0xC002) == 0x8000; }, false, true,
     "A15 = 1, A14 = 0, A1 = 0", 16, -1, "", "writes the selected AY register", nullptr},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x1F; }, false, true, "A7:A0 = #1F", 17, -1, "",
     "sets the DAC channel", "A (Soundrive 1)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x0F; }, false, true, "A7:A0 = #0F", 17, 20, "",
     "sets the DAC channel", "B (Soundrive 1, Covox stereo)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x4F; }, false, true, "A7:A0 = #4F", 17, 20, "",
     "sets the DAC channel", "C (Soundrive 1, Covox stereo)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x5F; }, false, true, "A7:A0 = #5F", 17, 19, "",
     "sets the DAC channel", "D (Soundrive 1, Profi Covox)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xF1; }, false, true, "A7:A0 = #F1", 18, -1, "",
     "sets the DAC channel", "A (Soundrive 2)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xF3; }, false, true, "A7:A0 = #F3", 18, -1, "",
     "sets the DAC channel", "B (Soundrive 2)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xF9; }, false, true, "A7:A0 = #F9", 18, -1, "",
     "sets the DAC channel", "C (Soundrive 2)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xFB; }, false, true, "A7:A0 = #FB", 18, 21, "",
     "sets the DAC channel", "D (Soundrive 2) / A+D mono (Pentagon, ATM)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x3F; }, false, true, "A7:A0 = #3F", 19, -1, "",
     "sets the DAC channel", "A (Profi Covox)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xB3; }, false, true, "A7:A0 = #B3", 22, -1, "",
     "sets the DAC channel", "B+C mono (GS Covox)"},
    {"DAC", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0xDF; }, false, true, "A7:A0 = #DF", 23, -1, "",
     "sets the DAC channel", "A+D mono (SpecDrum)"},
    {"Sprite attribute upload", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x57; }, false, true, "A7:A0 = #57", 14, -1, "",
     "the next attribute byte of the selected sprite", nullptr},
    {"Sprite pattern upload", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return Low(p) == 0x5B; }, false, true, "A7:A0 = #5B", 14, -1, "",
     "the next pattern byte", nullptr},
    {"Memory paging #7FFD", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return (p & 0xC002) == 0x4000; }, false, true,
     "A15 = 0, A14 = 1, A1 = 0", 1, -1, "", "ROM / bank / screen select / lock; rewrites the MMU slots", nullptr},
    {"Memory paging #1FFD", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return (p & 0xF002) == 0x1000; }, false, true,
     "A15:A12 = 0001, A1 = 0", 3, -1, "", "+3 paging: special mode, ROM high bit; rewrites the MMU slots", nullptr},
    {"Memory paging #DFFD", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return (p & 0xF002) == 0xD000; }, false, true,
     "A15:A12 = 1101, A1 = 0", 2, -1, "", "bank bits 6:4 (Pentagon 512 / 1024); rewrites the MMU slots", nullptr},
    {"ULA", [](uint16_t p, const PortDecoder_Next&, const NextBoard&) { return (p & 1) == 0; }, true, true, "A0 = 0", -1, -1,
     "keyboard half-rows by A15:A8, EAR in bit 6", "border colour (2:0), MIC (3), beeper (4)", nullptr},
};
}  // namespace

StateNode NextPorts(EmulatorContext* context, const NextPortsQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    const uint32_t word = static_cast<uint32_t>(board.Stored(0x82)) | (static_cast<uint32_t>(board.Stored(0x83)) << 8) |
                          (static_cast<uint32_t>(board.Stored(0x84)) << 16) | (static_cast<uint32_t>(board.Stored(0x85)) << 24);

    StateNode n = StateNode::Object();
    n["available"] = true;
    if (query.describe)
    {
        StateNode d = StateNode::Object();
        d["port"] = Hex16(query.port);
        d["access"] = query.write ? "write" : "read";
        const PortRule* rule = nullptr;
        for (const PortRule& r : kRules)
        {
            if ((query.write ? r.write : r.read) && r.match(query.port, *decoder, board))
            {
                // the Timex port answers a read only with NR #08 bit 2
                if (!query.write && Low(query.port) == 0xFF && r.bit == 0 && !(board.Stored(0x08) & 0x04))
                    continue;
                rule = &r;
                break;
            }
        }
        if (!rule)
        {
            d["device"] = "(not decoded by the Next)";
            d["decoded_by"] = "no rule of the Next's decoder matches; the 128K base board sees it (floating bus on a read)";
            d["enabled"] = true;
            d["side_effect"] = "none";
        }
        else
        {
            d["device"] = rule->device;
            if (rule->channels)
                d["channels"] = rule->channels;
            d["decoded_by"] = rule->decodedBy;
            const bool bitOn = rule->bit < 0 || ((word >> rule->bit) & 1) || (rule->bit2 >= 0 && ((word >> rule->bit2) & 1));
            d["enabled"] = bitOn;
            if (rule->bit >= 0)
            {
                StateNode by = StateNode::Object();
                by["nr"] = Hex8(0x82 + rule->bit / 8);
                by["bit"] = rule->bit % 8;
                by["word_bit"] = rule->bit;
                by["ports"] = kEnableBits[rule->bit].ports;
                if (rule->bit2 >= 0)
                {
                    by["also_nr"] = Hex8(0x82 + rule->bit2 / 8);
                    by["also_bit"] = rule->bit2 % 8;
                }
                d["enabled_by"] = std::move(by);
                d["enforced"] = kEnableBits[rule->bit].enforced;
            }
            d["side_effect"] = query.write ? rule->writeEffect : rule->readEffect;
        }
        n["describe"] = std::move(d);
    }

    StateNode ew = StateNode::Object();
    ew["value"] = StringHelper::Format("0x%08X", word);
    for (uint8_t reg = 0x82; reg <= 0x85; reg++)
        ew[StringHelper::Format("nr_%02x", reg)] = Hex8(board.Stored(reg));
    ew["restored_by"] = (word & 0x80000000u) ? "soft reset" : "hard reset";  // bit 31 chooses the reset that sets the word back to all ones
    StateNode bits = StateNode::Array();
    for (const EnableBit& b : kEnableBits)
    {
        StateNode e = StateNode::Object();
        e["bit"] = int(b.bit);
        e["nr"] = Hex8(0x82 + b.bit / 8);
        e["ports"] = b.ports;
        e["enabled"] = ((word >> b.bit) & 1) != 0;
        e["enforced"] = b.enforced;
        bits.push(std::move(e));
    }
    ew["bits"] = std::move(bits);
    n["enable_word"] = std::move(ew);
    n["note"] = "enforced = the emulator gates the ports by that bit; the other ports decode regardless of the enable word";
    return n;
}

StateNode NextRegRead(EmulatorContext* context, const NextRegReadQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    StateNode n = StateNode::Object();
    n["available"] = true;
    n["selected"] = Hex8(board.SelectedRegister());
    if (query.reg >= 0)
    {
        const uint8_t reg = static_cast<uint8_t>(query.reg);
        const NextRegInfo* info = FindNextReg(reg);
        n["reg"] = Hex8(reg);
        n["name"] = info ? info->name : "(not in the register table)";
        n["access"] = info ? std::string(info->readable ? "R" : "") + (info->writable ? "W" : "") : "";
        const uint8_t value = (info && !info->readable) ? board.Stored(reg) : board.Read(reg);
        n["value"] = Hex8(value);
        n["stored"] = Hex8(board.Stored(reg));
        if (info && info->hasReset)
            n["reset"] = Hex8(info->reset);
        const std::string decoded = NextRegDecode(reg, value);
        if (!decoded.empty())
            n["decoded"] = decoded;
        return n;
    }
    n["changed_only"] = query.changed;
    StateNode regs = StateNode::Array();
    size_t count = 0;
    const NextRegInfo* table = NextRegTable(count);
    for (size_t i = 0; i < count; i++)
    {
        const NextRegInfo& r = table[i];
        const uint8_t stored = board.Stored(r.number);
        if (query.changed && (r.hasReset ? stored == r.reset : stored == 0))
            continue;
        StateNode reg = StateNode::Object();
        reg["nr"] = Hex8(r.number);
        reg["name"] = r.name;
        reg["access"] = std::string(r.readable ? "R" : "") + (r.writable ? "W" : "");
        reg["value"] = Hex8(r.readable ? board.Read(r.number) : stored);
        reg["stored"] = Hex8(stored);
        if (r.hasReset)
            reg["reset"] = Hex8(r.reset);
        regs.push(std::move(reg));
    }
    n["registers"] = std::move(regs);
    return n;
}

namespace
{
/// One copper word as the copper executes it (copper.vhd): bit 15 = WAIT (hpos 14:9 in units of 8 pixels, line 8:0; line #1FF /
/// hpos 63 is the HALT idiom), else MOVE (register 14:8, value 7:0; register 0 is a NOP)
StateNode CopperWordNode(unsigned index, uint16_t word, unsigned pc)
{
    StateNode n = StateNode::Object();
    n["index"] = int(index);
    n["word"] = Hex16(word);
    if (word & 0x8000)
    {
        const unsigned hpos = (word >> 9) & 0x3F, vpos = word & 0x1FF;
        if (hpos == 0x3F && vpos == 0x1FF)
        {
            n["op"] = "halt";
            n["text"] = "HALT";
        }
        else
        {
            n["op"] = "wait";
            n["hpos"] = int(hpos);
            n["vpos"] = int(vpos);
            n["text"] = StringHelper::Format("WAIT line %u hpos %u", vpos, hpos);
        }
    }
    else
    {
        const uint8_t reg = (word >> 8) & 0x7F, value = word & 0xFF;
        if (reg == 0)
        {
            n["op"] = "nop";
            n["text"] = "NOP";
        }
        else
        {
            n["op"] = "move";
            n["reg"] = Hex8(reg);
            n["value"] = Hex8(value);
            if (const NextRegInfo* info = FindNextReg(reg))
                n["reg_name"] = info->name;
            n["text"] = StringHelper::Format("MOVE NR #%02X <- #%02X", reg, value);
        }
    }
    n["current"] = index == pc;
    return n;
}
}  // namespace

StateNode NextCopper(EmulatorContext* context, const NextCopperQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    const ::NextCopper& copper = decoder->Board().Copper();
    static const char* const kModes[4] = {"stopped", "restart_loop", "continue_loop", "restart_each_frame"};

    StateNode n = StateNode::Object();
    n["available"] = true;
    StateNode control = StateNode::Object();
    control["mode"] = int(copper.Mode());
    control["name"] = kModes[copper.Mode() & 3];
    control["raw_nr_62"] = Hex8(copper.ReadControl());
    n["control"] = std::move(control);
    n["running"] = copper.Mode() != 0;
    n["address"] = int(copper.WriteAddress());
    n["word_index"] = int(copper.WriteAddress() >> 1);
    n["address_byte"] = (copper.WriteAddress() & 1) ? "lsb" : "msb";
    n["pc"] = int(copper.Pc());
    n["line_offset"] = int(copper.ReadOffset());
    unsigned length = 0;
    for (unsigned i = 0; i < 1024; i++)
        if (copper.Instruction(i))
            length = i + 1;
    n["list_length"] = int(length);

    const unsigned first = query.first > 1023 ? 1023 : query.first;
    const unsigned end = first + query.count > 1024 ? 1024 : first + query.count;
    StateNode list = StateNode::Array();
    for (unsigned i = first; i < end; i++)
        list.push(CopperWordNode(i, copper.Instruction(i), copper.Pc()));
    n["instructions"] = std::move(list);
    if (copper.Pc() < first || copper.Pc() >= end)
    {
        StateNode around = StateNode::Array();
        const unsigned from = copper.Pc() >= 3 ? copper.Pc() - 3u : 0u;
        for (unsigned i = from; i < from + 8 && i < 1024; i++)
            around.push(CopperWordNode(i, copper.Instruction(i), copper.Pc()));
        n["around_pc"] = std::move(around);
    }
    if (query.raw)
    {
        std::string hex;
        hex.reserve(4096);
        for (unsigned i = 0; i < 1024; i++)
            hex += StringHelper::Format("%04X", copper.Instruction(i));
        n["raw"] = std::move(hex);
    }
    return n;
}

StateNode NextSprites(EmulatorContext* context, const NextSpritesQuery& query)
{
    PortDecoder_Next* decoder = NextDecoder(context);
    if (!decoder)
        return Unavailable();
    NextBoard& board = decoder->Board();
    const ::NextSprites& sprites = board.Sprites();
    const NextVideoRegs& video = board.Video();
    const uint8_t control = board.Stored(0x15);

    StateNode n = StateNode::Object();
    n["available"] = true;
    n["enabled"] = (control & 0x01) != 0;
    n["over_border"] = (control & 0x02) != 0;
    n["clip_over_border"] = (control & 0x20) != 0;
    n["zero_on_top"] = (control & 0x40) != 0;
    n["clip"] = ClipNode(video, 1);
    StateNode flags = StateNode::Object();
    flags["collision"] = sprites.CollisionFlag();  // read-and-cleared through port #303B; the report only looks
    flags["too_many"] = sprites.TooManyFlag();
    n["flags"] = std::move(flags);
    n["upload_slot"] = int(sprites.UploadSlot());
    n["mirror_index"] = int(sprites.ReadMirrorSprite());

    ::NextSprites::Info info[::NextSprites::kSprites];
    sprites.Describe(info);
    unsigned visible = 0;
    for (const auto& i : info)
        visible += i.visible ? 1 : 0;
    n["visible_count"] = int(visible);

    const unsigned first = query.first > 127 ? 127 : query.first;
    const unsigned end = first + query.count > 128 ? 128 : first + query.count;
    StateNode list = StateNode::Array();
    for (unsigned index = first; index < end; index++)
    {
        const ::NextSprites::Info& i = info[index];
        if (!query.all && !i.visible)
            continue;
        StateNode e = StateNode::Object();
        e["index"] = int(index);
        e["kind"] = i.relative ? "relative" : (i.extended ? "anchor" : "basic");
        if (i.relative)
        {
            e["anchor"] = i.anchor;
            e["offset_x"] = i.offsetX;
            e["offset_y"] = i.offsetY;
            e["relative_pattern"] = i.relativePattern;
            e["relative_palette"] = i.relativePalette;
        }
        else if (i.extended)
            e["unified"] = i.unified;
        e["x"] = i.x;
        e["y"] = i.y;
        e["pattern"] = int(i.pattern);
        e["four_bit"] = i.fourBit;
        e["palette_offset"] = int(i.palette);
        e["x_mirror"] = i.xMirror;
        e["y_mirror"] = i.yMirror;
        e["rotate"] = i.rotate;
        e["scale_x"] = int(i.xScale);
        e["scale_y"] = int(i.yScale);
        e["visible"] = i.visible;
        std::string bytes;
        for (unsigned b = 0; b < 5; b++)
            bytes += StringHelper::Format(b ? " %02X" : "%02X", sprites.Attribute(index, b));
        e["bytes"] = std::move(bytes);
        list.push(std::move(e));
    }
    n["sprites"] = std::move(list);

    // The 16K pattern RAM: how much is in use, and which 256-byte (8-bit) patterns hold anything
    unsigned nonZero = 0;
    StateNode used = StateNode::Array();
    for (unsigned pattern = 0; pattern < 64; pattern++)
    {
        bool any = false;
        for (unsigned b = 0; b < 256; b++)
            if (sprites.PatternByte(pattern * 256 + b))
            {
                nonZero++;
                any = true;
            }
        if (any)
            used.push(int(pattern));
    }
    StateNode memory = StateNode::Object();
    memory["size"] = int(::NextSprites::kPatternBytes);
    memory["non_zero_bytes"] = int(nonZero);
    memory["used_patterns_8bit"] = int(used.items.size());
    memory["used"] = std::move(used);
    n["pattern_memory"] = std::move(memory);
    return n;
}
}  // namespace DeviceState
