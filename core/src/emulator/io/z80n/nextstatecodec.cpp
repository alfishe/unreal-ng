#include "stdafx.h"

#include "nextstatecodec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <type_traits>

#include "emulator/io/z80n/nextboard.h"
#include "emulator/io/z80n/nextctc.h"
#include "emulator/io/z80n/nextcopper.h"
#include "emulator/io/z80n/nextdivmmc.h"
#include "emulator/io/z80n/nextdma.h"
#include "emulator/io/z80n/nexti2c.h"
#include "emulator/io/z80n/nextinterrupts.h"
#include "emulator/io/z80n/nextmultiface.h"
#include "emulator/io/z80n/nextsprites.h"
#include "emulator/io/z80n/nextuart.h"
#include "emulator/io/z80n/nextvideoregs.h"
#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/sound/nextaudio.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
constexpr uint8_t kImageVersion = 12;  // 12: + the membrane's extended keys; 11: + the key joystick map and its address; 10: + the AY address and hold flag; 9: the CTC's run start and pending pulses, the UARTs' break state; 7: ULA+ select; 6: the UARTs

/// Plain values and arrays, in declaration order, little-endian as the host stores them (an image is read by the build that wrote it)
class Writer
{
public:
    template <typename T>
    void Put(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "plain values only");
        const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
        _out.insert(_out.end(), bytes, bytes + sizeof(T));
    }
    template <typename T, size_t N>
    void PutArray(const T (&array)[N])
    {
        static_assert(std::is_trivially_copyable_v<T>, "plain values only");
        const auto* bytes = reinterpret_cast<const uint8_t*>(array);
        _out.insert(_out.end(), bytes, bytes + sizeof(array));
    }
    template <typename T, size_t N>
    void PutArray(const std::array<T, N>& array)
    {
        static_assert(std::is_trivially_copyable_v<T>, "plain values only");
        const auto* bytes = reinterpret_cast<const uint8_t*>(array.data());
        _out.insert(_out.end(), bytes, bytes + sizeof(T) * N);
    }
    void PutBytes(const uint8_t* bytes, size_t n) { _out.insert(_out.end(), bytes, bytes + n); }
    std::vector<uint8_t> Take() { return std::move(_out); }

private:
    std::vector<uint8_t> _out;
};

class Reader
{
public:
    Reader(const uint8_t* data, size_t size) : _data(data), _size(size) {}
    template <typename T>
    void Get(T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "plain values only");
        Take(reinterpret_cast<uint8_t*>(&value), sizeof(T));
    }
    template <typename T, size_t N>
    void GetArray(T (&array)[N])
    {
        static_assert(std::is_trivially_copyable_v<T>, "plain values only");
        Take(reinterpret_cast<uint8_t*>(array), sizeof(array));
    }
    template <typename T, size_t N>
    void GetArray(std::array<T, N>& array)
    {
        static_assert(std::is_trivially_copyable_v<T>, "plain values only");
        Take(reinterpret_cast<uint8_t*>(array.data()), sizeof(T) * N);
    }
    void GetBytes(uint8_t* dst, size_t n) { Take(dst, n); }
    bool Done() const { return _pos == _size && !_overrun; }

private:
    void Take(uint8_t* dst, size_t n)
    {
        if (_pos + n > _size)
        {
            _overrun = true;
            std::memset(dst, 0, n);
            return;
        }
        std::memcpy(dst, _data + _pos, n);
        _pos += n;
    }
    const uint8_t* _data;
    size_t _size;
    size_t _pos = 0;
    bool _overrun = false;
};
}  // namespace

