#include "espxtensaruntime.h"

#include "espapphost.h"
#include "espxtensacpu.h"

namespace vdac4
{

EspXtensaRuntime::EspXtensaRuntime(std::function<void(const int16_t*, size_t, uint32_t)> audio,
                                   std::function<void(const std::string&)> log, uint32_t mhz)
    : _log(std::move(log))
{
    EspAppHostPorts ports;
    ports.reply = [this](uint8_t command, const uint8_t* data, size_t size) {
        return _appHost ? _appHost->Reply(command, data, size) : static_cast<int>(kZmError);
    };
    ports.event = [this](uint8_t code, const uint8_t* data, size_t size) {
        return _appHost ? _appHost->SendEvent(code, data, size) : static_cast<int>(kZmError);
    };
    ports.declared = [this](const std::string& name, const std::string& version, uint8_t first, uint8_t last) {
        if (_info == nullptr)
            return;
        _info->name = name;
        _info->version = version;
        _info->commandFirst = first;
        _info->commandLast = last;
    };
    ports.mediaRange = [this](uint8_t first, uint8_t last) {
        return _appHost ? _appHost->SetMediaRange(first, last) : static_cast<int>(kZmError);
    };
    ports.appDone = [this]() {
        if (_appHost)
            _appHost->AppDone();
    };
    ports.ftOpened = [this]() {
        if (_appHost)
            _appHost->FtOpened(true);
    };
    ports.linkRead = [this](uint32_t address) {
        uint32_t value = 0;
        IVdac4TangLink* tang = _appHost ? _appHost->Tang() : nullptr;
        if (tang == nullptr || !tang->LinkRead(address, value))
            return 0u;
        return value;
    };
    ports.linkWrite = [this](uint32_t address, uint32_t value) {
        IVdac4TangLink* tang = _appHost ? _appHost->Tang() : nullptr;
        if (tang != nullptr)
            tang->LinkWrite(address, value);
    };
    ports.audio = std::move(audio);
    ports.log = [this](const std::string& line) {
        if (_log)
            _log(line);
    };
    _host = std::make_unique<EspAppHost>(CreateEspXtensaCpu(), std::move(ports), mhz);
}

EspXtensaRuntime::~EspXtensaRuntime() = default;

int EspXtensaRuntime::StartApp(const std::string& module, const std::vector<uint8_t>& elf, IVdac4AppHost& host,
                               Vdac4AppInfo& info)
{
    _appHost = &host;
    _info = &info;
    // The ESP's clock follows the firmware's: start from its time
    const uint64_t nowUs = static_cast<uint64_t>(host.NowMs()) * 1000;
    if (nowUs > _host->NowUs())
        _host->Advance(nowUs);

    bool answered = false;
    int result = kZmError;
    std::string name = module;
    const size_t dot = name.find('.');
    if (dot != std::string::npos)
        name.resize(dot);
    _host->StartApp(name, elf, [&](int r, const std::string&, const std::string&) {
        answered = true;
        result = r;
    });
    if (!_host->RunUntilLoopIdle(kCallLimitUs) || !answered)
    {
        if (_log)
            _log(module + ": start did not finish in 5 s");
        result = kZmError;
    }
    _info = nullptr;
    if (result != kZmOk)
    {
        _host->StopApp();
        _appHost = nullptr;
    }
    return result;
}

void EspXtensaRuntime::StopApp()
{
    _host->StopApp();
    _appHost = nullptr;
}

int EspXtensaRuntime::Command(uint8_t command, const uint8_t* data, size_t size)
{
    if (!_host->OwnsCommand(command))
        return kZmError;
    _host->Command(command, data, size);
    if (!_host->RunUntilLoopIdle(kCallLimitUs))
    {
        if (_log)
            _log("app command did not finish in 5 s");
        return kZmError;
    }
    return _host->LastCommandResult();
}

void EspXtensaRuntime::Poll(uint32_t nowMs)
{
    const uint64_t nowUs = static_cast<uint64_t>(nowMs) * 1000;
    if (nowUs > _host->NowUs())
        _host->Advance(nowUs);
}

}  // namespace vdac4
