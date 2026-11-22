#include "emulator/slots/cards/plusd/plusdautostart.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/analyzers/basic-lang/editormonitor.h"
#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/media/mediamanager.h"
#include "emulator/platform.h"
#include "emulator/slots/cards/plusd/plusdcard.h"
#include "unrealdisk/gdosfs.h"
#include "unrealdisk/floppyformats.h"

using unrealdisk::GdosFile;
using unrealdisk::GdosFileSystem;

namespace
{
constexpr uint8_t kBasic = 1;
constexpr uint8_t kCode = 4;
constexpr uint8_t kSnapshot48 = 5;
constexpr uint8_t kSnapshot128 = 9;

std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}


bool IsAuto(const GdosFile& f)
{
    return Lower(f.name).rfind("auto", 0) == 0;
}

bool IsSnapshot(const GdosFile& f)
{
    return f.type == kSnapshot48 || f.type == kSnapshot128;
}

/// `LOAD d1"<name>"` with the suffix of the file's type (S a 48K snapshot, K a 128K one)
std::string LoadCommand(const GdosFile& f)
{
    std::string command = "LOAD d1\"" + f.name + "\"";
    if (f.type == kSnapshot48)
        command += "S";
    else if (f.type == kSnapshot128)
        command += "K";
    return command;
}

/// The body of the system file on `image` (empty when it does not read)
std::vector<uint8_t> DosBody(unrealdisk::DiskImage& image, bool disciple)
{
    const auto IsSys = [disciple](const GdosFile& f) { return PlusDAutostart::IsSystemFile(f.name, f.type, disciple); };
    GdosFileSystem fs(image);
    std::vector<GdosFile> files;
    std::string error;
    if (!fs.List(files, error))
        return {};
    for (const GdosFile& f : files)
    {
        if (!IsSys(f))
            continue;
        std::vector<uint8_t> body;
        if (!fs.Read(f, body, error) || body.empty() || body.size() > PlusDCard::kRamSize)
            return {};
        return body;
    }
    return {};
}
}  // namespace

bool PlusDAutostart::IsSystemFile(const std::string& name, uint8_t type, bool disciple)
{
    return type == kCode && Lower(name).rfind(disciple ? "sys" : "+sys", 0) == 0;
}

std::string PlusDAutostart::FindSystemDisk(bool disciple)
{
    for (const std::string& base : {FileHelper::GetExecutablePath(), FileHelper::GetResourcesPath()})
    {
        const std::string path =
            FileHelper::PathCombine(base, disciple ? kDiscipleSystemDiskRelative : kSystemDiskRelative);
        if (FileHelper::FileExists(path))
            return path;
    }
    return {};
}

PlusDAutostart::Plan PlusDAutostart::MakePlan(unrealdisk::DiskImage& work, bool is48K, const std::string& systemDiskPath,
                                              bool disciple)
{
    const auto IsSys = [disciple](const GdosFile& f) { return IsSystemFile(f.name, f.type, disciple); };
    const char* const sysName = disciple ? "SYS" : "+SYS";
    const char* const dosName = disciple ? "GDOS" : "G+DOS";
    const char* const model128 = disciple ? "128K-DISCIPLE" : "128K-PLUSD";
    Plan plan;
    plan.kind = "mount";
    GdosFileSystem fs(work);
    std::string why;
    if (!fs.IsGdos(&why))
    {
        plan.message = "Autostart: not a G+DOS disk (" + why + ") - mounted only";
        return plan;
    }
    std::vector<GdosFile> files;
    if (!fs.List(files, why))
    {
        plan.message = "Autostart: the directory does not read (" + why + ") - mounted only";
        return plan;
    }

    const bool hasSys = std::any_of(files.begin(), files.end(), IsSys);
    const auto autoFile =
        std::find_if(files.begin(), files.end(), [&IsSys](const GdosFile& f) { return IsAuto(f) && !IsSys(f); });
    if (hasSys && autoFile != files.end())
    {
        plan.kind = "run";
        plan.dos = "disk";
        plan.startFile = autoFile->name;
        plan.message = std::string("Autostart: RUN (the disk's ") + sysName + " loads " + autoFile->name + ")";
        return plan;
    }

    // The start file, first match
    const GdosFile* start = nullptr;
    if (autoFile != files.end() && (autoFile->type == kBasic || IsSnapshot(*autoFile)))
        start = &*autoFile;
    if (!start)
    {
        static const char* const kLoaders[] = {"loader", "boot", "menu", "start"};
        for (const GdosFile& f : files)
            if (f.type == kBasic && std::find(std::begin(kLoaders), std::end(kLoaders), Lower(f.name)) != std::end(kLoaders))
            {
                start = &f;
                break;
            }
    }
    if (!start)
    {
        const auto basic = std::find_if(files.begin(), files.end(), [](const GdosFile& f) { return f.type == kBasic; });
        if (basic != files.end())
            start = &*basic;
    }
    if (!start)
    {
        const auto snapshot = std::find_if(files.begin(), files.end(), IsSnapshot);
        if (snapshot != files.end())
            start = &*snapshot;
    }
    if (!start)
    {
        plan.message = "Autostart: no program to start - mounted only";
        return plan;
    }
    if (start->type == kSnapshot128 && is48K)
    {
        plan.message = "Autostart: " + start->name + " is a 128K snapshot: use " + model128 + " - mounted only";
        return plan;
    }

    plan.startFile = start->name;
    plan.command = LoadCommand(*start);
    if (hasSys)
    {
        plan.kind = "run+load";
        plan.dos = "disk";
        plan.dosBody = DosBody(work, disciple);
    }
    else
    {
        if (systemDiskPath.empty())
        {
            plan.message = std::string("Autostart: the disk has no DOS and the system disk (") +
                           (disciple ? kDiscipleSystemDiskRelative : kSystemDiskRelative) + ") is missing - mounted only";
            return plan;
        }
        std::unique_ptr<unrealdisk::DiskImage> system;
        std::string format;
        if (!unrealdisk::FloppyFormats::Load({}, systemDiskPath, system, format).Ok() || !system)
        {
            plan.message = "Autostart: the system disk " + systemDiskPath + " does not load - mounted only";
            return plan;
        }
        plan.kind = "system-disk+load";
        plan.dos = "system-disk";
        plan.dosBody = DosBody(*system, disciple);
    }
    if (plan.dosBody.empty())
    {
        plan.kind = "mount";
        plan.message = std::string("Autostart: the ") + sysName + " file does not read - mounted only";
        return plan;
    }
    plan.message = "Autostart: " + plan.command +
                   (plan.dos == "system-disk" ? std::string(" (") + dosName + " from the system disk)" : std::string());
    return plan;
}

