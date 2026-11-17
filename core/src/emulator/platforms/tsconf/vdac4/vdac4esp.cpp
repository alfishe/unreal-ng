#include "stdafx.h"

#include "vdac4esp.h"

#include "vdac4espfirmware.h"
#include "vdac4espframes.h"
#include "vdac4esplink.h"
#include "vdac4espstate.h"
#include "vdac4mediarange.h"

Vdac4Esp::Vdac4Esp(TimeSource time, Config config)
    : _time(std::move(time)), _config(std::move(config)), _nonceState(_config.nonceSeed != 0 ? _config.nonceSeed : 1)
{
    _media = std::make_unique<Vdac4MediaRange>();
    _spi = std::make_unique<Vdac4EspSpi>(static_cast<Vdac4EspSpi::IHost*>(this));
    _link = std::make_unique<EspSpiLink>(*_spi, *_media);
    _transport = std::make_unique<EspFrameTransport>(*_link, *_media);
    _firmware = std::make_unique<Vdac4EspFirmware>(
        *_transport, *_link, *_media, _config.cacheDirectory,
        _config.cacheCapacity != 0 ? _config.cacheCapacity : EspModuleCache::kDefaultCapacity);
}

Vdac4Esp::~Vdac4Esp()
{
    _firmware->Shutdown();
}

SpiDevice& Vdac4Esp::Spi()
{
    return *_spi;
}

void Vdac4Esp::SetRuntime(IVdac4AppRuntime* runtime)
{
    _firmware->SetRuntime(runtime);
}

void Vdac4Esp::SetTang(IVdac4TangLink* tang)
{
    _firmware->SetTang(tang);
}

uint32_t Vdac4Esp::NowMs() const
{
    return static_cast<uint32_t>(_time ? _time() / 1000000u : 0);
}

uint8_t Vdac4Esp::NextNonce()
{
    // xorshift32; the nonce only has to differ from the previous boot's
    _nonceState ^= _nonceState << 13;
    _nonceState ^= _nonceState >> 17;
    _nonceState ^= _nonceState << 5;
    uint8_t nonce = static_cast<uint8_t>(_nonceState >> 24);
    if (_boots != 0 && nonce == _bootNonce)
        ++nonce;
    return nonce;
}

void Vdac4Esp::EnterPhase(Phase phase, uint32_t nowMs)
{
    _phase = phase;
    _phaseStartMs = nowMs;
}

void Vdac4Esp::PowerOn()
{
    PowerOff();
    EnterPhase(Phase::Booting, NowMs());
}

void Vdac4Esp::PowerOff()
{
    _firmware->Shutdown();
    _link->Stop();
    _transport->Reset();
    _spi->SetPowered(false);
    EnterPhase(Phase::Off, NowMs());
}

void Vdac4Esp::Boot(uint32_t nowMs)
{
    _bootNonce = NextNonce();
    ++_boots;
    _firmware->Boot(nowMs);
    _transport->Reset();
    _spi->SetPowered(true);
    _link->Begin(_bootNonce, nowMs);
    EnterPhase(Phase::Running, nowMs);
    _lastPassMs = nowMs;
    _firmware->Pass(nowMs);
}

void Vdac4Esp::UpdateTimeline(uint32_t nowMs)
{
    const uint32_t inPhase = nowMs - _phaseStartMs;
    switch (_phase)
    {
        case Phase::Off:
            return;
        case Phase::Booting:
            if (inPhase >= _config.bootMs)
                Boot(nowMs);
            return;
        case Phase::Running:
            if (nowMs != _lastPassMs)
            {
                _lastPassMs = nowMs;
                _firmware->Pass(nowMs);
            }
            if (_firmware->ResetRequested())
                EnterPhase(Phase::Flushing, nowMs);
            return;
        case Phase::Flushing:
            // transport.flush(): the ACK and what was before it reach the Z80, at most 1 s
            _transport->Drain(nowMs);
            if ((!_transport->Blocked() && _link->PendingToZ80() == 0) || inPhase >= kFlushTimeoutMs)
                EnterPhase(Phase::RestartWait, nowMs);
            return;
        case Phase::RestartWait:
            if (inPhase >= kResetDelayMs)
            {
                // ESP.restart(): the app and RAM are gone, the slave is not configured until the next boot
                _firmware->Shutdown();
                _link->Stop();
                _transport->Reset();
                _spi->SetPowered(false);
                EnterPhase(Phase::Booting, nowMs);
            }
            return;
    }
}

void Vdac4Esp::Advance()
{
    if (_inHost)
        return;
    _inHost = true;
    const uint32_t nowMs = NowMs();
    // A long gap can cross several phases (boot, then the first pass)
    for (int step = 0; step < 4; ++step)
    {
        const Phase before = _phase;
        UpdateTimeline(nowMs);
        if (_phase == before)
            break;
    }
    _inHost = false;
}

void Vdac4Esp::OnBusAccess()
{
    Advance();
}

void Vdac4Esp::OnBufferWritten()
{
    if (_inHost)
        return;
    _inHost = true;
    const uint32_t nowMs = NowMs();
    // The link task serves turns in every phase after boot (also while the main loop flushes for SYS_RESET)
    _link->OnRequestWritten(nowMs);
    if (_phase == Phase::Running)
    {
        _lastPassMs = nowMs;
        _firmware->Pass(nowMs);
        if (_firmware->ResetRequested())
            EnterPhase(Phase::Flushing, nowMs);
    }
    _inHost = false;
}

void Vdac4Esp::Save(std::vector<uint8_t>& out) const
{
    out.clear();
    Vdac4EspStateWriter writer(out);
    writer.U32(kStateMagic);
    writer.U16(kStateVersion);
    writer.U8(static_cast<uint8_t>(_phase));
    writer.U32(_phaseStartMs);
    writer.U32(_lastPassMs);
    writer.U32(_nonceState);
    writer.U8(_bootNonce);
    writer.U32(_boots);
    _spi->Save(writer);
    _link->Save(writer);
    _transport->Save(writer);
    _firmware->Save(writer);
}

bool Vdac4Esp::Load(const uint8_t* data, size_t size)
{
    Vdac4EspStateReader reader(data, size);
    const bool header = reader.U32() == kStateMagic && reader.U16() == kStateVersion;
    const uint8_t phase = reader.U8();
    if (!header || !reader.Ok() || phase > static_cast<uint8_t>(Phase::RestartWait))
    {
        PowerOn();  // a state that does not load leaves a freshly booting ESP, never a half-loaded one
        return false;
    }
    _phase = static_cast<Phase>(phase);
    _phaseStartMs = reader.U32();
    _lastPassMs = reader.U32();
    _nonceState = reader.U32();
    _bootNonce = reader.U8();
    _boots = reader.U32();
    const bool ok = _spi->Load(reader) && _link->Load(reader) && _transport->Load(reader) &&
                    _firmware->Load(reader) && reader.Ok() && reader.AtEnd();
    if (!ok)
        PowerOn();  // a state that does not load leaves a freshly booting ESP, never a half-loaded one
    return ok;
}
