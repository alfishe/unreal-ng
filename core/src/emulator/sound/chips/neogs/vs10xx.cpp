#include "vs10xx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/statebytes.h"

// minimp3 is third-party code: keep the project's warning set away from it
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#define MINIMP3_IMPLEMENTATION
#include "3rdparty/minimp3/minimp3.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/// minimp3's state: a plain struct of arrays (no pointers), so it can be
/// copied byte for byte into a snapshot
struct Vs10xxMp3State
{
    mp3dec_t dec;
};

namespace
{
constexpr int kBitratesV1[3][16] = {
    {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0}, // layer I
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},    // layer II
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},     // layer III
};
constexpr int kBitratesV2[3][16] = {
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}, // layer I
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},      // layers II and III
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
};
constexpr int kRates[4] = {44100, 48000, 32000, 0}; // MPEG-1; /2 for MPEG-2, /4 for 2.5
} // namespace

/// region <Construction and resets>

Vs10xxDecoder::Vs10xxDecoder(Chip chip, Level level, double unitsPerSecond)
    : _chip(chip)
    , _level(level)
    , _unitsPerSecond(unitsPerSecond)
    , _sciPort(*this)
    , _sdiPort(*this)
    , _dec(std::make_unique<Vs10xxMp3State>())
{
    // mp3dec_init only sets header[0]: zero the whole state so snapshots of it
    // are byte-stable
    memset(&_dec->dec, 0, sizeof(mp3dec_t));
    mp3dec_init(&_dec->dec);
    _regs.fill(0);
}

Vs10xxDecoder::~Vs10xxDecoder() = default;

void Vs10xxDecoder::clearStream()
{
    _fifo.clear();
    _pcm.clear();
    _pcmRead = 0;
    memset(&_dec->dec, 0, sizeof(mp3dec_t));
    mp3dec_init(&_dec->dec);
    _playFraction = 0;
    _samplesPlayed = 0;
    _decodedSamplesSecondBase = 0;
    _regs[SCI_HDAT0] = 0;
    _regs[SCI_HDAT1] = 0;
    _regs[SCI_DECODE_TIME] = 0;
}

void Vs10xxDecoder::setReset(bool running, int64_t now)
{
    if (running == _running)
        return;
    _now = std::max(_now, now);
    _running = running;
    if (!running)
    {
        // Held in reset: every register reads 0 (VOL 0 = full volume)
        _regs.fill(0);
        clearStream();
        _rate = 0;
        _channels = 0;
        return;
    }
    _regs.fill(0);
    _regs[SCI_STATUS] = static_cast<uint16_t>(_chip == Chip::VS1011 ? 0x0010 : 0x0000);
    _busyUntil = now + static_cast<int64_t>(HARD_RESET_CLOCKS / CRYSTAL_HZ * _unitsPerSecond);
}

void Vs10xxDecoder::softReset()
{
    const uint16_t vol = _regs[SCI_VOL];
    const uint16_t clockf = _regs[SCI_CLOCKF];
    clearStream();
    _regs[SCI_VOL] = vol;          // a software reset keeps VOL
    _regs[SCI_CLOCKF] = clockf;
    _busyUntil = _now + static_cast<int64_t>(SOFT_RESET_CLOCKS / CRYSTAL_HZ * _unitsPerSecond);
}

/// endregion </Construction and resets>

/// region <SCI>

void Vs10xxDecoder::SciPort::select(bool selected)
{
    _selected = selected;
    _count = 0; // every chip select starts a new frame
}

uint8_t Vs10xxDecoder::SciPort::exchange(uint8_t mosi)
{
    if (!_selected || !_owner._running)
        return 0xFF;

    uint8_t miso = 0x00;
    switch (_count)
    {
        case 0:
            _op = mosi;
            break;
        case 1:
            _addr = static_cast<uint8_t>(mosi & 0x0F);
            break;
        case 2:
            if (_op == 0x03)
                miso = static_cast<uint8_t>(_owner.readRegister(_addr) >> 8);
            _hi = mosi;
            break;
        case 3:
            if (_op == 0x03)
                miso = static_cast<uint8_t>(_owner.readRegister(_addr));
            else if (_op == 0x02)
                _owner.writeRegister(_addr, static_cast<uint16_t>((_hi << 8) | mosi));
            break;
        default:
            return 0x00;
    }
    _count = (_count + 1) & 3;
    return miso;
}

