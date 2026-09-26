#include "soundchip_gslw.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "3rdparty/blip_buf/blip_buf.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/core.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace
{
void lwTtdWrite16(uint8_t* dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

void lwTtdWrite32(uint8_t* dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

void lwTtdWrite64(uint8_t* dst, uint64_t value)
{
    for (int i = 0; i < 8; i++)
        dst[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint16_t lwTtdRead16(const uint8_t* src)
{
    return static_cast<uint16_t>(src[0] | (static_cast<uint16_t>(src[1]) << 8));
}

uint32_t lwTtdRead32(const uint8_t* src)
{
    return static_cast<uint32_t>(src[0]) | (static_cast<uint32_t>(src[1]) << 8)
        | (static_cast<uint32_t>(src[2]) << 16) | (static_cast<uint32_t>(src[3]) << 24);
}

uint64_t lwTtdRead64(const uint8_t* src)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++)
        value |= static_cast<uint64_t>(src[i]) << (8 * i);
    return value;
}
} // namespace

/// region <Constructors / destructors>

SoundChip_GSLightweight::SoundChip_GSLightweight(EmulatorContext* context, size_t ramKB, size_t sampleRate)
    : _context(context)
    , _ramKB(std::clamp<size_t>(ramKB, 128, 512)) // original GS card range
    , _sampleRate(sampleRate)
{
    _logger = _context ? _context->pModuleLogger : nullptr;

    // Mailbox overflow drops land in this card's activity counters
    _mb.counters = &_activityCounters;

    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    blip_set_rates(_blipL, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));

    reset();
}

SoundChip_GSLightweight::~SoundChip_GSLightweight()
{
    blip_delete(_blipL);
    blip_delete(_blipR);
    _blipL = nullptr;
    _blipR = nullptr;
}

void SoundChip_GSLightweight::setSampleRate(size_t sampleRate)
{
    _sampleRate = sampleRate;
    blip_set_rates(_blipL, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
    // Player position lives in the row/tick/quantum domain - nothing to rescale
}

void SoundChip_GSLightweight::setSynthesisSuppressed(bool suppressed)
{
    if (_synthesisSuppressed == suppressed)
        return;
    _synthesisSuppressed = suppressed;
    if (!suppressed)
    {
        if (_blipL) blip_clear(_blipL);
        if (_blipR) blip_clear(_blipR);
    }
}

/// endregion </Constructors / destructors>

/// region <Reset / ROM>

void SoundChip_GSLightweight::reset()
{
    resetCard();

    // Host mailbox and the external DAC latches flip back to their power-on
    // state (resetCard - the #33 pulse - leaves the mailbox alone: it is
    // flip-flops outside the card logic, same contract as the LLE)
    _mb.resetAll();
    for (int i = 0; i < 4; i++)
    {
        _channelData[i] = 0x80; // Midpoint = silence
        _channelVol[i] = 0;
    }
    makeVolumeTable();

    _lastL = 0;
    _lastR = 0;
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
    _frameHadActivity = false;
    _wasActive = false;
}

void SoundChip_GSLightweight::resetCard()
{
    // Firmware reboot (POST + INITVAR) leaves exactly this state; the host
    // mailbox survives (external flip-flops)
    softReset(false);
    _errCode = 0;

    // Timing restarts from zero but keeps the ZX frame base: the following
    // flush re-executes the elapsed frame time from the reset state, exactly
    // like the LLE's #33 handler
    _gsCyclesAbs = 0;
    _intQuantum = 0;
    _frameStartGsCycles = 0;
    _nmiPending = false;
}

void SoundChip_GSLightweight::hostReset()
{
    // GSReset=1 couples the card to the ZX reset line (same rule as the LLE:
    // Unreal z80.cpp "if (conf.sound.gsreset) reset_gs()"); GSReset=0 leaves
    // the card running across a ZX reset
    if (_context && _context->config.sound.gsreset != 0)
        reset();
}

void SoundChip_GSLightweight::loadROM(const std::string& /*romPath*/)
{
    // API symmetry only: there is no coprocessor to run firmware on
    MLOGWARNING("GS: lightweight personality ignores firmware images (no coprocessor)");
}

/// endregion </Reset / ROM>

/// region <Volume curve>

void SoundChip_GSLightweight::makeVolumeTable()
{
    // Same curve as the LLE card: _vfx[i] == gs_vol * i * 63 / 1024, landing
    // the channel level directly in the int16 blip domain (Unreal
    // make_gs_volume, gsz80.cpp level math)
    int gsVol = _context ? _context->config.sound.gs_vol : 8000;
    gsVol = std::clamp(gsVol, 0, 8192);

    for (int i = 0; i <= 0x40; i++)
        _vfx[i] = static_cast<uint32_t>(gsVol) * static_cast<uint32_t>(i) * 63u / 1024u;
}

/// endregion </Volume curve>

/// region <Command interpreter>

void SoundChip_GSLightweight::softReset(bool post)
{
    // INITVAR (COMF3 target, also reached by COM30-with-CNTMOD!=0): the
    // firmware clears its RAM (module system, fades, selectors), restores
    // the volume defaults, sets MTSTAT=%11000011 and VOL0-3 latches to 0x3F,
    // preserves ERRCODE/NUMPG and consumes one trailing DATRG byte.
    // COMF4 (post=true) re-POSTs on top - same visible state without the
    // 0.3-1.1 s POST window, so the BUG-6 idle signature appears promptly
    (void)post;

    _player.reset();
    _store.clear();
    _replyCount = 0;
    _replyHead = 0;
    _paramCount = 0;
    _paramHead = 0;
    _modVol = 0x40;
    _fxVol = 0x40;
    _fxMvol = 0x40;
    _mtVol = 0; // INITVAR default is 0; COM31 raises it to 0x40 (TDD 5.4)
    _modFade = 0;
    _fxFade = 0;
    _curMod = 0;
    _cntMod = 0;
    _module = 0;
    _mtStat = 0xC3;
    _curSmp = 0;
    _cntSmp = 0;
    _curFx = 0;
    _cntFx = 0;
    _curChannel = 0;
    _loading = false;
    _discardLoad = false;
    _covox = false;
    _subCommandExpected = false;
    _subCommand80 = false;
    _subCommandA0 = false;
    _subCommandFlags = 0;
    for (int i = 0; i < 4; i++)
        _channelVol[i] = 0x3F;

    consumeParam(); // INITVAR tail: IN A,(DATRG)
}

void SoundChip_GSLightweight::postReply(uint8_t value)
{
    // OUTRG + HSEND: the firmware posts one byte and waits for the host to
    // read it. The queue preserves the order; the next byte becomes visible
    // the moment the host consumes the current one (pumpReply on #B3 read)
    if (_replyCount < REPLY_QUEUE_CAPACITY)
    {
        _replyQueue[static_cast<uint8_t>((_replyHead + _replyCount) & (REPLY_QUEUE_CAPACITY - 1))] = value;
        _replyCount++;
    }
    pumpReply();
}

void SoundChip_GSLightweight::pumpReply()
{
    if (!(_mb.status & 0x80) && _replyCount != 0)
    {
        _mb.dataToHost = _replyQueue[_replyHead];
        _replyHead = static_cast<uint8_t>((_replyHead + 1) & (REPLY_QUEUE_CAPACITY - 1));
        _replyCount--;
        _mb.status |= 0x80;
    }
}

void SoundChip_GSLightweight::consumeParam()
{
    // IN A,(DATRG): pop one buffered param byte into the shadow latch -
    // callers read _mb.dataFromHost (the firmware's A register analog).
    // An empty buffer keeps the latch: stale DATRG readback, same as the
    // hardware latch and the firmware's no-poll drain paths.
    // Exact DATRG semantics: the read clears the shared bit7 - a reply the
    // handler posted BEFORE its param read loses its flag (the value stays
    // in the #B3 latch), byte-for-byte what the LLE firmware produces on
    // the get-then-set commands
    if (_paramCount != 0)
    {
        _mb.dataFromHost = _paramQueue[_paramHead];
        _paramHead = static_cast<uint8_t>((_paramHead + 1) & (PARAM_QUEUE_CAPACITY - 1));
        _paramCount--;
    }
    _mb.status &= 0x7F;
}

void SoundChip_GSLightweight::acceptHostData(uint8_t value)
{
    // Instant consumption (original gshle.cpp model): the host never sees
    // bit7 held by its own write - the interpreter takes the byte in the
    // same call, so status bit7 stays a pure reply indicator on this
    // personality
    _activityCounters.hostDataWritten++;
    _mb.dataFromHost = value;

    if (_covox)
    {
        // COM0E stream: every byte latches into DAC0+DAC2 on arrival
        _channelData[0] = value;
        _channelData[2] = value;
        return;
    }

    if (_loading)
    {
        // Cap at the card's configured RAM size: real hardware has nowhere
        // else to put the bytes either, but there _store is a window into a
        // fixed RAM array (COM30 overwrites in place, wrapping); here it's a
        // growing std::vector, so with no cap a runaway host loop (or a COM30
        // stream that never sends the terminating D2) would grow it without
        // bound. Dropping past the cap isn't a byte-exact wrap, but it is
        // memory-safe, and a load that overflows the card's own RAM was never
        // going to parse as a valid module anyway.
        if (!_discardLoad && _store.size() < _ramKB * 1024)
            _store.push_back(value);
        return;
    }

    // Param byte ahead of its command: buffered in write order for the
    // dispatch-time consumeParam calls (real firmware paces one latch via
    // the bit7 handshake; the instant model needs the small buffer to keep
    // multi-param sequences ordered)
    if (_paramCount < PARAM_QUEUE_CAPACITY)
    {
        _paramQueue[static_cast<uint8_t>((_paramHead + _paramCount) & (PARAM_QUEUE_CAPACITY - 1))] = value;
        _paramCount++;
    }
    else
    {
        _activityCounters.hostDataDropped++;
    }
}

void SoundChip_GSLightweight::acceptHostCommand(uint8_t value)
{
    _activityCounters.hostCommandsReceived++;
    _mb.commandFromHost = value;
    _mb.status |= 0x01; // command flip-flop; the dispatch acks (RSCOM analog)

    // Any command closes the covox stream (gshle: load_stream = 0)
    if (_covox)
        _covox = false;

    if (_loading)
    {
        // LOADCM: F3/F4 execute (their own reset+ack), D2 finishes the
        // stream, anything else is acked and discarded - the load continues
        if (value == 0xF3 || value == 0xF4)
        {
            dispatchCommand(value);
            return;
        }
        _mb.status &= 0xFE;
        if (value == 0xD2)
            finishLoad();
        return;
    }

    // A new command discards any unread reply (original gshle.cpp resets
    // resptr/resmode and gsstat on every command write) - replies never go
    // stale across commands, matching the LLE's single overwritten latch
    _replyCount = 0;
    _replyHead = 0;
    _mb.status &= 0x7F;

    dispatchCommand(value);
}

void SoundChip_GSLightweight::enterLoad(bool discard)
{
    _loading = true;
    _discardLoad = discard;
}

void SoundChip_GSLightweight::finishLoad()
{
    // LOAD3: the stream ends, the module occupies its store. The firmware
    // never validates tracker data (it just plays it); the LW parser refuses
    // a malformed upload and raises ERRCODE 0x10 instead
    _loading = false;
    _discardLoad = false;

    if (_discardLoad || _store.empty())
        return;

    const char* reason = nullptr;
    if (!_player.parse(_store.data(), _store.size(), &reason))
    {
        _errCode = 0x10; // firmware ERR10 class: upload rejected
        MLOGWARNING("GS: lightweight card rejected module upload (%s)", reason ? reason : "unknown");
    }
}

void SoundChip_GSLightweight::dispatchCommand(uint8_t command)
{
    // COM50/58/80/A0 continuation: this byte is the selector, not a command
    if (_subCommandExpected)
    {
        _subCommandExpected = false;
        _subCommandFlags = static_cast<uint8_t>(command & 0x1F);
        dispatchSubCommand(command);
        return;
    }

    switch (command)
    {
        // region <Low commands: direct DAC/volume control (COM_L.a80)>
        case 0x00: // reset flags: consume the dummy parameter
            consumeParam();
            break;
        case 0x01: // initialize DACs to midpoint
            for (int i = 0; i < 4; i++)
                _channelData[i] = 0x80;
            break;
        case 0x02: // VOL0-3 to 0x3F
            for (int i = 0; i < 4; i++)
                _channelVol[i] = 0x3F;
            break;
        case 0x03: // VOL0-3 to 0
            for (int i = 0; i < 4; i++)
                _channelVol[i] = 0;
            break;
        case 0x04: // set current channel (0-7)
            consumeParam();
            _curChannel = static_cast<uint8_t>(_mb.dataFromHost & 0x07);
            break;
        case 0x05: // volume of the current channel
            consumeParam();
            if (_curChannel < 4)
                _channelVol[_curChannel] = static_cast<uint8_t>(_mb.dataFromHost & 0x3F);
            break;
        case 0x06: // data byte of the current channel
            consumeParam();
            if (_curChannel < 4)
                _channelData[_curChannel] = _mb.dataFromHost;
            break;
        case 0x07: // data byte + volume of the current channel
        {
            consumeParam();
            uint8_t byte = _mb.dataFromHost;
            consumeParam();
            if (_curChannel < 4)
            {
                _channelData[_curChannel] = byte;
                _channelVol[_curChannel] = static_cast<uint8_t>(_mb.dataFromHost & 0x3F);
            }
            break;
        }
        case 0x09: // physical volume: channel in bits 3-2, level in 0-5
            consumeParam();
            _channelVol[(_mb.dataFromHost >> 2) & 0x03] = static_cast<uint8_t>(_mb.dataFromHost & 0x3F);
            break;
        case 0x0A: // data byte, then channel (WTDTL tail)
        {
            consumeParam();
            uint8_t byte = _mb.dataFromHost;
            consumeParam();
            _channelData[_mb.dataFromHost & 0x03] = byte;
            break;
        }
        case 0x0B: // data byte, then channel+volume
        {
            consumeParam();
            uint8_t byte = _mb.dataFromHost;
            consumeParam();
            uint8_t selector = _mb.dataFromHost;
            _channelData[(selector >> 2) & 0x03] = byte;
            _channelVol[(selector >> 2) & 0x03] = static_cast<uint8_t>(selector & 0x3F);
            break;
        }
        case 0x0C: // data byte to all channels
            consumeParam();
            for (int i = 0; i < 4; i++)
                _channelData[i] = _mb.dataFromHost;
            break;
        case 0x0D: // masked byte set: nibble mask, one byte per channel
        {
            consumeParam();
            uint8_t mask = static_cast<uint8_t>(_mb.dataFromHost & 0x0F);
            for (int i = 0; i < 4; i++)
            {
                if (mask & (1 << i))
                {
                    consumeParam();
                    _channelData[i] = _mb.dataFromHost;
                }
            }
            break;
        }
        case 0x0E: // covox stream: data latches DAC0+DAC2, exit on command
            _covox = true;
            break;
        case 0x0F: // stereo covox ('Y' param): v1 documented no-op
            consumeParam();
            break;
        case 0x13: // jump to address: no code to run
            consumeParam();
            consumeParam();
            break;
        case 0x16: // put byte to address: no RAM window
            consumeParam();
            consumeParam();
            consumeParam();
            break;
        case 0x17: // get byte from address: no RAM window, reads as zero
            consumeParam();
            consumeParam();
            postReply(0x00);
            break;
        case 0x18: // set memory pointer
            consumeParam();
            consumeParam();
            break;
        // endregion</Low commands>

        // region <Memory geometry (virtual, no RAM window)>
        case 0x20: // total memory: 24-bit byte count, L then H then C
        {
            const uint32_t total = static_cast<uint32_t>(_ramKB) * 1024u;
            postReply(static_cast<uint8_t>(total));
            postReply(static_cast<uint8_t>(total >> 8));
            postReply(static_cast<uint8_t>(total >> 16));
            break;
        }
        case 0x21: // free memory: total minus the upload store
        {
            const uint32_t total = static_cast<uint32_t>(_ramKB) * 1024u;
            const uint32_t used = static_cast<uint32_t>(_store.size());
            const uint32_t freeBytes = used < total ? total - used : 0;
            _errCode = 0; // COM21 clears the error latch
            postReply(static_cast<uint8_t>(freeBytes));
            postReply(static_cast<uint8_t>(freeBytes >> 8));
            postReply(static_cast<uint8_t>(freeBytes >> 16));
            break;
        }
        case 0x22: // peek page byte: no RAM window, reads as zero
            consumeParam();
            postReply(0x00);
            break;
        case 0x23: // NUMPG: 32 KB pages minus the system page
        {
            const uint8_t pages = static_cast<uint8_t>(_ramKB / 32);
            postReply(static_cast<uint8_t>(pages > 0 ? pages - 1 : 0));
            break;
        }
        // endregion</Memory geometry>

        // region <Module family (COM_H.a80)>
        case 0x2A: // MODVOL get-then-set, clamp 0x40
            postReply(_modVol);
            consumeParam();
            _modVol = std::min<uint8_t>(_mb.dataFromHost, 0x40);
            break;
        case 0x2B: // FXVOL get-then-set, clamp 0x40
            postReply(_fxVol);
            consumeParam();
            _fxVol = std::min<uint8_t>(_mb.dataFromHost, 0x40);
            break;
        case 0x2C: // CURMOD selector: 0=CNTMOD, <=CNTMOD ok, else 0
            postReply(_curMod);
            consumeParam();
            if (_mb.dataFromHost == 0)
                _curMod = _cntMod;
            else if (_mb.dataFromHost <= _cntMod)
                _curMod = _mb.dataFromHost;
            else
                _curMod = 0;
            break;
        case 0x2D: // CURSMP selector (same shape)
            postReply(_curSmp);
            consumeParam();
            if (_mb.dataFromHost == 0)
                _curSmp = _cntSmp;
            else if (_mb.dataFromHost <= _cntSmp)
                _curSmp = _mb.dataFromHost;
            else
                _curSmp = 0;
            break;
        case 0x2E: // CURFX selector (same shape)
            postReply(_curFx);
            consumeParam();
            if (_mb.dataFromHost == 0)
                _curFx = _cntFx;
            else if (_mb.dataFromHost <= _cntFx)
                _curFx = _mb.dataFromHost;
            else
                _curFx = 0;
            break;
        case 0x2F: // track selector: 2 params, no reply
            consumeParam();
            consumeParam();
            break;
        case 0x30: // module upload: reset-and-retry path or fresh slot 1
            if (_cntMod != 0)
            {
                softReset(false); // INITVAR: clears CNTMOD, consumes 1 byte
                break;
            }
            _cntMod = 1;
            _curMod = 1;
            postReply(1); // the assigned module number
            consumeParam(); // the trailing dummy byte of the COM30 trio
            enterLoad(false);
            break;
        case 0x31: // start playback
        {
            consumeParam();
            uint8_t selector = _mb.dataFromHost;
            if (selector == 0)
                selector = _curMod;
            if (selector == 0 || selector > _cntMod || !_player.isParsed())
            {
                // Error path: CURMOD zeroes, reply 0 (COM31_1/COM31_2)
                _curMod = 0;
                postReply(0x00);
                break;
            }
            postReply(selector);
            _module = selector;
            _curMod = selector;
            _mtStat = 0x03;             // play + active, stop cleared
            _mtVol = 0x40;              // COM31 resets MTVOL/MTROWS/tempo
            _player.setBpm(125);
            _player.start(0);
            break;
        }
        case 0x32: // stop: reply old MODULE, freeze position
            postReply(_module);
            consumeParam();
            _mtStat |= 0x80;
            _player.stop();
            break;
        case 0x33: // continue: reply old MODULE, resume
            postReply(_module);
            consumeParam();
            if (_module != 0 && (_mtStat & 0x40) == 0)
            {
                _mtStat &= static_cast<uint8_t>(~0x80);
                _player.continuePlay();
            }
            break;
        case 0x34: // MODFADE get-then-set, no clamp
            postReply(_modFade);
            consumeParam();
            _modFade = _mb.dataFromHost;
            break;
        case 0x35: // MTVOL get-then-set, clamp 0x40
            postReply(_mtVol);
            consumeParam();
            _mtVol = std::min<uint8_t>(_mb.dataFromHost, 0x40);
            break;
        case 0x36: // query: always 0xFF
            postReply(0xFF);
            break;
        case 0x37: // full module-system reset without INITVAR
            _mtStat |= 0x80;
            _curMod = 0;
            _cntMod = 0;
            _module = 0;
            _store.clear();
            _player.reset();
            break;
        // endregion</Module family>

        // region <SFX family: protocol-faithful consumption, no playback (v1)>
        case 0x38: // FX upload: new slot or table-full error
            if (_cntFx >= 60)
            {
                postReply(0x00);
                _curFx = 0;
                consumeParam();
                break;
            }
            _cntFx++;
            postReply(_cntFx); // the assigned FX number
            consumeParam();
            _curFx = _cntFx;
            enterLoad(true); // bytes consumed, nothing stored (no SFX in v1)
            break;
        case 0x39: // FX select + play: reply 0 ok / 0xFF bad selector
            consumeParam();
            if (_mb.dataFromHost != 0)
                _curFx = _mb.dataFromHost;
            if (_curFx == 0 || _curFx > _cntFx)
                postReply(0xFF);
            else
                postReply(0x00);
            break;
        case 0x3A: // FX channel-off mask
            consumeParam();
            break;
        case 0x3B:
        case 0x3C: // FXFADE get-then-set
            postReply(_fxFade);
            consumeParam();
            _fxFade = _mb.dataFromHost;
            break;
        case 0x3D: // FXMVOL get-then-set, clamp 0x40
            postReply(_fxMvol);
            consumeParam();
            _fxMvol = std::min<uint8_t>(_mb.dataFromHost, 0x40);
            break;
        case 0x3E: // FX upload variant selector
            consumeParam();
            if (_mb.dataFromHost == 0x01)
            {
                dispatchCommand(0x38); // same protocol as a plain upload
                break;
            }
            if (_mb.dataFromHost == 0x00)
            {
                // LX=#80 variant: identical wire behavior for a no-op store
                dispatchCommand(0x38);
                break;
            }
            postReply(0x00);
            consumeParam();
            break;
        case 0x40: // FX note (1 param)
        case 0x41: // FX volume (1 param)
            consumeParam();
            break;
        case 0x42: // FX field get-then-set: fresh-RAM semantics reply 0
        case 0x45:
        case 0x46:
        case 0x47:
            postReply(0x00);
            consumeParam();
            break;
        case 0x48: // FX fields (3 params)
        case 0x49:
            consumeParam();
            consumeParam();
            consumeParam();
            break;
        // endregion</SFX family>

        // region <Sub-command protocols>
        case 0x50: // FX channel control: param, then selector + 1-3 params
            consumeParam();
            _subCommand80 = false;
            _subCommandA0 = false;
            _subCommandExpected = true;
            break;
        case 0x58: // same protocol, replies its own command byte first
            postReply(0x58);
            _subCommand80 = false;
            _subCommandA0 = false;
            _subCommandExpected = true;
            break;
        case 0x80: // FX channel setup: param, then selector + 0-2 params
            consumeParam();
            if (_mb.dataFromHost != 0)
                _curFx = _mb.dataFromHost;
            if (_curFx == 0 || _curFx > _cntFx)
                postReply(0xFF); // COM39_9 error shape
            _subCommand80 = true;
            _subCommandA0 = false;
            _subCommandExpected = true;
            break;
        case 0xA0: // FX channel period/volume: param + selector, no params
            consumeParam();
            _subCommand80 = false;
            _subCommandA0 = true;
            _subCommandExpected = true;
            break;
        // endregion</Sub-command protocols>

        // region <Position/tempo queries (player-driven)>
        case 0x60: // MTSNGPS: song position
            postReply(_player.songPosition());
            break;
        case 0x61: // MTPATPS: row in pattern
            postReply(_player.patternPosition());
            break;
        case 0x62: // combined: song bits 7-6, row bits 5-0
            postReply(static_cast<uint8_t>(((_player.songPosition() << 6) & 0xC0)
                | (_player.patternPosition() & 0x3F)));
            break;
        case 0x63: // 4x CHREAL: current sample per channel (0x7F = none)
            for (int i = 0; i < 4; i++)
            {
                const uint8_t sample = _player.channelSample(i);
                postReply(static_cast<uint8_t>(sample == 0 ? 0x7F : sample));
            }
            break;
        case 0x64: // 4x CHMVOL: current row volume per channel
            for (int i = 0; i < 4; i++)
                postReply(_player.channelVolume(i));
            break;
        case 0x66: // FXF: external tempo change
            consumeParam();
            _player.setBpm(_mb.dataFromHost);
            break;
        case 0x67: // MTSPEED: ticks per row
            postReply(_player.speed());
            break;
        case 0x68: // MTBPM
            postReply(_player.bpm());
            break;
        case 0x69: // single engine step: no-op on this card
            break;
        // endregion</Position/tempo queries>

        // region <Reset/status family>
        case 0xF0: // ERRCODE query
            postReply(_errCode);
            break;
        case 0xF3: // INITVAR
            softReset(false);
            break;
        case 0xF4: // POST reboot (prompt idle signature - no delay)
            softReset(true);
            break;
        // endregion</Reset/status family>

        default: // COMZ: ack, no effect
            break;
    }

    // Every firmware handler ends with OUT (RSCOM),A: clear the command
    // flip-flop (status bit0)
    _mb.status &= 0xFE;
}

void SoundChip_GSLightweight::dispatchSubCommand(uint8_t /*sub*/)
{
    // The selector byte was captured in _subCommandFlags by dispatchCommand;
    // the family flags were latched at the 50/58/80/A0 entry. Nothing plays
    // (SFX v1 no-op); only the parameter consumption must match the firmware
    // so subsequent traffic stays in sync:
    // - COM50/58 (COM50_): always 1 param, +1 for selector >= 4, +1 for 7
    // - COM80: selector bit3 -> 1 param, bit4 -> 1 param
    // - COMA0: none
    _mb.status &= 0xFE; // RSCOM analog

    if (_subCommandA0)
        return;

    if (!_subCommand80)
    {
        const uint8_t selector = static_cast<uint8_t>(_subCommandFlags & 0x07);
        consumeParam();
        if (selector >= 4)
            consumeParam();
        if (selector == 7)
            consumeParam();
    }
    else
    {
        if (_subCommandFlags & 0x08)
            consumeParam();
        if (_subCommandFlags & 0x10)
            consumeParam();
    }
}

/// endregion </Command interpreter>

/// region <Host port interface (ZX side)>

uint8_t SoundChip_GSLightweight::portDeviceInMethod(uint16_t port)
{
    switch (port & 0x00FF)
    {
        case 0xB3: // GSDAT: bit7 clears, the GS->ZX byte returns; the next
            //       queued reply byte (if any) becomes visible immediately -
            //       the HSEND wait collapses to zero on this personality
        {
            flush();
            const uint8_t value = _mb.dataToHost;
            _mb.status &= 0x7F;
            _activityCounters.hostDataRead++;
            traceEvent(GSTraceSide::Host, PORT_DATA, value, false);
            pumpReply();
            return value;
        }
        case 0xBB: // GSCOM read: status, bits 1-6 read as 1 (pull-ups)
            flush();
            traceEvent(GSTraceSide::Host, PORT_COMMAND, static_cast<uint8_t>(_mb.status | 0x7E), false);
            return _mb.status | 0x7E;
        default:
            return 0xFF;
    }
}

void SoundChip_GSLightweight::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (port & 0x00FF)
    {
        case 0x33: // GSCTR: bit7 = reset, bit6 = NMI (applied before any
            //       flush, same order as the LLE's out_gs handler)
            traceEvent(GSTraceSide::Host, PORT_CONTROL, value, true);
            if (value & 0x80)
            {
                resetCard();
                flush(); // re-execute the elapsed frame time from reset state
                return;
            }
            if (value & 0x40)
            {
                _nmiPending = true;
                flush();
                return;
            }
            return;
        case 0xB3: // GSDAT write: the interpreter takes the byte in-call
            flush();
            traceEvent(GSTraceSide::Host, PORT_DATA, value, true);
            acceptHostData(value);
            return;
        case 0xBB: // GSCOM write: the interpreter reacts at once (no poll delay)
            flush();
            traceEvent(GSTraceSide::Host, PORT_COMMAND, value, true);
            acceptHostCommand(value);
            return;
        default:
            return;
    }
}

// Automation actions - same side effects as the ZX-side hardware ports

uint8_t SoundChip_GSLightweight::readStatus()
{
    flush();
    return _mb.status | 0x7E;
}

uint8_t SoundChip_GSLightweight::readData()
{
    flush();
    const uint8_t value = _mb.dataToHost;
    _mb.status &= 0x7F;
    pumpReply();
    return value;
}

void SoundChip_GSLightweight::sendCommand(uint8_t command)
{
    flush();
    acceptHostCommand(command);
}

void SoundChip_GSLightweight::sendData(uint8_t data)
{
    flush();
    acceptHostData(data);
}

void SoundChip_GSLightweight::triggerNMI()
{
    // Counted like the LLE, but there is no coprocessor to deliver it to
    _nmiPending = true;
    flush();
}

/// endregion </Host port interface>

/// region <Runtime personality switch (SoundManager::switchGeneralSoundCard)>

GSForwardMailbox SoundChip_GSLightweight::snapshotMailbox() const
{
    GSForwardMailbox snapshot = _mb;
    snapshot.counters = nullptr; // owner back-pointer, not protocol state
    return snapshot;
}

void SoundChip_GSLightweight::restoreMailbox(const GSForwardMailbox& snapshot)
{
    _mb = snapshot;
    _mb.counters = &_activityCounters; // rebind drop accounting to this card
}

void SoundChip_GSLightweight::accumulateActivityCounters(const GSActivityCounters& other)
{
    accumulateGSActivityCounters(_activityCounters, other);
    // This personality has no coprocessor: cpuSteps is defined as always 0,
    // even when the counters arrive from an outgoing LLE card
    _activityCounters.cpuSteps = 0;
}

bool SoundChip_GSLightweight::captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const
{
    // Only a completed load is replayable through the LLE firmware (the
    // store survives finishLoad's parse). A switch mid-upload keeps the
    // mailbox but loses the partial stream - documented v1 limit. Pending
    // reply-queue bytes beyond the visible dataToHost latch are likewise
    // not carried across (the LLE keeps its replies inside firmware RAM).
    if (_loading || _cntMod == 0 || _store.empty())
    {
        bytes.clear();
        playing = false;
        return false;
    }

    bytes = _store;
    playing = _player.isPlaying();
    return true;
}

void SoundChip_GSLightweight::replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback)
{
    if (bytes.empty())
        return;

    // SoundManager hands this to a freshly constructed card (step 3 of the
    // switch, before restoreMailbox) - _cntMod is still 0, so COM30
    // dispatches straight into the load, no F3 reset-and-retry lead-in
    // needed. The instant-dispatch model takes every byte in the same
    // call, so this is a plain host-port replay with no frame-stepping.
    sendData(0x01);
    sendCommand(0x30);
    for (uint8_t b : bytes)
        sendData(b);
    sendCommand(0xD2);

    if (startPlayback)
    {
        sendData(0x00);
        sendCommand(0x31);
    }
}

