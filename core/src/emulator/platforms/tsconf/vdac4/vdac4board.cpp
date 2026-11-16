#include "stdafx.h"

#include "vdac4board.h"

#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "esp/espxtensaruntime.h"
#include "vdac4audio.h"
#include "vdac4esp.h"
#include "vdac4espfirmware.h"
#include "vdac4tang.h"

/// The ESP's LINK bus to the Tang (tang::read / tang::write)
class Vdac4Board::TangLink final : public IVdac4TangLink
{
public:
    TangLink(Vdac4Tang& tang, std::function<void()> catchUp) : _tang(tang), _catchUp(std::move(catchUp)) {}

    bool LinkRead(uint32_t address, uint32_t& value) override
    {
        if (_catchUp)
            _catchUp();
        value = _tang.LinkRead(address);
        return true;
    }
    bool LinkWrite(uint32_t address, uint32_t value) override
    {
        if (_catchUp)
            _catchUp();
        _tang.LinkWrite(address, value);
        return true;
    }

private:
    Vdac4Tang& _tang;
    std::function<void()> _catchUp;
};

Vdac4Board::Vdac4Board(EmulatorContext* context, Vdac4Tang& tang, std::function<uint64_t()> nowNs,
                       std::function<void()> catchUp)
    : _context(context), _tang(tang), _link(std::make_unique<TangLink>(tang, std::move(catchUp)))
{
    const CONFIG& config = context->config;
    if (config.vdac4_esp_log[0] != '\0')
        _logFile.open(config.vdac4_esp_log, std::ios::app);

    // Audio: the firmware's I2S player (design §8.2 pcm_*) feeds the mixer's "VDAC4 ESP" row
    _audio = std::make_unique<Vdac4Audio>();
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->attachSlotCard(_audio.get());
        _audioAttached = true;
    }
    _runtime = std::make_unique<vdac4::EspXtensaRuntime>(
        [this](const int16_t* frames, size_t count, uint32_t rate) { _audio->Push(frames, count, rate); },
        [this](const std::string& line) { Log(line); }, config.vdac4_esp_mhz);

    Vdac4Esp::Config espConfig;
    espConfig.cacheDirectory = config.vdac4_esp_cache[0] != '\0'
                                   ? std::filesystem::path(config.vdac4_esp_cache)
                                   : std::filesystem::path(FileHelper::GetUserDataPath()) / "vdac4" / "zm";
    espConfig.bootMs = config.vdac4_esp_boot_ms;
    // The ESP's clock never goes back. The machine's emulated time restarts at 0 on every machine reset
    // (Core::Reset clears t_states; an SPG / snapshot load resets too), while the board's ESP keeps running on
    // its own supply: the ESP time continues from where it was (an offset, as Ds12887::RebaseEmulatedTime does
    // for the battery clock). Without it the frame parser and every millis() difference in the firmware see a
    // step back as a wrap of 49 days (a frame timed out: the Z80's MOD_PUT lost)
    auto monotonic = [this, source = std::move(nowNs)]() {
        const uint64_t now = source() + _offsetNs;
        if (now < _lastNs)
        {
            _offsetNs += _lastNs - now;
            _backSteps++;
            return _lastNs;
        }
        _lastNs = now;
        return now;
    };
    _esp = std::make_unique<Vdac4Esp>(std::move(monotonic), espConfig);
    _esp->SetRuntime(_runtime.get());
    _esp->SetTang(_link.get());
    _esp->Firmware().SetPacketObserver([this](uint8_t command, size_t length, uint32_t nowMs) {
        std::lock_guard<std::mutex> lock(_ringLock);
        ++_packetCount;
        _recentPackets.push_back({command, static_cast<uint32_t>(length), nowMs});
        if (_recentPackets.size() > kRecentPackets)
            _recentPackets.pop_front();
    });
}

Vdac4Board::~Vdac4Board()
{
    // The ESP stops first (no more PCM), then the row leaves the mixer
    _esp.reset();
    _runtime.reset();
    if (_audioAttached && _context && _context->pSoundManager)
        _context->pSoundManager->detachSlotCard(_audio.get());
}

SpiDevice& Vdac4Board::Spi()
{
    return _esp->Spi();
}

void Vdac4Board::PowerOn()
{
    _esp->PowerOn();
    Publish();
}

void Vdac4Board::OnFrameEnd()
{
    _esp->Advance();
    Publish();
}

void Vdac4Board::Publish()
{
    Vdac4Control::BoardState state;
    Vdac4Control::CaptureLive(*this, _tang, state);
    std::lock_guard<std::mutex> lock(_ringLock);
    _published = std::move(state);
}

Vdac4Control::BoardState Vdac4Board::Published() const
{
    std::lock_guard<std::mutex> lock(_ringLock);
    return _published;
}

void Vdac4Board::Log(const std::string& line)
{
    if (_logFile.is_open())
    {
        _logFile << line << '\n';
        _logFile.flush();
    }
    std::lock_guard<std::mutex> lock(_ringLock);
    _recentLog.push_back(line);
    if (_recentLog.size() > kRecentLogLines)
        _recentLog.pop_front();
}

std::vector<std::string> Vdac4Board::RecentLog(size_t lines) const
{
    std::lock_guard<std::mutex> lock(_ringLock);
    const size_t count = std::min(lines, _recentLog.size());
    return {_recentLog.end() - static_cast<std::ptrdiff_t>(count), _recentLog.end()};
}

std::vector<Vdac4Board::Packet> Vdac4Board::RecentPackets(uint64_t* count) const
{
    std::lock_guard<std::mutex> lock(_ringLock);
    if (count)
        *count = _packetCount;
    return {_recentPackets.begin(), _recentPackets.end()};
}
