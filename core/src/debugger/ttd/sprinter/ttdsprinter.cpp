#include "stdafx.h"

#include "ttdsprinter.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "3rdparty/z84c15/z84c15.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/sprinter/screensprinter.h"

namespace ttd
{

namespace
{
/// Little-endian field writer / reader over a caller's buffer
struct Writer
{
    uint8_t* p;
    void U8(uint8_t v) { *p++ = v; }
    void U16(uint16_t v)
    {
        U8(static_cast<uint8_t>(v));
        U8(static_cast<uint8_t>(v >> 8));
    }
    void U32(uint32_t v)
    {
        U16(static_cast<uint16_t>(v));
        U16(static_cast<uint16_t>(v >> 16));
    }
    void U64(uint64_t v)
    {
        U32(static_cast<uint32_t>(v));
        U32(static_cast<uint32_t>(v >> 32));
    }
    void Bytes(const void* src, size_t n)
    {
        std::memcpy(p, src, n);
        p += n;
    }
    void Zeros(size_t n)
    {
        std::memset(p, 0, n);
        p += n;
    }
};

struct Reader
{
    const uint8_t* p;
    uint8_t U8() { return *p++; }
    uint16_t U16()
    {
        const uint16_t lo = U8();
        return static_cast<uint16_t>(lo | (U8() << 8));
    }
    uint32_t U32()
    {
        const uint32_t lo = U16();
        return lo | (static_cast<uint32_t>(U16()) << 16);
    }
    uint64_t U64()
    {
        const uint64_t lo = U32();
        return lo | (static_cast<uint64_t>(U32()) << 32);
    }
    void Bytes(void* dst, size_t n)
    {
        std::memcpy(dst, p, n);
        p += n;
    }
};

uint64_t Fnv1a(const uint8_t* data, size_t size, uint64_t h = 0xcbf29ce484222325ULL)
{
    for (size_t i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t HashOf(const TTDSerializable& device)
{
    std::vector<uint8_t> blob(device.TTDStateSize());
    device.TTDSaveState(blob.data());
    return Fnv1a(blob.data(), blob.size());
}
}  // namespace

/// region <TTDSprinterPld>

// Layout (v1):
//    0    1  version
//    1  112  SprinterPldState (fixed-width fields, no padding: sprinterpldstate.h)
//  113    1  code #89 Covox-Blaster control (the decoder's _cblControl)
//  114    1  powered on (the next reset is a RESET button, not a power-on)
//  115    8  frame of the first port read after the last PLD reset (i64, -1 = not yet)
//  123    2  its PC
//  125    1  INT source: mode page (RGMOD bit 0 as the INT list uses it)
//  126    2  INT source: frame lines of the INT list (320 / 312)
//  128    8  INT source: the pulse the last acknowledge ended (i64, -1 = none)
//  136    1  INT source: the PLD's INT flip-flop holds a keyboard INT
//  137    2  the frame height the raster runs with (ScreenSprinter; 0 = no Sprinter screen)
//  139   32  active configuration module: name, zero padded (the registry index is not stable)
//  171    2  module state room M (u16): the largest StateSize() of the registered modules (Standard: 0)
//  173    2  module state: bytes the active module uses (u16)
//  175    M  module state
//  175+M  2  accelerator state: bytes (u16)
//  177+M  A  accelerator state (PortDecoder_Sprinter::SaveAccelState)
// The WD1793's command in flight (its rate-retry search included) is the Wd1793Context blob

size_t TTDSprinterPld::ModuleStateRoom() const
{
    size_t room = 0;
    const SprinterPldConfigurationRegistry& registry = _decoder.GetRegistry();
    for (size_t i = 0; i < registry.Count(); i++)
        room = std::max(room, registry.At(i).StateSize());
    return room;
}

size_t TTDSprinterPld::TTDStateSize() const
{
    return kFixedSize + ModuleStateRoom() + 2 + _decoder.AccelStateSize();
}

void TTDSprinterPld::TTDSaveState(uint8_t* dst) const
{
    static_assert(sizeof(SprinterPldState) == 112, "SprinterPldState changed: bump TTDSprinterPld::kVersion");
    if (!dst)
        return;

    const PortDecoder_Sprinter& d = _decoder;
    Writer w{dst};
    w.U8(kVersion);
    w.Bytes(&d._pld, sizeof(SprinterPldState));
    w.U8(d._cblControl);
    w.U8(d._poweredOn ? 1 : 0);
    w.U64(static_cast<uint64_t>(d._dcpOpenedFrame));
    w.U16(d._dcpOpenedPc);

    const SprinterIntSource& ints = d._intSource;
    w.U8(ints.ModePage());
    w.U16(ints.FrameLines());
    w.U64(static_cast<uint64_t>(ints.AckedPulse()));
    w.U8(ints.KeyboardIntLatched() ? 1 : 0);

    const EmulatorContext* context = d._context;
    const auto* screen = context ? dynamic_cast<const ScreenSprinter*>(context->pScreen) : nullptr;
    w.U16(screen ? screen->FrameLines() : 0);


    const SprinterPldConfiguration& module = d.ActiveModule();
    char name[kModuleNameSize] = {};
    const std::string& moduleName = module.Descriptor().name;
    std::memcpy(name, moduleName.data(), std::min(moduleName.size(), kModuleNameSize));
    w.Bytes(name, kModuleNameSize);

    const size_t room = ModuleStateRoom();
    const size_t used = std::min(module.StateSize(), room);
    w.U16(static_cast<uint16_t>(room));
    w.U16(static_cast<uint16_t>(used));
    if (used)
        module.SaveState(w.p);
    w.p += used;
    w.Zeros(room - used);

    const size_t accel = d.AccelStateSize();
    w.U16(static_cast<uint16_t>(accel));
    if (accel)
        d.SaveAccelState(w.p);
}

void TTDSprinterPld::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;

    PortDecoder_Sprinter& d = _decoder;
    Reader r{src + 1};
    r.Bytes(&d._pld, sizeof(SprinterPldState));
    d._cblControl = r.U8();
    d._poweredOn = r.U8() != 0;
    d._dcpOpenedFrame = static_cast<int64_t>(r.U64());
    d._dcpOpenedPc = r.U16();

    const uint8_t modePage = r.U8();
    const uint16_t intLines = r.U16();
    const auto ackedPulse = static_cast<int64_t>(r.U64());
    const bool keyboardInt = r.U8() != 0;
    d._intSource.RestoreState(modePage, intLines, ackedPulse, keyboardInt);

    EmulatorContext* context = d._context;
    const uint16_t screenLines = r.U16();
    if (auto* screen = context ? dynamic_cast<ScreenSprinter*>(context->pScreen) : nullptr; screen && screenLines)
        screen->RestoreFrameLines(screenLines);

    // The module by name: its registry index depends on what this build registered
    char name[kModuleNameSize + 1] = {};
    r.Bytes(name, kModuleNameSize);
    const int index = d._registry.FindByName(name);
    if (index >= 0)
        d._pld.configModule = static_cast<uint8_t>(index);
    else if (d._pld.configModule >= d._registry.Count())
        d._pld.configModule = static_cast<uint8_t>(SprinterPldConfigurationRegistry::kStandardIndex);

    const size_t room = r.U16();
    const size_t used = r.U16();
    SprinterPldConfiguration& module = d.ActiveModule();
    // Another module set (room) is another blob size too, which the registry refuses before this point
    if (room == ModuleStateRoom())
    {
        if (used && used <= room && used == module.StateSize())
            module.LoadState(r.p);
        r.p += room;

        const size_t accel = r.U16();
        if (accel && accel == d.AccelStateSize())
            d.LoadAccelState(r.p);
    }

    // The CPU's frame geometry follows a restored frame height (config.frame)
    if (context && context->pCore && context->pCore->GetZ80())
        context->pCore->GetZ80()->RecomputeFrameTiming();

    d.OnTtdStateLoaded();
}

uint64_t TTDSprinterPld::TTDHashState() const
{
    return HashOf(*this);
}

/// endregion </TTDSprinterPld>

/// region <TTDSprinterZ84>

// Layout (v1): version, then Z84C15::SaveState (z84c15.h: system registers, wait generator, watchdog,
// CTC, SIO with its receive FIFOs, PIO; every daisy-chain source's IP / IUS)

size_t TTDSprinterZ84::TTDStateSize() const
{
    return 1 + Z84Lib::Z84C15::kStateSize;
}

void TTDSprinterZ84::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    dst[0] = kVersion;
    _decoder.GetZ84().SaveState(dst + 1);
}

void TTDSprinterZ84::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    _decoder.GetZ84().LoadState(src + 1);
    _decoder.OnTtdStateLoaded();  // the loader's fast RAM window follows CSBR
}