/// endregion </Runtime personality switch>

/// region <Lazy sync core>

double SoundChip_GSLightweight::gsCyclesPerZxTact() const
{
    if (!_context)
        return static_cast<double>(GS_CLOCK_HZ) / static_cast<double>(CPU_CLOCK_RATE);

    const CONFIG& config = _context->config;
    if (config.frame == 0 || config.frame_duration_us == 0)
        return static_cast<double>(GS_CLOCK_HZ) / static_cast<double>(CPU_CLOCK_RATE);

    // Same math as the LLE: only the HOST speed multiplier stretches the ZX
    // tact domain, hardware turbo is already descaled by AudioTstate
    double zxBaseHz = static_cast<double>(config.frame) / (static_cast<double>(config.frame_duration_us) * 1e-6);
    double hostMultiplier = static_cast<double>(_context->emulatorState.HostSpeedMultiplier());

    return static_cast<double>(GS_CLOCK_HZ) / (zxBaseHz * hostMultiplier);
}

uint64_t SoundChip_GSLightweight::currentZxTacts() const
{
    if (_context && _context->pCore && _context->pCore->GetZ80())
        return static_cast<uint64_t>(_context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t));

    return _frameStartZxTacts;
}

int64_t SoundChip_GSLightweight::frameGsLength() const
{
    if (!_context)
        return 0;

    double tacts = static_cast<double>(_context->config.frame) * static_cast<double>(_context->emulatorState.HostSpeedMultiplier());
    return static_cast<int64_t>(std::llround(tacts * gsCyclesPerZxTact()));
}