uint16_t Vs10xxDecoder::readRegister(uint8_t index)
{
    if (index == SCI_DECODE_TIME && _rate)
        _regs[SCI_DECODE_TIME] = static_cast<uint16_t>(_decodedSamplesSecondBase + _samplesPlayed / _rate);
    return _regs[index];
}

void Vs10xxDecoder::writeRegister(uint8_t index, uint16_t value)
{
    switch (index)
    {
        case SCI_MODE:
            _regs[SCI_MODE] = static_cast<uint16_t>(value & ~SM_RESET);
            if (value & SM_RESET)
                softReset();
            return;
        case SCI_STATUS:
            // Version bits are read-only
            _regs[SCI_STATUS] = static_cast<uint16_t>((value & ~0x0070) | (_regs[SCI_STATUS] & 0x0070));
            return;
        case SCI_DECODE_TIME:
            // Writable: software resets the counter
            _decodedSamplesSecondBase = value;
            _samplesPlayed = 0;
            _regs[SCI_DECODE_TIME] = value;
            return;
        case SCI_AUDATA:
        case SCI_HDAT0:
        case SCI_HDAT1:
            return; // read-only
        default:
            _regs[index] = value; // register 2, CLOCKF, WRAM, WRAMADDR, AIADDR, VOL: stored
            return;
    }
}

/// endregion </SCI>

/// region <SDI and the data path>

uint8_t Vs10xxDecoder::SdiPort::exchange(uint8_t mosi)
{
    Vs10xxDecoder& d = _owner;
    if (!d._running)
        return 0xFF;
    d._bytesReceived++;
    if (d._level == Level::Stub)
        return 0xFF; // accepted and dropped: DREQ stays up
    if (d._fifo.size() < INPUT_FIFO)
        d._fifo.push_back(mosi);
    return 0xFF;
}

bool Vs10xxDecoder::dreq(int64_t now)
{
    advance(now);
    if (!_running || now < _busyUntil)
        return false;
    return INPUT_FIFO - _fifo.size() >= DREQ_FREE;
}

int Vs10xxDecoder::parseFrameLength(size_t at, uint32_t& header) const
{
    if (_fifo.size() < at + 4)
        return -1;
    const uint8_t b0 = _fifo[at], b1 = _fifo[at + 1], b2 = _fifo[at + 2], b3 = _fifo[at + 3];
    if (b0 != 0xFF || (b1 & 0xE0) != 0xE0)
        return 0;
    const int version = (b1 >> 3) & 3; // 0 = 2.5, 2 = 2, 3 = 1
    const int layer = 4 - ((b1 >> 1) & 3); // 1..3
    const int bitrateIndex = b2 >> 4;
    const int rateIndex = (b2 >> 2) & 3;
    if (version == 1 || layer == 4 || bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3)
        return 0; // reserved values; free format is not supported
    const bool mpeg1 = version == 3;
    int rate = kRates[rateIndex];
    if (version == 2)
        rate /= 2;
    else if (version == 0)
        rate /= 4;
    const int kbps = mpeg1 ? kBitratesV1[layer - 1][bitrateIndex] : kBitratesV2[layer - 1][bitrateIndex];
    const int padding = (b2 >> 1) & 1;
    int length;
    if (layer == 1)
        length = (12 * kbps * 1000 / rate + padding) * 4;
    else if (layer == 3 && !mpeg1)
        length = 72 * kbps * 1000 / rate + padding;
    else
        length = 144 * kbps * 1000 / rate + padding;
    header = (static_cast<uint32_t>(b0) << 24) | (static_cast<uint32_t>(b1) << 16) | (static_cast<uint32_t>(b2) << 8) | b3;
    return length;
}

