#include "stdafx.h"

#include "vdac4board.h"

#include "common/filehelper.h"
#include "common/processtempfolder.h"
#include "emulator/config.h"
#include "base/featuremanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "esp/espxtensaruntime.h"
#include "vdac4audio.h"
#include "vdac4esp.h"
#include "vdac4espfirmware.h"
#include "vdac4ftworker.h"
#include "vdac4tang.h"

#include <cstdlib>
#include <cstring>

/// The ESP's LINK bus to the Tang (tang::read / tang::write). An ESP job's accesses happen at its stamp through
/// the FT worker (on its thread or at once); the firmware's own (the FT detach guard, the Z80's TANG_READ /
/// TANG_WRITE) wait for the ESP's job and the FT worker, then bring the clock to the CPU
class Vdac4Board::TangLink final : public IVdac4TangLink
{
public:
    explicit TangLink(Vdac4Board& board) : _board(board) {}

    bool LinkRead(uint32_t address, uint32_t& value) override
    {
        if (vdac4::EspXtensaRuntime::InJob())
        {
            value = _board._ft->Read(_board._jobStamp, address);
            return true;
        }
        Prepare();
        value = _board._tang.LinkRead(address);
        return true;
    }
    bool LinkWrite(uint32_t address, uint32_t value) override
    {
        if (vdac4::EspXtensaRuntime::InJob())
        {
            _board._ft->Write(_board._jobStamp, address, value);
            return true;
        }
        Prepare();
        _board._tang.LinkWrite(address, value);
        return true;
    }
    bool LinkFtWrite(uint32_t ftAddress, const uint8_t* words, size_t count) override
    {
        if (vdac4::EspXtensaRuntime::InJob())
        {
            _board._ft->WriteFt(_board._jobStamp, ftAddress, words, count);
            return true;
        }
        Prepare();
        _board._tang.LinkFtWrite(ftAddress, words, count);
        return true;
    }

private:
    void Prepare()
    {
        if (_board._runtime)
            _board._runtime->WaitIdle();
        if (_board._catchUp)
            _board._catchUp();
    }

    Vdac4Board& _board;
};

Vdac4Board::Vdac4Board(EmulatorContext* context, Vdac4Tang& tang, std::function<uint64_t()> nowNs,
                       std::function<void()> catchUp)
    : _context(context), _tang(tang), _catchUp(std::move(catchUp))
{
    _link = std::make_unique<TangLink>(*this);
    _ft = std::make_unique<Vdac4FtWorker>(tang);
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
    // The app's Advance runs as a job while it is the FT812's host (design §7.5): the Tang and its chip are the
    // job's then, the main thread waits for it before it touches them
    vdac4::EspJobHooks hooks;
    hooks.hosted = [this]() { return _tang.GetPersonality() == Vdac4Tang::Personality::Ft && _tang.IsEspHost(); };
    hooks.stamp = [this]() {
        if (_catchUp)
            _catchUp();
        return _tang.CurrentTact();
    };
    // The job's Tang accesses go through the FT worker (vdac4ftworker.h): the Tang is not touched here
    hooks.begin = [this](uint64_t stamp) { _jobStamp = stamp; };
    hooks.end = [this](uint64_t stamp, uint64_t job) { _ft->EndJob(stamp, job); };
    hooks.hostedAfter = [this](uint64_t job) { return _ft->JobHosted(job); };
    hooks.outstanding = [this](bool outstanding) {
        if (!outstanding)
            _ft->WaitIdle();
        _tang.SetEspJobOutstanding(outstanding);
    };
    _runtime->SetJobHooks(std::move(hooks));
    // UNREAL_VDAC4_ESP_THREAD=0/1 overrides [VDAC4] EspThread (A/B runs of one build)
    bool threaded = config.vdac4_esp_thread;
    if (const char* env = std::getenv("UNREAL_VDAC4_ESP_THREAD"); env && *env)
        threaded = std::strcmp(env, "0") != 0;
    _runtime->SetThreaded(threaded);
    // UNREAL_VDAC4_FT_THREAD=0/1 overrides [VDAC4] FtThread
    bool ftThreaded = config.vdac4_ft_thread;
    if (const char* env = std::getenv("UNREAL_VDAC4_FT_THREAD"); env && *env)
        ftThreaded = std::strcmp(env, "0") != 0;
    _ft->SetThreaded(ftThreaded);
    // UNREAL_VDAC4_ESP_LATENCY=<ms> overrides [VDAC4] EspLatencyMs
    uint32_t latency = config.vdac4_esp_latency_ms;
    if (const char* env = std::getenv("UNREAL_VDAC4_ESP_LATENCY"); env && *env)
        latency = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    _runtime->SetLatency(latency);
    ApplyJitFeature();
    _tang.SetEspWait([this]() { _runtime->WaitIdle(); });

    Vdac4Esp::Config espConfig;
    // [VDAC4] EspCache, else the desktop app's folder that outlives it, else (tests, headless) an empty one of this
    // board's own: then every run of a program starts from the same ESP flash, whatever ran before or beside it
    if (config.vdac4_esp_cache[0] != '\0')
        espConfig.cacheDirectory = FileHelper::ToFsPath(config.vdac4_esp_cache);
    else if (!Config::DefaultVdac4EspCacheFolder().empty())
        espConfig.cacheDirectory = FileHelper::ToFsPath(Config::DefaultVdac4EspCacheFolder());
    else if (!(_ownCache = ProcessTempFolder::Make(kOwnCachePrefix)).empty())
        espConfig.cacheDirectory = _ownCache;
    else
        espConfig.cacheDirectory = FileHelper::ToFsPath(FileHelper::GetUserDataPath()) / "vdac4" / "zm";
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
    _tang.SetEspWait(nullptr);
    // The ESP stops first (no more PCM), then the row leaves the mixer
    _esp.reset();
    _runtime.reset();
    _ft.reset();
    if (!_ownCache.empty())
        FileHelper::DeleteFolder(FileHelper::FromFsPath(_ownCache));
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
    // A machine frame ends with the ESP's outputs handed over: no job in flight across a frame boundary (TTD
    // checkpoints, snapshots, the frame-end probes and the published state see a settled ESP)
    _runtime->Flush();
    ApplyJitFeature();
    Publish();
}

void Vdac4Board::ApplyJitFeature()
{
    // UNREAL_XTENSA_JIT=0/1 overrides the esp_jit feature (A/B runs of one build)
    static const char* const env = std::getenv("UNREAL_XTENSA_JIT");
    bool on = true;
    if (env && *env)
        on = std::strcmp(env, "0") != 0;
    else if (_context && _context->pFeatureManager)
        on = _context->pFeatureManager->isEnabled(Features::kEspJit);
    if (_jit != (on ? 1 : 0))
    {
        _jit = on ? 1 : 0;
        _runtime->SetJit(on);
    }
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