void SoundChip_GSLightweight::flush()
{
    uint64_t zxTacts = currentZxTacts();
    if (zxTacts < _frameStartZxTacts)
        return; // ZX reset rewound the clock; wait for the next frame base

    uint64_t relative = zxTacts - _frameStartZxTacts;
    int64_t target = _frameStartGsCycles + static_cast<int64_t>(std::llround(static_cast<double>(relative) * gsCyclesPerZxTact()));
    runTo(target);
}

void SoundChip_GSLightweight::runTo(int64_t target)
{
    // Quantum-granular advance: at each 320-cycle boundary the periodic
    // interrupt fires and one player/DAC tick runs (the firmware ISR's one
    // LD A,(DE) fetch per acceptance). The interpreter has no instruction
    // timing of its own, so the sub-quantum remainder carries like the LLE's
    while (totalGsCycles() < target)
    {
        // NMI from #33 bit6: latched, consumed at the next boundary - there
        // is no coprocessor, so the acceptance is the whole visible effect
        if (_nmiPending)
        {
            _nmiPending = false;
            _activityCounters.nmisAccepted++;
            traceEvent(GSTraceSide::Interrupt, 0, 0, false, 0, GSTraceFlags::kNmi);
        }

        if (_intQuantum >= GS_CYCLES_PER_INT)
        {
            _intQuantum = static_cast<int16_t>(_intQuantum - GS_CYCLES_PER_INT);
            _gsCyclesAbs += GS_CYCLES_PER_INT;
            serviceQuantum();
            continue;
        }

        const int64_t left = target - totalGsCycles();
        const int64_t toBoundary = GS_CYCLES_PER_INT - _intQuantum;
        _intQuantum = static_cast<int16_t>(_intQuantum + static_cast<int16_t>(std::min<int64_t>(left, toBoundary)));
    }
}