bool Vs10xxDecoder::decodeNextFrame()
{
    for (;;)
    {
        if (_fifo.size() < 10)
            return false;

        // ID3v2 tag: "ID3", version, flags, 4 syncsafe size bytes
        if (_fifo[0] == 'I' && _fifo[1] == 'D' && _fifo[2] == '3')
        {
            const size_t size = 10 + ((_fifo[6] & 0x7F) << 21 | (_fifo[7] & 0x7F) << 14 | (_fifo[8] & 0x7F) << 7 | (_fifo[9] & 0x7F));
            const size_t drop = std::min(size, _fifo.size());
            _fifo.erase(_fifo.begin(), _fifo.begin() + static_cast<std::ptrdiff_t>(drop));
            continue;
        }

        uint32_t header = 0;
        const int length = parseFrameLength(0, header);
        if (length == 0)
        {
            _fifo.pop_front(); // junk: resynchronise byte by byte
            continue;
        }
        if (length < 0 || _fifo.size() < static_cast<size_t>(length))
            return false; // wait for the rest of the frame

        uint8_t frame[2881];
        const size_t n = std::min(static_cast<size_t>(length), sizeof frame);
        std::copy_n(_fifo.begin(), n, frame);
        _fifo.erase(_fifo.begin(), _fifo.begin() + static_cast<std::ptrdiff_t>(length));

        mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        mp3dec_frame_info_t info{};
        const int samples = mp3dec_decode_frame(&_dec->dec, frame, static_cast<int>(n), pcm, &info);
        _regs[SCI_HDAT1] = static_cast<uint16_t>(header >> 16);
        _regs[SCI_HDAT0] = static_cast<uint16_t>(header);
        if (samples <= 0)
            continue; // e.g. the Xing/LAME info frame, or a frame without its reservoir yet

        _framesDecoded++;
        _rate = static_cast<uint32_t>(info.hz);
        _channels = info.channels;
        _bitrateKbps = info.bitrate_kbps;
        updateAudata();

        // Compact the played part, then append (always stereo)
        if (_pcmRead > 0)
        {
            _pcm.erase(_pcm.begin(), _pcm.begin() + static_cast<std::ptrdiff_t>(_pcmRead));
            _pcmRead = 0;
        }
        for (int i = 0; i < samples; i++)
        {
            const int16_t l = pcm[i * info.channels];
            const int16_t r = info.channels > 1 ? pcm[i * info.channels + 1] : l;
            if (_level == Level::Software)
            {
                _pcm.push_back(l);
                _pcm.push_back(r);
            }
            else
            {
                _pcm.push_back(0);
                _pcm.push_back(0);
            }
        }
        return true;
    }
}

void Vs10xxDecoder::updateAudata()
{
    const bool stereo = _channels > 1;
    if (_chip == Chip::VS1011)
    {
        _regs[SCI_AUDATA] = static_cast<uint16_t>((_rate & 0xFFFE) | (stereo ? 1 : 0));
        return;
    }
    // VS1001 (datasheet p.26): bits 8:0 kbit/s, 12:9 sample-rate index, 15 stereo
    static constexpr uint32_t rates[] = {44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};
    int index = 0;
    for (int i = 0; i < 9; i++)
    {
        if (rates[i] == _rate)
            index = i;
    }
    _regs[SCI_AUDATA] = static_cast<uint16_t>((stereo ? 0x8000 : 0) | (index << 9) | (_bitrateKbps & 0x1FF));
}

