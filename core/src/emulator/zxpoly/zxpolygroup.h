#pragma once

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/io/keyboard/keyboard.h"

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

class Emulator;
class EmulatorContext;
class ZXPolyPortInterceptor;
struct ZXPSnapshot;

/// A ZX-Poly machine built from four stock emulator instances of one model
/// (docs/inprogress/2026-09-27-zxpoly/quad-instance-architecture.md,
/// model-agnostic-sync-layer.md).
///
/// Instance 0 is the master: it owns input, sound and the display. Instances
/// 1..3 are slaves. Two phases:
///
///  - Loader (#3D00 unlocked): only the master runs; the slaves are parked in
///    WAIT, as on the real platform after reset. A multiloader streams plane
///    data into them through the IO-mapped window; those writes are collected
///    per slave (overlay).
///  - Locked: set by the #3D00 write with the lock bit (with local reset), or
///    by loading a .zxp. At the lock the slaves become copies of the master
///    with their overlay on top and their own reset-command PC; from then on
///    all four run the same frames and differ only in graphics data.
///
/// Two ways to drive it:
///  - RunFrame(): the caller's thread steps all four, one frame at a time (the
///    "sequential" pipeline, the reference mode for tests);
///  - AttachToMaster(): the master runs as a normal instance (its own loop,
///    pacing, sound, the GUI adopts it); at the end of each of its frames the
///    group runs the slaves' frame and composes the picture into the master's
///    framebuffer. Host keys sent to the master are queued by the group.
class ZXPolyGroup : public Observer
{
public:
    static constexpr size_t MODULES = 4;

    /// What differs between the master and a slave at a frame boundary
    struct Divergence
    {
        bool diverged = false;
        size_t module = 0;
        std::string what;
    };

public:
    explicit ZXPolyGroup(std::string symbolicPrefix = "zxpoly");
    ~ZXPolyGroup();

    ZXPolyGroup(const ZXPolyGroup&) = delete;
    ZXPolyGroup& operator=(const ZXPolyGroup&) = delete;

    /// Creates the four instances of `model` (any creatable model name).
    /// Slave audio is muted; host key input is gated on every member
    bool Create(const std::string& model, std::string* error = nullptr);

    /// Removes the instances from the emulator manager
    void Destroy();

    bool IsCreated() const { return _instances[0] != nullptr; }
    Emulator* GetInstance(size_t module) const { return _instances[module].get(); }
    EmulatorContext* GetContext(size_t module) const;

    /// Loads a .zxp: module i into instance i, then applies #3D00 (video
    /// mode). The group is locked afterwards
    bool LoadZXP(const std::string& path, std::string* error = nullptr);

    /// Loads a ZX-Poly ROM image (.prom: up to four 16K parts, module i gets
    /// part i mod N as its only ROM) and powers the machine on: every CPU at
    /// #0000, #3D00 = 0 - the slaves wait until the ROM releases them
    bool LoadPROM(const std::string& path, std::string* error = nullptr);

    /// Mounts a disk image in drive A of every member and boots it on the
    /// master through TR-DOS (loader phase). The ZX-Poly multiloader on the
    /// disk then fills the slaves and locks the machine
    bool BootDisk(const std::string& path, std::string* error = nullptr);

    /// Copies the master's machine state into every slave: CPU registers,
    /// frame position, interrupt state, all RAM pages, paging latches, TR-DOS
    /// session. Plane data is applied on top by the caller afterwards. Locks
    /// the group
    void ReplicateFromMaster();

    /// Keyboard input for the whole machine. Queued and applied to all four
    /// instances at the next frame boundary, so every CPU sees the change at
    /// the same T-state
    void PressKey(ZXKeysEnum key);
    void ReleaseKey(ZXKeysEnum key);

    /// Runs one frame: in the loader phase the master alone (the locking #3D00
    /// write replicates at that instruction), in the locked phase all four
    void RunFrame();

    /// Live mode: hook the group into the master's own frame loop (see the
    /// class comment) and take host keys addressed to the master. Call before
    /// the master is started
    void AttachToMaster();
    void DetachFromMaster();
    std::shared_ptr<Emulator> GetMaster() const { return _instances[0]; }

    /// Live display frame at double resolution: the master's framebuffer
    /// (border included) scaled 2x with the composed 512 x 384 paper on top,
    /// so mode 5 shows its full resolution. Updated at every rendered master
    /// frame; the master's own framebuffer keeps a 1:1 picture for
    /// screenshots and recording
    unsigned GetDisplayWidth() const { return _displayWidth; }
    unsigned GetDisplayHeight() const { return _displayHeight; }
    const uint32_t* GetDisplayBuffer() const { return _displayFront.data(); }

    /// Copies the latest display frame (tear-free); false when dstSize does
    /// not match the current display size
    bool CopyDisplay(uint8_t* dst, size_t dstSize);
    void RunFrames(unsigned frames);

