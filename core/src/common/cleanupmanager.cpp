#include "common/cleanupmanager.h"

#include <cstdio>
#include <exception>
#include <fstream>
#include <map>
#include <sstream>

#include "common/filehelper.h"
#include "common/logger.h"

void CleanupContext::Removed(const std::string& what)
{
    _removed.push_back(what);
}

void CleanupContext::Failed(const std::string& what, const std::string& reason)
{
    _failed.push_back(what + ": " + reason);
}

namespace
{
    /// name -> unix seconds of the last completed run; a damaged line is skipped
    std::map<std::string, int64_t> ReadState(const std::string& path)
    {
        std::map<std::string, int64_t> state;
        std::ifstream in(FileHelper::ToFsPath(path));
        std::string line;
        while (std::getline(in, line))
        {
            const size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0)
                continue;
            try
            {
                state[line.substr(0, tab)] = std::stoll(line.substr(tab + 1));
            }
            catch (const std::exception&)
            {
            }
        }
        return state;
    }

    bool WriteState(const std::string& path, const std::map<std::string, int64_t>& state)
    {
        // Written beside and renamed over, so a crash never leaves half a file
        const std::string temp = path + ".tmp";
        {
            std::ofstream out(FileHelper::ToFsPath(temp), std::ios::trunc);
            if (!out)
                return false;
            for (const auto& [name, when] : state)
                out << name << '\t' << when << '\n';
            if (!out)
                return false;
        }
        std::error_code ec;
        std::filesystem::rename(FileHelper::ToFsPath(temp), FileHelper::ToFsPath(path), ec);
        return !ec;
    }
}  // namespace

CleanupManager::~CleanupManager()
{
    Stop();
}

CleanupManager& CleanupManager::Instance()
{
    static CleanupManager manager;
    return manager;
}

void CleanupManager::AddStep(CleanupStep step)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (CleanupStep& existing : _steps)
        if (existing.name == step.name)
        {
            existing = std::move(step);
            return;
        }
    _steps.push_back(std::move(step));
}

void CleanupManager::RunAsync()
{
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true))
        return;
    if (_thread.joinable())
        _thread.join();
    _stop = false;
    try
    {
        _thread = std::thread([this]() {
            try
            {
                RunDue();
            }
            catch (...)
            {
                LOGWARNING("CleanupManager: the cleanup run failed unexpectedly");
            }
            _running = false;
        });
    }
    catch (const std::exception& e)
    {
        // No thread (resources): cleanup is housekeeping, the emulator goes on without it
        _running = false;
        LOGWARNING("CleanupManager: cannot start the cleanup thread: %s", e.what());
    }
}

std::vector<CleanupStepReport> CleanupManager::RunNow()
{
    Wait();
    _stop = false;
    return RunDue();
}

void CleanupManager::Wait()
{
    if (_thread.joinable())
        _thread.join();
}

void CleanupManager::Stop()
{
    _stop = true;
    Wait();
}

std::vector<CleanupStepReport> CleanupManager::LastReports() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _lastReports;
}

void CleanupManager::SetStatePath(const std::string& path)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _statePath = path;
}

void CleanupManager::SetClock(std::function<int64_t()> nowUnixSeconds)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _now = std::move(nowUnixSeconds);
}

std::string CleanupManager::StatePath() const
{
    if (!_statePath.empty())
        return _statePath;
    const std::string folder = FileHelper::GetUserDataFolder("");
    return folder.empty() ? std::string() : FileHelper::PathCombine(folder, "cleanup-state.txt");
}

std::vector<CleanupStepReport> CleanupManager::RunDue()
{
    std::vector<CleanupStep> steps;
    std::string statePath;
    int64_t now = 0;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        steps = _steps;
        statePath = StatePath();
        now = _now ? _now()
                   : std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    }

    std::map<std::string, int64_t> state = statePath.empty() ? std::map<std::string, int64_t>{} : ReadState(statePath);
    std::vector<CleanupStepReport> reports;
    bool stateChanged = false;
    for (const CleanupStep& step : steps)
    {
        CleanupStepReport report;
        report.name = step.name;
        const auto last = state.find(step.name);
        const bool due = last == state.end() || now - last->second >= step.interval.count() || now < last->second;
        if (!due || !step.run || _stop)
        {
            reports.push_back(std::move(report));
            continue;
        }

        report.ran = true;
        CleanupContext context(_stop);
        try
        {
            step.run(context);
            report.completed = !_stop;
        }
        catch (const std::exception& e)
        {
            report.error = e.what();
        }
        catch (...)
        {
            report.error = "unknown exception";
        }
        report.removed = context.RemovedItems();
        report.failed = context.FailedItems();

        if (!report.error.empty())
            LOGWARNING("CleanupManager: step '%s' failed: %s", step.name.c_str(), report.error.c_str());
        for (const std::string& failed : report.failed)
            LOGWARNING("CleanupManager: step '%s' could not remove %s", step.name.c_str(), failed.c_str());
        if (!report.removed.empty())
            LOGINFO("CleanupManager: step '%s' removed %zu item(s)", step.name.c_str(), report.removed.size());

        if (report.completed)
        {
            state[step.name] = now;
            stateChanged = true;
        }
        reports.push_back(std::move(report));
    }

    if (stateChanged && !statePath.empty() && !WriteState(statePath, state))
        LOGWARNING("CleanupManager: cannot write %s", statePath.c_str());

    std::lock_guard<std::mutex> lock(_mutex);
    _lastReports = reports;
    return reports;
}