void Vs10xxDecoder::advance(int64_t now)
{
    if (now <= _now)
        return;
    const int64_t elapsed = now - _now;
    _now = now;
    if (!_running || now < _busyUntil)
        return;

    // Keep the chip's audio FIFO topped up, then play what the elapsed time owes
    while (pcmQueued() < PCM_LOW_WATER && decodeNextFrame())
    {
    }
    if (_rate == 0)
        return;

    _playFraction += static_cast<double>(elapsed) * _rate / _unitsPerSecond;
    size_t owed = static_cast<size_t>(_playFraction);
    _playFraction -= static_cast<double>(owed);
    while (owed > 0)
    {
        if (pcmQueued() == 0 && !decodeNextFrame())
        {
            _playFraction = 0; // underrun: time passes, nothing plays, DECODE_TIME freezes
            break;
        }
        const size_t take = std::min(owed, pcmQueued());
        _played.insert(_played.end(), _pcm.begin() + static_cast<std::ptrdiff_t>(_pcmRead),
                       _pcm.begin() + static_cast<std::ptrdiff_t>(_pcmRead + take * 2));
        _pcmRead += take * 2;
        _samplesPlayed += take;
        owed -= take;
        if (pcmQueued() < PCM_LOW_WATER)
            decodeNextFrame();
    }
    // The played history never needs more than a few frames
    if (_played.size() > 1 << 20)
        _played.erase(_played.begin(), _played.end() - (1 << 16));
}

bool Vs10xxDecoder::renderFrame(int16_t* out, int samples, double gain)
{
    if (samples <= 0)
        return false;

    // VOL: 0.5 dB steps per channel, high byte left; #FFFF powers down
    const uint16_t vol = _regs[SCI_VOL];
    const double gl = vol == 0xFFFF ? 0.0 : gain * std::pow(10.0, -0.5 * (vol >> 8) / 20.0);
    const double gr = vol == 0xFFFF ? 0.0 : gain * std::pow(10.0, -0.5 * (vol & 0xFF) / 20.0);

    const size_t available = _played.size() / 2;
    bool audible = false;
    if (available == 0)
    {
        std::fill_n(out, samples * 2, int16_t{0});
        _lastL = _lastR = 0;
        return false;
    }

    // Linear resampling of this frame's played samples onto `samples`
    // outputs, continuing from the previous frame's last sample
    const double step = static_cast<double>(available) / samples;
    for (int i = 0; i < samples; i++)
    {
        const double pos = (i + 1) * step - 1.0; // -1 = previous frame's last sample
        const double base = std::floor(pos);
        const double frac = pos - base;
        const long index = static_cast<long>(base);
        auto at = [&](long k, int side) -> double
        {
            if (k < 0)
                return side ? _lastR : _lastL;
            return _played[static_cast<size_t>(k) * 2 + side];
        };
        const double l = at(index, 0) + (at(std::min<long>(index + 1, static_cast<long>(available) - 1), 0) - at(index, 0)) * frac;
        const double r = at(index, 1) + (at(std::min<long>(index + 1, static_cast<long>(available) - 1), 1) - at(index, 1)) * frac;
        const long ol = std::lround(std::clamp(l * gl, -32768.0, 32767.0));
        const long orr = std::lround(std::clamp(r * gr, -32768.0, 32767.0));
        out[i * 2] = static_cast<int16_t>(ol);
        out[i * 2 + 1] = static_cast<int16_t>(orr);
        audible |= ol != 0 || orr != 0;
    }
    _lastL = _played[(available - 1) * 2];
    _lastR = _played[(available - 1) * 2 + 1];
    _played.clear();
    return audible;
}

/// endregion </SDI and the data path>

/// region <State snapshot>

// Layout: 0 version, 1 running, 2 registers (16 x u16), 34 now, 42 busyUntil,
// 50 rate u32, 54 channels u8, 55 bitrate u16, 57 playFraction (double bits),
// 65 framesDecoded, 73 samplesPlayed, 81 bytesReceived, 89 decodedSamplesSecondBase,
// 97 lastL, 99 lastR, 101 SCI op/addr/hi/count/selected, 106 FIFO length u16,
// 108 PCM length u16 (int16 values), 112 FIFO (2048), then the PCM, then minimp3's state
namespace
{
constexpr size_t kStFifo = 112;
constexpr size_t kStPcm = kStFifo + Vs10xxDecoder::INPUT_FIFO;
constexpr size_t kStDec = kStPcm + Vs10xxDecoder::STATE_PCM_MAX * 2;
static_assert(kStDec + sizeof(mp3dec_t) <= Vs10xxDecoder::STATE_SIZE);
} // namespace