void SoundChip_GSLightweight::serviceQuantum()
{
    _activityCounters.interruptPeriods++;
    _activityCounters.interruptsAccepted++; // instant handler: never coalesced
    traceEvent(GSTraceSide::Interrupt, 0, 0, false);

    if (!_player.isPlaying())
        return; // idle card: latches hold, no sample fetches

    uint8_t out[GSModPlayer::kChannels];
    uint8_t vols[GSModPlayer::kChannels];
    _player.advanceQuantum(out, vols);
    for (int i = 0; i < GSModPlayer::kChannels; i++)
    {
        _channelData[i] = out[i];
        // Channel latch = row volume scaled by MODVOL*MTVOL (the firmware's
        // VOL_H math before it writes the 6-bit latch)
        const uint32_t scaled = (static_cast<uint32_t>(vols[i]) * _modVol * _mtVol) >> 12;
        _channelVol[i] = static_cast<uint8_t>(std::min<uint32_t>(scaled, 0x3F));
    }

    _activityCounters.dacFetches++;
    _activityCounters.lastDacFetchGsCycle = totalGsCycles();
    _activityCounters.lastDacFetchFrame = currentFrameNumber();
    traceEvent(GSTraceSide::DacFetch, 0x6000, _channelData[0], false, 0);
    emitSample();
}