uint64_t TTDSprinterZ84::TTDHashState() const
{
    return HashOf(*this);
}

/// endregion </TTDSprinterZ84>

/// region <TTDSprinterInput>

// Layout (v1):
//    0   1  version
//    1  16  keyboard: the bytes on the way (ring)
//   17   1  ring head
//   18   1  ring count
//   19   1  overflow: the #00 overrun code is due
//   20   1  typematic key (PcKey, 0 = none)
//   21  16  keys held (bitmap by PcKey)
//   37   8  end of the frame of the ring's first byte (base T-states)
//   45   8  end of the last frame sent
//   53   8  next typematic make
//   61   3  serial mouse: the packet
//   64   1  its bytes delivered (3 = none in flight)
//   65   1  the counters last reported: X
//   66   1  Y
//   67   1  buttons (active low)
//   68   1  a sample is held (the next one is compared with it)
//   69   8  end of the frame of the packet's next byte (base T-states)
//   77   8  keyboard bytes the SIO refused (statistics)

void TTDSprinterInput::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    SprinterInput& input = _decoder.GetInput();
    const Ps2KeyboardStream::State& k = input.KeyboardStream().GetState();
    const MsSerialMouse::State& m = input.SerialMouse().GetState();

    Writer w{dst};
    w.U8(kVersion);
    w.Bytes(k.queue, sizeof(k.queue));
    w.U8(k.head);
    w.U8(k.count);
    w.U8(k.overflow);
    w.U8(k.repeatKey);
    w.Bytes(k.held, sizeof(k.held));
    w.U64(k.nextByteAt);
    w.U64(k.lastByteAt);
    w.U64(k.repeatAt);
    w.Bytes(m.packet, sizeof(m.packet));
    w.U8(m.sent);
    w.U8(m.lastX);
    w.U8(m.lastY);
    w.U8(m.lastButtons);
    w.U8(m.synced);
    w.U64(m.nextByteAt);
    w.U64(input.KeyboardOverruns());
}