struct NextStateCodec::Impl
{
// ---- Save ----
static void SaveVideo(Writer& w, const NextVideoRegs& v)
{
    w.PutArray(v._clip);
    w.PutArray(v._clipIndex);
    w.PutArray(v._palette);
    w.Put(v._index);
    w.Put(v._control);
    w.Put(v._ulapMode);
    w.Put(v._ulapIndex);
    w.Put(v._ulaNext);
    w.Put(v._portFf);
    w.Put(v._port123b);
    w.Put(v._l2Offset);
    w.Put(v._shadowAlias);
    w.Put(v._first);
    w.Put(v._haveFirst);
}
static void LoadVideo(Reader& r, NextVideoRegs& v)
{
    r.GetArray(v._clip);
    r.GetArray(v._clipIndex);
    r.GetArray(v._palette);
    r.Get(v._index);
    r.Get(v._control);
    r.Get(v._ulapMode);
    r.Get(v._ulapIndex);
    r.Get(v._ulaNext);
    r.Get(v._portFf);
    r.Get(v._port123b);
    r.Get(v._l2Offset);
    r.Get(v._shadowAlias);
    r.Get(v._first);
    r.Get(v._haveFirst);
}

static void SaveSprites(Writer& w, const NextSprites& s)
{
    w.PutArray(s._attr);
    w.PutArray(s._pattern);
    w.Put(s._slot);
    w.Put(s._byte);
    w.Put(s._patternOffset);
    w.Put(s._patternHalf);
    w.Put(s._mirror);
    w.Put(s._collision);
    w.Put(s._tooMany);
}
static void LoadSprites(Reader& r, NextSprites& s)
{
    s.Touch();
    r.GetArray(s._attr);
    r.GetArray(s._pattern);
    r.Get(s._slot);
    r.Get(s._byte);
    r.Get(s._patternOffset);
    r.Get(s._patternHalf);
    r.Get(s._mirror);
    r.Get(s._collision);
    r.Get(s._tooMany);
}

static void SaveCopper(Writer& w, const NextCopper& c)
{
    w.PutArray(c._code);
    w.Put(c._pc);
    w.Put(c._address);
    w.Put(c._stored);
    w.Put(c._mode);
    w.Put(c._lastMode);
    w.Put(c._offset);
    w.Put(c._pending);
    w.Put(c._running);
    w.Put(c._t);
    w.Put(c._hcPerLine);
    w.Put(c._lines);
    w.Put(c._frameClocks);
    w.Put(c._lastMoveAt);
    w.Put(c._observed);
}
static void LoadCopper(Reader& r, NextCopper& c)
{
    r.GetArray(c._code);
    r.Get(c._pc);
    r.Get(c._address);
    r.Get(c._stored);
    r.Get(c._mode);
    r.Get(c._lastMode);
    r.Get(c._offset);
    r.Get(c._pending);
    r.Get(c._running);
    r.Get(c._t);
    r.Get(c._hcPerLine);
    r.Get(c._lines);
    r.Get(c._frameClocks);
    r.Get(c._lastMoveAt);
    r.Get(c._observed);
    c._idleUntil = 0;  // derived: the next run works it out again
}

static void SaveBoard(Writer& w, const NextBoard& b)
{
    w.Put(b._selected);
    w.PutArray(b._regs);
    w.Put(b._timing);
    w.Put(b._resetPending);
    w.Put(b._resetType);
    w.Put(b._resetHard);
    for (unsigned i = 0; i < NextKeyJoystick::kEntries; i++)
        w.Put(b._keyJoystick.Entry(i));
    w.Put(b._keyJoystick.Address());
    w.Put(b._keyJoystick.Selected());
}
static void LoadBoard(Reader& r, NextBoard& b)
{
    r.Get(b._selected);
    r.GetArray(b._regs);
    r.Get(b._timing);
    r.Get(b._resetPending);
    r.Get(b._resetType);
    b.LatchVideoFrameSync();  // the effective video bits are not in the image: they follow the registers
    r.Get(b._resetHard);
    for (unsigned i = 0; i < NextKeyJoystick::kEntries; i++)
    {
        uint8_t entry = 0;
        r.Get(entry);
        b._keyJoystick.SetEntry(i, entry);
    }
    uint16_t address = 0;
    bool selected = false;
    r.Get(address);
    r.Get(selected);
    b._keyJoystick.Restore(address, selected);
}

static void SaveMemory(Writer& w, const NextMemory& m)
{
    w.PutArray(m._mmu);
    w.Put(m._allRam);
    w.Put(m._rom);
    w.Put(m._machineType);
    w.Put(m._alt);
    w.Put(m._dffd);
    w.Put(m._divView.mapped);
    w.Put(m._divView.mapram);
    w.Put(m._divView.bank);
    w.Put(m._mfActive);
    w.Put(m._l2View.read);
    w.Put(m._l2View.write);
    w.Put(m._l2View.segment);
    w.Put(m._l2View.bank);
    w.Put(m._l2View.offset);
    w.Put(m._configMode);
    w.Put(m._bootRom);
    w.Put(m._bootRomLoaded);
    w.Put(m._cfgBank);
}
static void LoadMemory(Reader& r, NextMemory& m)
{
    r.GetArray(m._mmu);
    r.Get(m._allRam);
    r.Get(m._rom);
    r.Get(m._machineType);
    r.Get(m._alt);
    r.Get(m._dffd);
    r.Get(m._divView.mapped);
    r.Get(m._divView.mapram);
    r.Get(m._divView.bank);
    r.Get(m._mfActive);
    r.Get(m._l2View.read);
    r.Get(m._l2View.write);
    r.Get(m._l2View.segment);
    r.Get(m._l2View.bank);
    r.Get(m._l2View.offset);
    r.Get(m._configMode);
    r.Get(m._bootRom);
    r.Get(m._bootRomLoaded);
    r.Get(m._cfgBank);
}

static void SaveDma(Writer& w, const NextDma& d)
{
    w.Put(d._aToB);
    w.Put(d._portAAddress);
    w.Put(d._portBAddress);
    w.Put(d._blockLength);
    w.Put(d._portAIsIo);
    w.Put(d._portBIsIo);
    w.Put(d._portAMode);
    w.Put(d._portBMode);
    w.Put(d._portATiming);
    w.Put(d._portBTiming);
    w.Put(d._prescaler);
    w.Put(d._mode);
    w.Put(d._autoRestart);
    w.Put(d._ceWait);
    w.Put(d._readMask);
    w.Put(d._transferring);
    w.Put(d._waiting);
    w.Put(d._z80Compat);
    w.Put(d._finishPending);
    w.Put(d._atLeastOne);
    w.Put(d._endOfBlock);
    w.Put(d._src);
    w.Put(d._dst);
    w.Put(d._counter);
    w.Put(d._nextByteAt);
    w.Put(static_cast<uint8_t>(d._seq));
    w.Put(d._readSeq);
    w.Put(d._regTemp);
}
static void LoadDma(Reader& r, NextDma& d)
{
    r.Get(d._aToB);
    r.Get(d._portAAddress);
    r.Get(d._portBAddress);
    r.Get(d._blockLength);
    r.Get(d._portAIsIo);
    r.Get(d._portBIsIo);
    r.Get(d._portAMode);
    r.Get(d._portBMode);
    r.Get(d._portATiming);
    r.Get(d._portBTiming);
    r.Get(d._prescaler);
    r.Get(d._mode);
    r.Get(d._autoRestart);
    r.Get(d._ceWait);
    r.Get(d._readMask);
    r.Get(d._transferring);
    r.Get(d._waiting);
    r.Get(d._z80Compat);
    r.Get(d._finishPending);
    r.Get(d._atLeastOne);
    r.Get(d._endOfBlock);
    r.Get(d._src);
    r.Get(d._dst);
    r.Get(d._counter);
    r.Get(d._nextByteAt);
    uint8_t seq = 0;
    r.Get(seq);
    d._seq = static_cast<NextDma::Seq>(seq);
    r.Get(d._readSeq);
    r.Get(d._regTemp);
}

static void SaveCtc(Writer& w, const NextCtc& c)
{
    for (const NextCtc::Channel& ch : c._ch)
    {
        w.Put(ch.control);
        w.Put(ch.tc);
        w.Put(ch.counter);
        w.Put(ch.waitTc);
        w.Put(ch.running);
        w.Put(ch.waitTrigger);
        w.Put(ch.runStart);
        w.Put(ch.phase);
        w.Put(ch.zeros);
        for (uint64_t at : ch.pending)
            w.Put(at);
        w.Put(ch.pendingCount);
    }
    w.Put(c._vector);
    w.Put(c._now);
}
static void LoadCtc(Reader& r, NextCtc& c)
{
    for (NextCtc::Channel& ch : c._ch)
    {
        r.Get(ch.control);
        r.Get(ch.tc);
        r.Get(ch.counter);
        r.Get(ch.waitTc);
        r.Get(ch.running);
        r.Get(ch.waitTrigger);
        r.Get(ch.runStart);
        r.Get(ch.phase);
        r.Get(ch.zeros);
        for (uint64_t& at : ch.pending)
            r.Get(at);
        r.Get(ch.pendingCount);
    }
    r.Get(c._vector);
    r.Get(c._now);
    c._quietUntil = 0;  // derived from the channels just loaded
}

/// The UARTs: registers, FIFOs, shifters, the line timing and the break state; not the peers (an ESP module's own state is the machine
/// serial peer's blob, the Pi's the second one's, a joystick connector device's the joystick peer's); the connector's route is NR #0B
static void SaveUart(Writer& w, const NextUart& u)
{
    for (const NextUart::Channel& c : u._ch)
    {
        w.Put(c.prescalerMsb);
        w.Put(c.prescalerLsb);
        w.Put(c.frame);
        w.PutArray(c.rx);
        w.Put(c.rxHead);
        w.Put(c.rxCount);
        w.PutArray(c.tx);
        w.Put(c.txHead);
        w.Put(c.txCount);
        w.Put(c.txBusy);
        w.Put(c.txByte);
        w.Put(c.txDone);
        w.Put(c.rxBusy);
        w.Put(c.rxByte);
        w.Put(c.rxDone);
        w.Put(c.rxFreeAt);
        w.Put(c.txFreeAt);
        w.Put(c.txReadyAt);
        w.Put(c.lastLook);
        w.Put(c.errOverflow);
        w.Put(c.errFraming);
        w.Put(c.rtsOut);
        w.Put(c.rxLevel);
        w.Put(c.txLevel);
        w.Put(c.sent);
        w.Put(c.received);
        w.Put(c.lost);
        w.Put(c.rxLow);
        w.Put(c.lowSince);
        w.Put(c.lowFramed);
        w.Put(c.lowPaused);
        w.Put(c.breakIn);
    }
    w.Put(u._selected);
    w.Put(u._now);
}
static void LoadUart(Reader& r, NextUart& u)
{
    for (NextUart::Channel& c : u._ch)
    {
        r.Get(c.prescalerMsb);
        r.Get(c.prescalerLsb);
        r.Get(c.frame);
        r.GetArray(c.rx);
        r.Get(c.rxHead);
        r.Get(c.rxCount);
        r.GetArray(c.tx);
        r.Get(c.txHead);
        r.Get(c.txCount);
        r.Get(c.txBusy);
        r.Get(c.txByte);
        r.Get(c.txDone);
        r.Get(c.rxBusy);
        r.Get(c.rxByte);
        r.Get(c.rxDone);
        r.Get(c.rxFreeAt);
        r.Get(c.txFreeAt);
        r.Get(c.txReadyAt);
        r.Get(c.lastLook);
        r.Get(c.errOverflow);
        r.Get(c.errFraming);
        r.Get(c.rtsOut);
        r.Get(c.rxLevel);
        r.Get(c.txLevel);
        r.Get(c.sent);
        r.Get(c.received);
        r.Get(c.lost);
        r.Get(c.rxLow);
        r.Get(c.lowSince);
        r.Get(c.lowFramed);
        r.Get(c.lowPaused);
        r.Get(c.breakIn);
        c.toldValid = false;  // the peer is told the line again at the next change
        c.breakTold = (c.frame & NextUart::kFrameBreak) != 0;  // the peer's own state (its blob) says what it was told
        // a restored ring that is out of range would index outside the arrays
        c.rxHead %= NextUart::kRxFifo;
        c.rxCount = std::min<uint16_t>(c.rxCount, NextUart::kRxFifo);
        c.txHead %= NextUart::kTxFifo;
        c.txCount = std::min<uint8_t>(c.txCount, NextUart::kTxFifo);
    }
    r.Get(u._selected);
    u._selected &= 1;
    r.Get(u._now);
    u._pollClean = false;  // the levels and the counts were replaced
}

static void SaveInterrupts(Writer& w, const NextInterruptSource& i)
{
    w.Put(i._timing);
    w.Put(i._control);
    w.PutArray(i._nmiReturn);
    w.Put(i._enable0);
    w.Put(i._enableCtc);
    w.Put(i._enableUart);
    w.PutArray(i._dma);
    w.Put(i._lineControl);
    w.Put(i._lineValue);
    w.Put(i._counterOffset);
    w.Put(i._pending);
    w.Put(i._inService);
    w.Put(i._lastT);
    w.Put(i._pulseOn);
    w.Put(i._pulseFrame);
    w.Put(i._pulseStart);
}
static void LoadInterrupts(Reader& r, NextInterruptSource& i)
{
    r.Get(i._timing);
    r.Get(i._control);
    r.GetArray(i._nmiReturn);
    r.Get(i._enable0);
    r.Get(i._enableCtc);
    r.Get(i._enableUart);
    r.GetArray(i._dma);
    r.Get(i._lineControl);
    r.Get(i._lineValue);
    r.Get(i._counterOffset);
    r.Get(i._pending);
    r.Get(i._inService);
    r.Get(i._lastT);
    r.Get(i._pulseOn);
    r.Get(i._pulseFrame);
    r.Get(i._pulseStart);
}

static void SaveI2c(Writer& w, const NextI2c& i)
{
    w.Put(i._scl);
    w.Put(i._sda);
    w.Put(i._slaveSda);
    const Ds1307& d = i._rtc;
    w.PutArray(d._reg);
    w.Put(static_cast<uint8_t>(d._state));
    w.Put(d._lastScl);
    w.Put(d._lastSda);
    w.Put(d._shift);
    w.Put(d._bits);
    w.Put(d._pointer);
    w.Put(d._sdaOut);
    w.Put(d._readMode);
    w.Put(d._pointerSet);
}
static void LoadI2c(Reader& r, NextI2c& i)
{
    r.Get(i._scl);
    r.Get(i._sda);
    r.Get(i._slaveSda);
    Ds1307& d = i._rtc;
    r.GetArray(d._reg);
    uint8_t state = 0;
    r.Get(state);
    d._state = static_cast<Ds1307::State>(state);
    r.Get(d._lastScl);
    r.Get(d._lastSda);
    r.Get(d._shift);
    r.Get(d._bits);
    r.Get(d._pointer);
    r.Get(d._sdaOut);
    r.Get(d._readMode);
    r.Get(d._pointerSet);
}

static void SaveSmall(Writer& w, const NextMultiface& m, const NextDivMmc& d)
{
    w.Put(m._nmiActive);
    w.Put(m._invisible);
    w.Put(m._mfEnable);
    w.Put(d._control);
    w.Put(d._automap);
    w.Put(d._buttonNmi);
}
static void LoadSmall(Reader& r, NextMultiface& m, NextDivMmc& d)
{
    r.Get(m._nmiActive);
    r.Get(m._invisible);
    r.Get(m._mfEnable);
    r.Get(d._control);
    r.Get(d._automap);
    r.Get(d._buttonNmi);
}

static void SaveAudio(Writer& w, const NextAudio* a)
{
    // The three chips through their own TTD serializer (registers, generators, envelope, noise), then what the Next adds: pan, the selected
    // chip, the mode bits and the four DACs. The render timing (tick clock, DC filters) is not state: it resyncs after a load
    for (unsigned i = 0; i < NextAudio::kChips; i++)
    {
        std::vector<uint8_t> chip(a->_chips[i]->TTDStateSize());
        a->_chips[i]->TTDSaveState(chip.data());
        w.PutBytes(chip.data(), chip.size());
    }
    w.PutArray(a->_pan);
    w.Put(a->_selected);
    w.Put(a->_turboSound);
    w.Put(a->_dacEnabled);
    w.Put(a->_ym);
    w.Put(a->_acb);
    w.Put(a->_speakerExcluded);
    w.Put(a->_monoMask);
    w.PutArray(a->_dac);
    w.PutArray(a->_addr);
    w.Put(a->_held);
}
static void LoadAudio(Reader& r, NextAudio* a)
{
    for (unsigned i = 0; i < NextAudio::kChips; i++)
    {
        std::vector<uint8_t> chip(a->_chips[i]->TTDStateSize());
        r.GetBytes(chip.data(), chip.size());
        a->_chips[i]->TTDLoadState(chip.data());
    }
    r.GetArray(a->_pan);
    r.Get(a->_selected);
    r.Get(a->_turboSound);
    r.Get(a->_dacEnabled);
    r.Get(a->_ym);
    r.Get(a->_acb);
    r.Get(a->_speakerExcluded);
    r.Get(a->_monoMask);
    r.GetArray(a->_dac);
    r.GetArray(a->_addr);
    r.Get(a->_held);
    a->_timeKnown = false;  // the render clock follows the machine again from the next frame
}

static void SaveSd(Writer& w, const SdCardSpi& card)
{
    std::vector<uint8_t> state(SdCardSpi::STATE_SIZE);
    card.saveState(state.data());
    w.PutBytes(state.data(), state.size());
}
static void LoadSd(Reader& r, SdCardSpi& card)
{
    std::vector<uint8_t> state(SdCardSpi::STATE_SIZE);
    r.GetBytes(state.data(), state.size());
    card.loadState(state.data());
}

static void SaveExtendedInput(Writer& w, const NextExtendedInput& e)
{
    w.Put(e.PcKeys());
    w.Put(e.Pad(true));
    w.Put(e.Right());
    w.Put(e.MembraneKeys());
}
static void LoadExtendedInput(Reader& r, NextExtendedInput& e)
{
    uint16_t keys = 0, right = 0, membrane = 0;
    uint8_t left = 0;
    r.Get(keys);
    r.Get(left);
    r.Get(right);
    r.Get(membrane);
    e.SetPcKeys(keys);
    e.SetPad(true, left);
    e.SetRight(right);
    e.SetMembraneKeys(membrane);
}

static void SaveDecoder(Writer& w, const PortDecoder_Next& p)
{
    w.Put(p._nr08NoContention);
    w.Put(p._ratio);
    w.Put(p._pendingTiming);
    w.Put(p._spiSelected);
    w.Put(p._spiRx);
    w.Put(p._spiPending);
    w.Put(p._spiPendingValid);
    w.Put(p._spiBusyUntil);
    w.Put(p._spiBusyRatio);
    w.Put(p._spiTooFast);
}
static void LoadDecoder(Reader& r, PortDecoder_Next& p)
{
    r.Get(p._nr08NoContention);
    r.Get(p._ratio);
    r.Get(p._pendingTiming);
    r.Get(p._spiSelected);
    r.Get(p._spiRx);
    r.Get(p._spiPending);
    r.Get(p._spiPendingValid);
    r.Get(p._spiBusyUntil);
    r.Get(p._spiBusyRatio);
    r.Get(p._spiTooFast);
}
};