/// endregion </Lazy sync core>

/// region <Diagnostics>

uint32_t SoundChip_GSLightweight::currentFrameNumber() const
{
    return _context ? static_cast<uint32_t>(_context->emulatorState.frame_counter) : 0;
}

void SoundChip_GSLightweight::traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel, uint8_t extraFlags)
{
    if (!_portTrace.isCapturing())
        return;

    GSTraceEvent event;
    event.timestamp = totalGsCycles();
    event.frameNumber = currentFrameNumber();
    event.port = port;
    event.pc = 0; // no coprocessor on this personality
    event.value = value;
    event.channel = channel;
    event.side = side;
    event.flags = static_cast<uint8_t>((isOut ? GSTraceFlags::kDirectionOut : 0) | extraFlags);
    _portTrace.record(event);
}

/// endregion </Diagnostics>

/// region <Frame lifecycle>

void SoundChip_GSLightweight::handleFrameStart()
{
    _frameHadActivity = false;

    _frameStartZxTacts = currentZxTacts();
    _frameStartGsCycles = totalGsCycles();
    _frameGsCycles = frameGsLength();
}

void SoundChip_GSLightweight::handleFrameEnd(size_t expectedSamples)
{
    if (_frameGsCycles <= 0)
        return;

    runTo(_frameStartGsCycles + _frameGsCycles);

    blip_end_frame(_blipL, static_cast<unsigned>(_frameGsCycles));
    blip_end_frame(_blipR, static_cast<unsigned>(_frameGsCycles));

    int samplesThisFrame;
    if (expectedSamples > 0)
    {
        samplesThisFrame = static_cast<int>(expectedSamples);
    }
    else
    {
        samplesThisFrame = static_cast<int>(std::llround(
            static_cast<double>(_frameGsCycles) * static_cast<double>(_sampleRate) / static_cast<double>(GS_CLOCK_HZ)));
    }
    samplesThisFrame = std::clamp(samplesThisFrame, 0, static_cast<int>(MAX_SAMPLES_PER_FRAME));

    int samplesL = blip_read_samples(_blipL, &_buffer[0], samplesThisFrame, 1 /* stereo stride */);
    int samplesR = blip_read_samples(_blipR, &_buffer[1], samplesThisFrame, 1 /* stereo stride */);

    for (int i = samplesL; i < samplesThisFrame; i++)
        _buffer[i * 2] = 0;
    for (int i = samplesR; i < samplesThisFrame; i++)
        _buffer[i * 2 + 1] = 0;

    // Activity of this frame: the audio-settings LED and, held for a second,
    // the HUD nudge (SoundManager / AudioActivityIndicators)
    _wasActive = _frameHadActivity;
}