void TTDSprinterInput::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    SprinterInput& input = _decoder.GetInput();
    Ps2KeyboardStream::State k{};
    MsSerialMouse::State m{};

    Reader r{src + 1};
    r.Bytes(k.queue, sizeof(k.queue));
    k.head = r.U8();
    k.count = r.U8();
    k.overflow = r.U8();
    k.repeatKey = r.U8();
    r.Bytes(k.held, sizeof(k.held));
    k.nextByteAt = r.U64();
    k.lastByteAt = r.U64();
    k.repeatAt = r.U64();
    r.Bytes(m.packet, sizeof(m.packet));
    m.sent = r.U8();
    m.lastX = r.U8();
    m.lastY = r.U8();
    m.lastButtons = r.U8();
    m.synced = r.U8();
    m.nextByteAt = r.U64();
    input.SetKeyboardOverruns(r.U64());

    input.KeyboardStream().SetState(k);
    input.SerialMouse().SetState(m);
    _decoder.OnTtdStateLoaded();  // a byte on its way with the keyboard INT on needs the step hook
}

uint64_t TTDSprinterInput::TTDHashState() const
{
    return HashOf(*this);
}

/// endregion </TTDSprinterInput>

/// region <TTDSprinterVideoRam>

size_t TTDSprinterVideoRam::TTDStateSize() const
{
    return 1 + SprinterVideoRam::kSize;
}

void TTDSprinterVideoRam::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    dst[0] = kVersion;
    std::memcpy(dst + 1, _decoder.GetVideoRam().Data(), SprinterVideoRam::kSize);
}

void TTDSprinterVideoRam::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    SprinterVideoRam& vram = _decoder.GetVideoRam();
    std::memcpy(vram.Data(), src + 1, SprinterVideoRam::kSize);
    vram.RefreshPalette();               // the pens' RGBA cache
    _decoder.GetIntSource().Invalidate();  // the INT list comes from the mode table
}

uint64_t TTDSprinterVideoRam::TTDHashState() const
{
    return Fnv1a(_decoder.GetVideoRam().Data(), SprinterVideoRam::kSize);
}

/// endregion </TTDSprinterVideoRam>

/// region <TTDSprinterFastRam>

void TTDSprinterFastRam::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    dst[0] = kVersion;
    SprinterMemory* memory = _decoder.GetSprinterMemory();
    if (memory)
        std::memcpy(dst + 1, memory->FastRam(), kFastRamSize);
    else
        std::memset(dst + 1, 0, kFastRamSize);
}

void TTDSprinterFastRam::TTDLoadState(const uint8_t* src)
{
    if (!src || src[0] != kVersion)
        return;
    if (SprinterMemory* memory = _decoder.GetSprinterMemory())
        std::memcpy(memory->FastRam(), src + 1, kFastRamSize);
}

uint64_t TTDSprinterFastRam::TTDHashState() const
{
    SprinterMemory* memory = _decoder.GetSprinterMemory();
    return memory ? Fnv1a(memory->FastRam(), kFastRamSize) : 0;
}

/// endregion </TTDSprinterFastRam>

}  // namespace ttd