void Vs10xxDecoder::saveState(uint8_t* dst, bool machineVisibleOnly) const
{
    using namespace statebytes;
    memset(dst, 0, STATE_SIZE);
    dst[0] = 1;
    dst[1] = _running ? 1 : 0;
    for (int i = 0; i < REGISTERS; i++)
        put16(dst + 2 + i * 2, _regs[static_cast<size_t>(i)]);
    put64(dst + 34, _now);
    put64(dst + 42, _busyUntil);
    put32(dst + 50, _rate);
    dst[54] = static_cast<uint8_t>(_channels);
    put16(dst + 55, static_cast<uint16_t>(_bitrateKbps));
    putDouble(dst + 57, _playFraction);
    putU64(dst + 65, _framesDecoded);
    putU64(dst + 73, _samplesPlayed);
    putU64(dst + 81, _bytesReceived);
    putU64(dst + 89, _decodedSamplesSecondBase);
    put16(dst + 97, static_cast<uint16_t>(_lastL));
    put16(dst + 99, static_cast<uint16_t>(_lastR));
    dst[101] = _sciPort._op;
    dst[102] = _sciPort._addr;
    dst[103] = _sciPort._hi;
    dst[104] = static_cast<uint8_t>(_sciPort._count);
    dst[105] = _sciPort._selected ? 1 : 0;

    const size_t fifo = std::min(_fifo.size(), INPUT_FIFO);
    put16(dst + 106, static_cast<uint16_t>(fifo));
    std::copy_n(_fifo.begin(), fifo, dst + kStFifo);

    // Only the level of the PCM queue drives the machine (when the next frame
    // is decoded); its samples and minimp3's floats are audio
    const size_t pcm = std::min(_pcm.size() - _pcmRead, STATE_PCM_MAX);
    put16(dst + 108, static_cast<uint16_t>(pcm));
    if (machineVisibleOnly)
        return;
    for (size_t i = 0; i < pcm; i++)
        put16(dst + kStPcm + i * 2, static_cast<uint16_t>(_pcm[_pcmRead + i]));
    memcpy(dst + kStDec, &_dec->dec, sizeof(mp3dec_t));
}

void Vs10xxDecoder::loadState(const uint8_t* src)
{
    using namespace statebytes;
    _running = src[1] != 0;
    for (int i = 0; i < REGISTERS; i++)
        _regs[static_cast<size_t>(i)] = get16(src + 2 + i * 2);
    _now = get64(src + 34);
    _busyUntil = get64(src + 42);
    _rate = get32(src + 50);
    _channels = src[54];
    _bitrateKbps = get16(src + 55);
    _playFraction = getDouble(src + 57);
    _framesDecoded = getU64(src + 65);
    _samplesPlayed = getU64(src + 73);
    _bytesReceived = getU64(src + 81);
    _decodedSamplesSecondBase = getU64(src + 89);
    _lastL = static_cast<int16_t>(get16(src + 97));
    _lastR = static_cast<int16_t>(get16(src + 99));
    _sciPort._op = src[101];
    _sciPort._addr = src[102];
    _sciPort._hi = src[103];
    _sciPort._count = src[104];
    _sciPort._selected = src[105] != 0;

    const size_t fifo = std::min<size_t>(get16(src + 106), INPUT_FIFO);
    _fifo.assign(src + kStFifo, src + kStFifo + fifo);
    const size_t pcm = std::min<size_t>(get16(src + 108), STATE_PCM_MAX);
    _pcm.resize(pcm);
    _pcmRead = 0;
    for (size_t i = 0; i < pcm; i++)
        _pcm[i] = static_cast<int16_t>(get16(src + kStPcm + i * 2));
    memcpy(&_dec->dec, src + kStDec, sizeof(mp3dec_t));
    _played.clear(); // not yet rendered audio of the old timeline
}

/// endregion </State snapshot>