void SoundChip_GSLightweight::onEmulatorPaused()
{
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);

    _wasActive = false;
}

/// endregion </Frame lifecycle>

/// region <Audio pipeline>

void SoundChip_GSLightweight::computeStereo(int32_t& outL, int32_t& outR) const
{
    // LLE shape: centered sample scaled by the volume curve, channels 1,2
    // left / 3,4 right with 50% cross-feed (Unreal gsz80.cpp:148-154,251)
    int32_t v[4];
    for (int ch = 0; ch < 4; ch++)
    {
        int32_t centered = static_cast<int8_t>(static_cast<uint8_t>(_channelData[ch] - 0x80));
        v[ch] = centered * static_cast<int32_t>(_vfx[_channelVol[ch]]) / 256;
    }

    int32_t l = v[0] + v[1];
    int32_t r = v[2] + v[3];
    outL = (l + r / 2) / 2;
    outR = (r + l / 2) / 2;
}

void SoundChip_GSLightweight::emitSample()
{
    int32_t newL, newR;
    computeStereo(newL, newR);

    if (!_synthesisSuppressed && _frameGsCycles > 0)
    {
        int32_t deltaL = newL - _lastL;
        int32_t deltaR = newR - _lastR;

        if (deltaL != 0 || deltaR != 0)
        {
            int64_t position = totalGsCycles() - _frameStartGsCycles;
            int64_t clamped = std::clamp<int64_t>(position, 0, _frameGsCycles - 1);

            if (deltaL != 0)
                blip_add_delta(_blipL, static_cast<unsigned>(clamped), deltaL);
            if (deltaR != 0)
                blip_add_delta(_blipR, static_cast<unsigned>(clamped), deltaR);
            _frameHadActivity = true;
        }
    }

    _lastL = newL;
    _lastR = newR;
}