std::vector<uint8_t> NextStateCodec::Save(const PortDecoder_Next& p)
{
    Writer w;
    w.Put(kImageVersion);
    Impl::SaveBoard(w, *p._board);
    Impl::SaveMemory(w, *static_cast<const NextMemory*>(p._context->pMemory));
    Impl::SaveVideo(w, p._board->_video);
    Impl::SaveSprites(w, p._board->_sprites);
    Impl::SaveCopper(w, p._board->_copper);
    Impl::SaveDma(w, p._dma);
    Impl::SaveCtc(w, p._ctc);
    Impl::SaveInterrupts(w, *p._interrupts);
    Impl::SaveI2c(w, p._i2c);
    Impl::SaveSmall(w, *p._multiface, *p._divMmc);
    Impl::SaveDecoder(w, p);
    Impl::SaveExtendedInput(w, p._extendedInput);
    if (p._audio)
        Impl::SaveAudio(w, p._audio.get());
    Impl::SaveSd(w, p._sd[0]);
    Impl::SaveSd(w, p._sd[1]);
    Impl::SaveUart(w, p._uart);
    return w.Take();
}

bool NextStateCodec::Load(PortDecoder_Next& p, const uint8_t* data, size_t size)
{
    // The length is fixed for a machine: an image of another length is another machine's
    if (!data || size != Save(p).size() || data[0] != kImageVersion)
        return false;
    Reader r(data, size);
    uint8_t version = 0;
    r.Get(version);
    Impl::LoadBoard(r, *p._board);
    NextMemory& memory = *static_cast<NextMemory*>(p._context->pMemory);
    Impl::LoadMemory(r, memory);
    Impl::LoadVideo(r, p._board->_video);
    Impl::LoadSprites(r, p._board->_sprites);
    Impl::LoadCopper(r, p._board->_copper);
    Impl::LoadDma(r, p._dma);
    Impl::LoadCtc(r, p._ctc);
    Impl::LoadInterrupts(r, *p._interrupts);
    Impl::LoadI2c(r, p._i2c);
    Impl::LoadSmall(r, *p._multiface, *p._divMmc);
    Impl::LoadDecoder(r, p);
    Impl::LoadExtendedInput(r, p._extendedInput);
    if (p._audio)
        Impl::LoadAudio(r, p._audio.get());
    Impl::LoadSd(r, p._sd[0]);
    Impl::LoadSd(r, p._sd[1]);
    Impl::LoadUart(r, p._uart);
    p.UpdateUartRoute(false);  // the joystick connector's UART follows NR #0B, which the image holds
    p.SyncPiLine();            // and UART 1's pins NR #A0
    memory.Remap();  // the slot table, the windows and the contention flags follow the image
    return r.Done();
}
