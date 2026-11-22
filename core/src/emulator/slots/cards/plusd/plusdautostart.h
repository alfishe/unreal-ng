#pragma once

/// @file plusdautostart.h
/// @brief The MGT disk autostart, +D and DISCiPLE (docs/inprogress/2026-11-15-mgt-plus-d/autostart.md §3): what to
/// start is chosen from the disk's G+DOS directory, then a user's steps are replayed on the emulation thread.
///
/// Most MGT disks carry no DOS: it was already in the card's RAM. The system file is `+SYS*` on the +D (G+DOS),
/// `SYS*` on the DISCiPLE (GDOS 3: its ROM's RUN looks for that name). So the plan is one of
///  - `run`: the disk has the system file and `AUTO*`: reset, `RUN` (the ROM loads both);
///  - `run+load`: the system file, no `AUTO*`: reset, `RUN`, wait for the DOS, the start command;
///  - `system-disk+load`: no system file: the emulator's system disk (boot/plusd/gdos-2a-system.mgt,
///    boot/disciple/gdos-3d-system.mgt) in drive 1, reset, `RUN`, wait for the DOS, the work's disk back in drive 1,
///    the start command;
///  - `mount`: nothing to start (only CODE, or a 128K snapshot on a 48K machine): the disk stays mounted.
/// The start command, first match: an `AUTO*` program or snapshot; a BASIC loader (`loader`, `boot`, `menu`,
/// `start`); the first BASIC program; the first snapshot (`S` 48K, `K` 128K) - typed as `LOAD d1"<name>"...`.
///
/// Every step is a condition with a timeout of 10 s emulated, not a delay: typing goes through the CommandTyper (it
/// waits for the editor), "the DOS is loaded" is the whole `+SYS` body seen at the +D RAM's #2000 (G+DOS then rewrites its last byte) and the +D
/// paged out, then the ROM editor waiting for a key (the editor monitor), "the disk is back" is the
/// work's image in the drive. OnFrame() runs at the frame end (MainLoop), after the typer.

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class EmulatorContext;
class PlusDCard;
namespace unrealdisk { class DiskImage; }

class PlusDAutostart
{
public:
    struct Plan
    {
        std::string kind;            ///< run, run+load, system-disk+load, mount
        std::string dos;             ///< disk (the work's +SYS), system-disk, "" (mount)
        std::string command;         ///< the start command typed after the DOS ("" for run / mount)
        std::string startFile;       ///< the file it loads
        std::vector<uint8_t> dosBody;  ///< the +SYS body the DOS check compares with the +D RAM at #2000
        std::string message;         ///< the outcome in words (HUD / log / the answer)
        std::string basic;           ///< "48" / "128": the BASIC of the reset (set by the caller, not MakePlan)

        bool Starts() const { return kind != "mount" && !kind.empty(); }
    };

    struct Step
    {
        std::string name;    ///< reset, run, dos, swap, start
        uint64_t frame = 0;  ///< frames since the reset when it completed
    };

    /// What a caller reads back while and after the steps run
    struct Status
    {
        bool active = false;   ///< steps still to run
        bool finished = false; ///< ran to the end (started or failed)
        bool ok = false;       ///< the start command (or RUN) was taken
        Plan plan;
        std::string stage;     ///< the step running or the last one
        std::string message;   ///< failure reason (the step and why) or the outcome
        std::vector<Step> steps;
    };

    /// Where the system disk is looked for: next to the executable, then the resources
    static constexpr const char* kSystemDiskRelative = "boot/plusd/gdos-2a-system.mgt";
    static constexpr const char* kDiscipleSystemDiskRelative = "boot/disciple/gdos-3d-system.mgt";
    static std::string FindSystemDisk(bool disciple = false);
    /// The system file of the card's DOS: `+SYS*` (G+DOS), `SYS*` (GDOS 3); CODE, any case
    static bool IsSystemFile(const std::string& name, uint8_t type, bool disciple);

    explicit PlusDAutostart(EmulatorContext* context) : _context(context) {}

    /// The plan for the work's disk (its G+DOS directory). Nothing changes. `is48K`: a 128K snapshot cannot start
    static Plan MakePlan(unrealdisk::DiskImage& work, bool is48K, const std::string& systemDiskPath,
                         bool disciple = false);

    /// Start the steps after the reset (the CPU held): `workPath` goes back into drive 1 for system-disk+load
    void Arm(const Plan& plan, const std::string& workPath);
    void Disarm();
    bool IsPending() const;
    Status GetStatus() const;

    /// One frame of the steps (emulation thread, frame end)
    void OnFrame();

private:
    enum class Stage : uint8_t
    {
        Idle,
        TypeRun,
        WaitRun,
        WaitDos,
        Swap,
        WaitSwap,
        TypeStart,
        WaitStart,
        Done,
    };

    static constexpr uint64_t kStepTimeoutFrames = 500;  ///< 10 s emulated
    static constexpr uint16_t kStartIdleFrames = 500;    ///< the editor may print the DOS's report first

    void Enter(Stage stage, const char* name);
    void Complete(const char* step);
    void Finish(bool ok, const std::string& message);
    PlusDCard* Card() const;
    bool EditorIdle();    ///< the ROM editor waits for a key (arms the editor monitor on the first call)
    void DisarmEditor();

    EmulatorContext* _context = nullptr;
    mutable std::mutex _mutex;  ///< _status (read by automation threads)
    Status _status;
    Stage _stage = Stage::Idle;
    std::string _workPath;
    uint64_t _armedFrame = 0;
    uint64_t _stageFrame = 0;
    bool _dosLoaded = false;  ///< the whole +SYS body was seen in the +D RAM
    bool _dosReady = false;   ///< ... and the +D paged out since
    bool _editorArmed = false;  ///< the editor monitor is armed by this wait
    unrealdisk::DiskImage* _systemImage = nullptr;  ///< the system disk's image while it is in drive 1
};