/// endregion </Audio pipeline>

/// region <TTDSerializable>

void SoundChip_GSLightweight::serializeFixedState(uint8_t* dst) const
{
    // [0..23]: LLE-compatible header (same fields, same offsets - bit1 of
    // [23] is the LLE intPending slot, always 0 here: this card never
    // defers a periodic request)
    dst[0] = _mb.status;
    dst[1] = _mb.dataFromHost;
    dst[2] = _mb.dataToHost;
    dst[3] = _mb.commandFromHost;
    dst[4] = 0; // LLE mpag slot: no banking on this card
    memcpy(&dst[5], _channelVol, 4);
    memcpy(&dst[9], _channelData, 4);
    lwTtdWrite64(&dst[13], static_cast<uint64_t>(_gsCyclesAbs));
    lwTtdWrite16(&dst[21], static_cast<uint16_t>(_intQuantum));
    // bits 2/3 were the queue-era pending flags; _mb.status (dst[0]) is
    // authoritative now, the slots stay reserved for layout compatibility
    dst[23] = static_cast<uint8_t>(_nmiPending ? 1 : 0);

    // [24..42]: interpreter variables (the LLE stores Z80 regs here)
    dst[24] = _modVol;
    dst[25] = _fxVol;
    dst[26] = _fxMvol;
    dst[27] = _mtVol;
    dst[28] = _modFade;
    dst[29] = _fxFade;
    dst[30] = _curMod;
    dst[31] = _cntMod;
    dst[32] = _module;
    dst[33] = _mtStat;
    dst[34] = _errCode;
    dst[35] = _curSmp;
    dst[36] = _cntSmp;
    dst[37] = _curFx;
    dst[38] = _cntFx;
    dst[39] = static_cast<uint8_t>((_loading ? 1 : 0) | (_discardLoad ? 2 : 0)
        | (_covox ? 4 : 0) | (_subCommandExpected ? 8 : 0)
        | (_subCommand80 ? 16 : 0) | (_subCommandA0 ? 32 : 0));
    dst[40] = static_cast<uint8_t>((_curChannel << 5) | (_subCommandFlags & 0x1F));

    // [41..58]: card->host reply queue
    dst[41] = _replyCount;
    dst[42] = _replyHead;
    memcpy(&dst[43], _replyQueue, REPLY_QUEUE_CAPACITY);

    // [59..76]: host->card param buffer (queue-era command-queue slots)
    dst[59] = _paramCount;
    dst[60] = _paramHead; // tail = (head + count) & mask
    memcpy(&dst[61], _paramQueue, PARAM_QUEUE_CAPACITY);

    // [77..94]: reserved (queue-era data-queue slots, zero)
    std::fill_n(&dst[77], 18, static_cast<uint8_t>(0));

    // [77..88]: the current frame's timeline, same slots as the LLE card
    // (SoundChip_GeneralSound::serializeFixedState)
    lwTtdWrite32(&dst[77], static_cast<uint32_t>(totalGsCycles() - _frameStartGsCycles));
    lwTtdWrite32(&dst[81], static_cast<uint32_t>(_frameStartZxTacts));
    lwTtdWrite32(&dst[85], static_cast<uint32_t>(_frameGsCycles));
}

