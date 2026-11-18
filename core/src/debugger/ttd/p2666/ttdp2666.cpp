#include "ttdp2666.h"

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/platforms/p2666/p2666betadisk.h"
#include "emulator/platforms/p2666/p2666uart.h"
#include "emulator/ports/models/portdecoder_p2666.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/p2666audio.h"
#include "emulator/video/screen.h"

#include <cstring>
#include <type_traits>
#include <vector>

namespace
{
constexpr uint8_t kVersion = 1;

/// The three passes over one field list: its size, the save, the load
struct Sizer
{
    size_t size = 0;
    template <typename T>
    void operator()(T&)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a blob field must be plain bytes");
        size += sizeof(T);
    }
    void Chip(SoundChip_AY8910& chip) { size += chip.TTDStateSize(); }
};

struct Saver
{
    uint8_t* at;
    template <typename T>
    void operator()(T& field)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a blob field must be plain bytes");
        std::memcpy(at, &field, sizeof(T));
        at += sizeof(T);
    }
    void Chip(SoundChip_AY8910& chip)
    {
        chip.TTDSaveState(at);
        at += chip.TTDStateSize();
    }
};

struct Loader
{
    const uint8_t* at;
    template <typename T>
    void operator()(T& field)
    {
        static_assert(std::is_trivially_copyable_v<T>, "a blob field must be plain bytes");
        std::memcpy(&field, at, sizeof(T));
        at += sizeof(T);
    }
    void Chip(SoundChip_AY8910& chip)
    {
        chip.TTDLoadState(at);
        at += chip.TTDStateSize();
    }
};
}  // namespace

/// Friend of the board's classes: the one field walk per blob
struct P2666Codec
{
    template <typename V>
    static void Board(V& v, PortDecoderP2666& d)
    {
        uint8_t version = kVersion;
        v(version);
        EmulatorState& s = *d._state;
        P2666State& p = s.p2666;
        v(p.pxx77);
        v(p.cpm);
        v(p.pen2);
        v(p.bf0);
        v(p.dos1Off);
        v(p.border3);
        v(p.covox);
        v(p.powered);
        v(p.window);
        v(p.palette);
        v(p.intFlagClearFrame);
        v(p.intFlagClearEdge);
        v(p.intFrame);
        v(p.intFall);
        v(p.intAnchor);
        v(p.intRise);
        v(p.intRisesLeft);
        // The ATM text renderer's view of the last #xx77 address
        auto aFF77 = static_cast<uint32_t>(s.atm.aFF77);
        v(aFF77);
        s.atm.aFF77 = static_cast<decltype(s.atm.aFF77)>(aFF77);
        v(d._cmos);
        v(d._cmosAddress);
        v(d._mcuCmos);
    }

    template <typename V>
    static void Sound(V& v, PortDecoderP2666& d)
    {
        uint8_t version = kVersion;
        v(version);
        P2666Audio& a = *d._audio;
        for (auto& chip : a._chips)
            v.Chip(*chip);
        v(a._regs);
        v(a._addr);
        v(a._oda);
        v(a._selected);
        v(a._covox);
        v(a._fe);
        v(a._dc);
        v(a._tickT);
        v(a._frameStartT);
        v(a._timeKnown);
        v(a._lastRight);
        v(a._lastLeft);
    }

    template <typename V>
    static void BetaDisk(V& v, PortDecoderP2666& d)
    {
        uint8_t version = kVersion;
        v(version);
        P2666BetaDisk& b = d._beta;
        v(b._system);
        v(b._command);
        v(b._track);
        v(b._sector);
        v(b._data);
        v(b._position);
        v(b._stepIn);
        v(b._intrq);
        v(b._nrd);
        v(b._drq);
        v(b._multi);
        v(b._restore);
        v(b._seek);
        v(b._step);
        v(b._readAddr);
        v(b._read);
        v(b._write);
        v(b._sectorCmd);
        v(b._trackCmd);
        v(b._delayEnd);
        v(b._byteEdge);
        v(b._waitCpu);
        v(b._byte);
        v(b._ioSector);
        v(b._radrState);
        v(b._radrData);
        v(b._ramData);
        v(b._indexEpoch);
    }