PlusDCard* PlusDAutostart::Card() const
{
    return PlusDCard::Find(_context);
}

void PlusDAutostart::Arm(const Plan& plan, const std::string& workPath)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _status = Status{};
    _status.active = true;
    _status.plan = plan;
    _workPath = workPath;
    _armedFrame = _context->emulatorState.frame_counter;
    _stageFrame = _armedFrame;
    _dosLoaded = false;
    _dosReady = false;
    PlusDCard* card = Card();
    _systemImage = plan.dos == "system-disk" && card && card->Drive(0) ? card->Drive(0)->getDiskImage() : nullptr;
    _status.steps.push_back({"reset", 0});
    _stage = Stage::TypeRun;
    _status.stage = "run";
}

void PlusDAutostart::Disarm()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_status.active)
    {
        _status.active = false;
        _status.finished = true;
        _status.message = "cancelled (" + _status.stage + ")";
    }
    DisarmEditor();
    _stage = Stage::Idle;
    _systemImage = nullptr;
}

bool PlusDAutostart::EditorIdle()
{
    EditorMonitor* monitor = _context->pDebugManager ? _context->pDebugManager->GetEditorMonitor() : nullptr;
    if (!monitor)
        return false;
    if (!_editorArmed)
    {
        // The monitor is the typer's; it is idle between the typed steps. Armed only for this wait
        monitor->ClearEvents();
        monitor->Arm();
        _editorArmed = true;
        return false;
    }
    for (const EditorMonitor::Event& e : monitor->Events())
    {
        if (e.point == ROMControlPoints::Point::EditorIdle || e.point == ROMControlPoints::Point::ReportShown)
            return true;
    }
    return false;
}

void PlusDAutostart::DisarmEditor()
{
    if (!_editorArmed)
        return;
    _editorArmed = false;
    EditorMonitor* monitor = _context->pDebugManager ? _context->pDebugManager->GetEditorMonitor() : nullptr;
    if (monitor)
    {
        monitor->Disarm();
        monitor->ClearEvents();
    }
}

bool PlusDAutostart::IsPending() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _stage != Stage::Idle && _stage != Stage::Done;
}

PlusDAutostart::Status PlusDAutostart::GetStatus() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _status;
}

void PlusDAutostart::Enter(Stage stage, const char* name)
{
    _stage = stage;
    _stageFrame = _context->emulatorState.frame_counter;
    _status.stage = name;
}

void PlusDAutostart::Complete(const char* step)
{
    _status.steps.push_back({step, _context->emulatorState.frame_counter - _armedFrame});
}