    bool IsLocked() const { return _locked; }

    /// Compares every slave with the master (locked phase only): port reads
    /// of the last frame (when the check is enabled), then the control state -
    /// PC, SP, I, IM, IFF1, HALT, T-state position, paging. Data registers are
    /// not compared: they legitimately hold plane-specific graphics bytes
    Divergence CheckLockstep() const;

    /// Debug aid: record every IN of every instance per frame and report the
    /// first read where a slave saw a different port, T-state or value than
    /// the master. Slows execution (installs the CPU bus trace hook)
    void EnablePortReadCheck(bool enable);

    /// Debug aid: record the PC of every executed instruction per frame and
    /// report the first instruction where a slave's control flow left the
    /// master's, with the master's preceding instructions
    void EnableInstructionTrace(bool enable);

    /// ZX-Poly video mode (#3D00 D2-D4)
    uint8_t GetVideoMode() const { return static_cast<uint8_t>((_port3D00 >> 2) & 0x07u); }
    void SetVideoMode(uint8_t mode);
    uint8_t GetPort3D00() const { return _port3D00; }

    /// Module register R0..R3 as last written
    uint8_t GetModuleRegister(size_t module, size_t reg) const { return _regs[module][reg]; }

    /// Number of bytes the loader wrote into a slave through the IO window
    size_t GetOverlayBytes(size_t module) const;

    /// The 6912-byte screen each module currently displays (#7FFD D3 selects
    /// page 5 or 7)
    const uint8_t* GetScreenMemory(size_t module) const;

    /// Composes the current picture (512 x 384, framebuffer pixel format)
    void Compose(std::vector<uint32_t>& out) const;

    /// Platform port access from a member's CPU (ZXPolyPortInterceptor).
    /// Return true when the access is consumed
    bool OnPortIn(size_t module, uint16_t port, uint8_t& value);
    bool OnPortOut(size_t module, uint16_t port, uint8_t value);

private:
    std::string _prefix;
    std::array<std::shared_ptr<Emulator>, MODULES> _instances{};
    std::array<std::unique_ptr<ZXPolyPortInterceptor>, MODULES> _interceptors{};

    // Platform state
    uint8_t _port3D00 = 0;
    std::array<std::array<uint8_t, 4>, MODULES> _regs{};
    bool _locked = false;
    bool _lockedThisFrame = false;   // the slaves start mid-frame, at the lock point
    uint64_t _lastMasterFrame = 0;   // a smaller frame counter means the master was reset
    bool _slavesRunning = false;     // unlocked with #3D00 D0 = 1: the slaves execute on their own
    bool _attached = false;

    // Loader phase: what the IO window wrote into each slave (-1 = untouched),
    // indexed by RAM page * 16K + offset
    std::array<std::vector<int16_t>, MODULES> _overlay;

    std::mutex _displayMutex;
    std::vector<uint32_t> _displayFront;
    std::vector<uint32_t> _displayBack;
    unsigned _displayWidth = 0;
    unsigned _displayHeight = 0;

    std::mutex _keysMutex;
    std::vector<std::pair<ZXKeysEnum, bool>> _pendingKeys;

    struct PortRead
    {
        uint32_t t;
        uint16_t port;
        uint16_t pc;
        uint8_t value;
    };
    bool _portReadCheck = false;
    std::array<std::vector<PortRead>, MODULES> _portReads;

    bool _instructionTrace = false;
    std::array<std::vector<uint16_t>, MODULES> _instructions;

private:
    void ResetPlatformState();
    void DetectMasterReset();
    void MountMasterDisksOnSlaves();
    void OnMainPortWrite(uint8_t value);
    void LocalReset(size_t module);
    void AlignSlaveClock(size_t module, unsigned extraT);
    void InstallMasterM1Hook();
    void CatchUpSlaves();
    void ResetSlaveMachines();
    uint8_t ModuleStatus(size_t module) const;
    uint8_t* ModuleRam(size_t module, uint16_t address) const;
    bool OnPort7FFDWrite(size_t module, uint8_t value);
    void ApplyPendingInput();
    void AdvanceSlaves();
    void RunSlaveToMasterPosition(size_t module);
    void OnMasterFrameEnd(bool rendered);
    void ComposeIntoMasterFramebuffer();
    void ComposeDisplayFrame();
    void ResizeDisplay(unsigned width, unsigned height);
    void OnHostKeyPressed(int id, Message* message);
    void OnHostKeyReleased(int id, Message* message);
    void QueueHostKey(Message* message, bool pressed);
    void PerformLock();
    uint16_t ResetCommandTarget(size_t module) const;
    size_t WindowOffset(size_t module, uint16_t address) const;
    bool IsTRDOSActive(size_t module) const;
    uint8_t ModuleIdentity(size_t module) const;
};