    template <typename V>
    static void Uart(V& v, PortDecoderP2666& d)
    {
        uint8_t version = kVersion;
        v(version);
        P2666Uart& u = d._uart;
        v(u._dll);
        v(u._dlm);
        v(u._ier);
        v(u._fcr);
        v(u._lcr);
        v(u._mcr);
        v(u._spr);
        v(u._out);
        v(u._tx);
        v(u._rx);
        v(u._txHead);
        v(u._txFilled);
        v(u._rxHead);
        v(u._rxFilled);
        v(u._txEmptyInt);
        v(u._transmitterEmpty);
        v(u._rxAvailable);
        v(u._overrun);
        v(u._msInt);
        v(u._ctsOld);
        v(u._rtsFifo);
        v(u._intState);
        v(u._txBusy);
        v(u._txByte);
        v(u._txEnd);
        v(u._rxBusy);
        v(u._rxEnd);
        v(u._rxLineFree);
        v(u._rxSince);
        v(u._last);
    }

    template <typename V>
    static void Walk(V& v, PortDecoderP2666& d, ttd::TTDP2666Device::Part part)
    {
        switch (part)
        {
            case ttd::TTDP2666Device::Part::Board:
                Board(v, d);
                break;
            case ttd::TTDP2666Device::Part::Sound:
                Sound(v, d);
                break;
            case ttd::TTDP2666Device::Part::BetaDisk:
                BetaDisk(v, d);
                break;
            case ttd::TTDP2666Device::Part::Uart:
                Uart(v, d);
                break;
        }
    }

    /// What the board derives from the loaded values
    static void AfterLoad(PortDecoderP2666& d, ttd::TTDP2666Device::Part part)
    {
        if (part == ttd::TTDP2666Device::Part::Board)
        {
            EmulatorState& s = *d._state;
            for (unsigned i = 0; i < 16; i++)
            {
                s.atm.palette[i] = PortDecoderP2666::PaletteWordToAbgr(s.p2666.palette[i]);
                s.atm.paletteRegs[i] = static_cast<uint8_t>(s.p2666.palette[i]);
            }
            s.atm.borderBright = s.p2666.border3;
            if (d._screen)
                d._screen->RefreshProgrammablePalette();
        }
        else if (part == ttd::TTDP2666Device::Part::Uart)
        {
            // The INT line the restored UART holds
            if (d._uart._onInt)
                d._uart._onInt(d._uart._intState);
        }
    }
};

namespace ttd {

TTDP2666Device::TTDP2666Device(PortDecoderP2666& decoder, Part part) : _decoder(decoder), _part(part)
{
    Sizer sizer;
    P2666Codec::Walk(sizer, _decoder, _part);
    _size = sizer.size;
}

void TTDP2666Device::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    Saver saver{dst};
    P2666Codec::Walk(saver, _decoder, _part);
}

void TTDP2666Device::TTDLoadState(const uint8_t* src)
{
    if (!src)
        return;
    Loader loader{src};
    P2666Codec::Walk(loader, _decoder, _part);
    P2666Codec::AfterLoad(_decoder, _part);
}

std::string TTDP2666Device::TTDDeviceName() const
{
    switch (_part)
    {
        case Part::Board:
            return "P2666Board";
        case Part::Sound:
            return "P2666Sound";
        case Part::BetaDisk:
            return "P2666BetaDisk";
        case Part::Uart:
            return "P2666Uart";
    }
    return "P2666";
}

TTDDeviceType TTDP2666Device::TTDType() const
{
    switch (_part)
    {
        case Part::Board:
            return TTDDeviceType::P2666Board;
        case Part::Sound:
            return TTDDeviceType::P2666Sound;
        case Part::BetaDisk:
            return TTDDeviceType::P2666BetaDisk;
        case Part::Uart:
            return TTDDeviceType::P2666Uart;
    }
    return TTDDeviceType::P2666Board;
}

uint64_t TTDP2666Device::TTDHashState() const
{
    std::vector<uint8_t> blob(_size);
    TTDSaveState(blob.data());
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (uint8_t byte : blob)
    {
        h ^= byte;
        h *= 0x100000001b3ULL;  // FNV-1a prime
    }
    return h;
}

}  // namespace ttd