void PlusDAutostart::Finish(bool ok, const std::string& message)
{
    DisarmEditor();
    _stage = Stage::Done;
    _status.active = false;
    _status.finished = true;
    _status.ok = ok;
    _status.message = message;
    _systemImage = nullptr;
    if (_context->pModuleLogger)
    {
        if (ok)
            _context->pModuleLogger->Info(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC,
                                          "PlusDAutostart: %s", message.c_str());
        else
            _context->pModuleLogger->Warning(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC,
                                             "PlusDAutostart: %s", message.c_str());
    }
}

void PlusDAutostart::OnFrame()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_stage == Stage::Idle || _stage == Stage::Done)
        return;
    CommandTyper* typer = _context->pDebugManager ? _context->pDebugManager->GetCommandTyper() : nullptr;
    PlusDCard* card = Card();
    if (!typer || !card)
    {
        Finish(false, std::string(!typer ? "no command typer" : "the MGT card is gone") + " (" + _status.stage + ")");
        return;
    }
    const uint64_t now = _context->emulatorState.frame_counter;
    const bool late = now - _stageFrame > kStepTimeoutFrames;
    auto typed = [&](const char* what) -> int {
        // 1: taken, 0: still typing, -1: failed (finished)
        if (typer->GetStatus() == CommandTyper::Status::Running)
            return 0;
        const CommandTyper::Result result = typer->GetResult();
        if (CommandTyper::Succeeded(result))
            return 1;
        Finish(false, std::string(what) + " was not typed: " +
                          (result.message.empty() ? CommandTyper::FailureName(result.failure) : result.message));
        return -1;
    };

    switch (_stage)
    {
        case Stage::TypeRun:
        case Stage::TypeStart:
        {
            const bool run = _stage == Stage::TypeRun;
            CommandTyper::Options options;
            options.idleFrames = kStartIdleFrames;
            if (typer->Request(run ? "RUN" : _status.plan.command, options))
                Enter(run ? Stage::WaitRun : Stage::WaitStart, run ? "run" : "start");
            else if (late)
                Finish(false, std::string(run ? "RUN" : _status.plan.command) + " was not typed: other automation is typing");
            break;
        }
        case Stage::WaitRun:
        {
            const int state = typed("RUN");
            if (state <= 0)
                break;
            Complete("run");
            if (_status.plan.kind == "run")
                Finish(true, _status.plan.message);
            else
                Enter(Stage::WaitDos, "dos");
            break;
        }
        case Stage::WaitDos:
        {
            // The whole +SYS body was in the +D RAM (its first sector is there long before the last), then the +D paged
            // out: G+DOS initialised and handed back to BASIC. G+DOS rewrites the body's last byte as it starts, so the
            // full match is latched while it lasts; all but the last byte with the +D paged out counts too
            const std::vector<uint8_t>& body = _status.plan.dosBody;
            if (!body.empty() && std::memcmp(card->Ram(), body.data(), body.size()) == 0)
                _dosLoaded = true;
            if (!body.empty() && !card->IsPaged() &&
                (_dosLoaded || std::memcmp(card->Ram(), body.data(), body.size() - 1) == 0))
                _dosReady = true;
            // Then the ROM's editor waits for a key: the +D ROM's RUN has also looked for AUTO* (on the system disk;
            // a swap before that makes the ROM load the work's AUTO* by chance)
            if (_dosReady && EditorIdle())
            {
                DisarmEditor();
                Complete("dos");
                Enter(_status.plan.dos == "system-disk" ? Stage::Swap : Stage::TypeStart,
                      _status.plan.dos == "system-disk" ? "swap" : "start");
            }
            else if (late)
                Finish(false, "the DOS did not load in 10 s (dos)");
            break;
        }
        case Stage::Swap:
        {
            if (!_context->pMediaManager)
            {
                Finish(false, "no media manager (swap)");
                break;
            }
            MediaSource source;
            source.path = _workPath;
            InsertOptions options;
            options.disposition = Disposition::Discard;
            options.ttdReason = "plusd-autostart";
            const MediaResult inserted = _context->pMediaManager->Insert(card->DriveSlotId(0), source, options);
            if (!inserted.Ok())
                Finish(false, "the work's disk did not go back into drive 1: " + inserted.message);
            else
                Enter(Stage::WaitSwap, "swap");
            break;
        }
        case Stage::WaitSwap:
        {
            FDD* drive = card->Drive(0);
            DiskImage* image = drive ? drive->getDiskImage() : nullptr;
            if (image != nullptr && image != _systemImage)
            {
                Complete("swap");
                Enter(Stage::TypeStart, "start");
            }
            else if (late)
                Finish(false, "the work's disk was not in drive 1 after 10 s (swap)");
            break;
        }
        case Stage::WaitStart:
        {
            const int state = typed(_status.plan.command.c_str());
            if (state <= 0)
                break;
            Complete("start");
            Finish(true, _status.plan.message);
            break;
        }
        case Stage::Idle:
        case Stage::Done:
            break;
    }
}