size_t SoundChip_GSLightweight::TTDStateSize() const
{
    return TTD_FIXED_STATE_SIZE + 4 + _store.size() + 4 + _player.serializeStateSize();
}

void SoundChip_GSLightweight::TTDSaveState(uint8_t* dst) const
{
    serializeFixedState(dst);

    uint8_t* p = dst + TTD_FIXED_STATE_SIZE;
    lwTtdWrite32(p, static_cast<uint32_t>(_store.size()));
    p += 4;
    if (!_store.empty())
    {
        memcpy(p, _store.data(), _store.size());
        p += _store.size();
    }

    const size_t playerSize = _player.serializeStateSize();
    lwTtdWrite32(p, static_cast<uint32_t>(playerSize));
    p += 4;
    _player.serializeState(p);
}

void SoundChip_GSLightweight::TTDLoadState(const uint8_t* src)
{
    _mb.status = src[0];
    _mb.dataFromHost = src[1];
    _mb.dataToHost = src[2];
    _mb.commandFromHost = src[3];
    memcpy(_channelVol, &src[5], 4);
    memcpy(_channelData, &src[9], 4);
    _gsCyclesAbs = static_cast<int64_t>(lwTtdRead64(&src[13]));
    _intQuantum = static_cast<int16_t>(lwTtdRead16(&src[21]));
    _nmiPending = (src[23] & 1) != 0;
    // bits 2/3: queue-era pending flags, ignored - _mb.status is authoritative

    _modVol = src[24];
    _fxVol = src[25];
    _fxMvol = src[26];
    _mtVol = src[27];
    _modFade = src[28];
    _fxFade = src[29];
    _curMod = src[30];
    _cntMod = src[31];
    _module = src[32];
    _mtStat = src[33];
    _errCode = src[34];
    _curSmp = src[35];
    _cntSmp = src[36];
    _curFx = src[37];
    _cntFx = src[38];
    _loading = (src[39] & 1) != 0;
    _discardLoad = (src[39] & 2) != 0;
    _covox = (src[39] & 4) != 0;
    _subCommandExpected = (src[39] & 8) != 0;
    _subCommand80 = (src[39] & 16) != 0;
    _subCommandA0 = (src[39] & 32) != 0;
    _curChannel = static_cast<uint8_t>(src[40] >> 5);
    _subCommandFlags = static_cast<uint8_t>(src[40] & 0x1F);

    _replyCount = static_cast<uint8_t>(std::min<size_t>(src[41], REPLY_QUEUE_CAPACITY));
    _replyHead = static_cast<uint8_t>(src[42] & (REPLY_QUEUE_CAPACITY - 1));
    memcpy(_replyQueue, &src[43], REPLY_QUEUE_CAPACITY);

    _paramCount = static_cast<uint8_t>(std::min<size_t>(src[59], PARAM_QUEUE_CAPACITY));
    _paramHead = static_cast<uint8_t>(src[60] & (PARAM_QUEUE_CAPACITY - 1));
    memcpy(_paramQueue, &src[61], PARAM_QUEUE_CAPACITY);
    // src[77..94]: reserved (queue-era data-queue slots), ignored

    // Upload store + player runtime
    const uint8_t* p = src + TTD_FIXED_STATE_SIZE;
    const uint32_t storeSize = lwTtdRead32(p);
    p += 4;
    _store.assign(p, p + storeSize);
    p += storeSize;

    const uint32_t playerSize = lwTtdRead32(p);
    p += 4;
    _player.loadState(p, playerSize);

    // The parsed module image is rebuilt from the restored store (the player
    // never carries it in its own blob)
    if (!_store.empty())
        _player.parse(_store.data(), _store.size(), nullptr);

    // Host-side pipeline follows the restored levels without emitting a
    // step (the seek position already produced its own audio)
    computeStereo(_lastL, _lastR);
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
    _frameHadActivity = false;

    // The frame timeline the checkpoint was taken in (see serializeFixedState)
    _frameStartGsCycles = totalGsCycles() - static_cast<int64_t>(lwTtdRead32(&src[77]));
    _frameStartZxTacts = lwTtdRead32(&src[81]);
    _frameGsCycles = static_cast<int64_t>(lwTtdRead32(&src[85]));
}

uint64_t SoundChip_GSLightweight::TTDHashState() const
{
    // FNV-1a over the fixed header plus the player runtime (the upload store
    // is skipped like the LLE's RAM: it only changes during a load, and the
    // loading flag + store length in the header pin that trajectory)
    uint8_t blob[TTD_FIXED_STATE_SIZE];
    serializeFixedState(blob);

    uint64_t hash = 14695981039346656037ull; // FNV-1a offset basis
    for (size_t i = 0; i < TTD_FIXED_STATE_SIZE; i++)
    {
        hash ^= blob[i];
        hash *= 1099511628211ull; // FNV prime
    }

    const size_t playerSize = _player.serializeStateSize();
    std::vector<uint8_t> player(playerSize);
    _player.serializeState(player.data());
    for (size_t i = 0; i < playerSize; i++)
    {
        hash ^= player[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

/// endregion </TTDSerializable>
