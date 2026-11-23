#pragma once

/// @file vdac4appruntime.h
/// @brief The boundary between the VDAC4 ESP firmware HLE (Vdac4EspFirmware) and an ESP app runtime.
///
/// On the board an app is a relocatable Xtensa ELF (ZM ABI 1, `modules/sdk/include/zm_app.h` of the VDAC4
/// firmware) the firmware's ModuleApp loads from its flash cache, calls `start(core)`, routes the Z80 commands of
/// its range to `command()` and calls `poll()` from the network core. The emulator does not run Xtensa code: a
/// runtime behind IVdac4AppRuntime stands for the app (a native reimplementation, a recorder, a test fake). The
/// firmware side - module delivery, cache, ELF check, start / stop rules, command routing, media range, replies,
/// events - stays the firmware's (vdac4espfirmware.cpp).
///
/// Threading: everything here runs on the emulation thread, inside Z80 port accesses (the SPI exchange that ends
/// a link turn) or Vdac4Esp::Advance. Calls never block and never reach another thread.
///   - Command() runs inside the turn whose request completed the frame; Reply() works only inside it (as
///     core->reply on the board).
///   - Poll() runs on every firmware pass (a pass follows every link turn and every Advance; Advance should be
///     called at least once per emulated millisecond quantum, so a runtime's output lags at most one quantum).
///   - SendFrame() may be called at any time on the emulation thread between Z80 accesses (from Poll, from a
///     timer of the runtime's own, from Command). Frames go into the ESP -> Z80 ring whole; without room they
///     wait in order (the firmware's blocking send) and are lost when the Z80 is quiet for 5 s or after 30 s, as
///     SpiLink::reserve fails. While frames wait, the firmware takes no new Z80 command.
///   - SendEvent() is core->event: at most 63 bytes, an 8-deep queue, one event per firmware pass.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// zm_result_t of the ZM ABI
enum Vdac4ZmResult : int
{
    kZmOk = 0,
    kZmError = -1,
    kZmNoMemory = -2,
    kZmNotFound = -3,
    kZmBusy = -4,
    kZmTimeout = -5
};

/// Tang (FPGA) registers over the ESP's LINK bus (tang::read / tang::write): TANG_READ / TANG_WRITE from the
/// Z80 and the FT812 detach guard read through it. Addresses are byte addresses of 32-bit registers (0..0xFC).
class IVdac4TangLink
{
public:
    virtual ~IVdac4TangLink() = default;
    /// false: the LINK does not answer (TANG_READ then answers [0])
    virtual bool LinkRead(uint32_t address, uint32_t& value) = 0;
    virtual bool LinkWrite(uint32_t address, uint32_t value) = 0;
    /// FT_ADDR = `ftAddress`, then FT_DATA = each of `count` words (little-endian bytes): ft_write in one call
    virtual bool LinkFtWrite(uint32_t ftAddress, const uint8_t* words, size_t count)
    {
        bool answered = LinkWrite(kLinkFtAddr, ftAddress);
        for (size_t i = 0; i < count; i++)
        {
            const uint8_t* p = words + 4 * i;
            answered = LinkWrite(kLinkFtData, static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                                  (static_cast<uint32_t>(p[2]) << 16) |
                                                  (static_cast<uint32_t>(p[3]) << 24)) &&
                       answered;
        }
        return answered;
    }

    static constexpr uint32_t kLinkFtAddr = 0x54;
    static constexpr uint32_t kLinkFtData = 0x58;
};

/// Frames to the Z80
class IVdac4FrameSink
{
public:
    virtual ~IVdac4FrameSink() = default;
    /// A frame `5A command length data xor` to the Z80, payload <= 1024 bytes. Any time (see the file comment).
    /// False: refused at once (payload too long, or the wait backlog is full)
    virtual bool SendFrame(uint8_t command, const uint8_t* data, size_t size) = 0;
    /// core->event: kZmOk, or kZmBusy when the payload is over 63 bytes or the 8-deep queue is full
    virtual int SendEvent(uint8_t code, const uint8_t* data, size_t size) = 0;
};

/// What the firmware gives a running app (the parts of zm_core_t an HLE runtime needs)
class IVdac4AppHost : public IVdac4FrameSink
{
public:
    /// core->reply: only inside Command(); kZmError outside it or when the send fails
    virtual int Reply(uint8_t response, const uint8_t* data, size_t size) = 0;
    /// core->link_media: raw media DMA for app commands first..last (inside the app's command range);
    /// first > last turns it off. kZmOk or kZmError
    virtual int SetMediaRange(uint8_t first, uint8_t last) = 0;
    /// core->app_done: inside Command() only; the app is stopped after its reply
    virtual void AppDone() = 0;
    /// core->millis
    virtual uint32_t NowMs() const = 0;
    /// core->resource: a resource the plugin delivered (MOD_QUERY flag 1). The pointer stays valid until the
    /// resource is replaced or released, or the app stops. kZmOk or kZmNotFound
    virtual int Resource(const char* name, const uint8_t*& data, size_t& size) = 0;
    /// core->resource_release (nullptr: all)
    virtual void ReleaseResource(const char* name) = 0;
    /// A cached module's bytes (module_open reads the ELF from the cache). kZmOk, kZmNotFound or kZmError
    virtual int ReadModule(const char* name, std::vector<uint8_t>& out) = 0;
    /// core->ft_open succeeded: arms the FT812 detach guard (`selected`: the FT812 personality was selected)
    virtual void FtOpened(bool selected) = 0;
    /// The Tang LINK, nullptr when none is attached
    virtual IVdac4TangLink* Tang() = 0;
    /// Events SendEvent takes now before it answers kZmBusy (the free places of the 8-deep queue)
    virtual size_t EventQueueRoom() const { return 8; }
};

/// What the running app declared (zm_app_t name, version, command_first, command_last)
struct Vdac4AppInfo
{
    std::string name;
    std::string version;
    uint8_t commandFirst = 1;  ///< first > last: no commands
    uint8_t commandLast = 0;
};

class IVdac4AppRuntime
{
public:
    virtual ~IVdac4AppRuntime() = default;
    /// Start the app of `module` ("PLAYER.ELF"); `elf` is the cached image the firmware re-checked. The host
    /// outlives the app. Fill `info` before calling host.SetMediaRange. kZmOk, or an error (the app is then
    /// not running and APP_START reports the result)
    virtual int StartApp(const std::string& module, const std::vector<uint8_t>& elf, IVdac4AppHost& host,
                         Vdac4AppInfo& info) = 0;
    /// app->stop and the rest of stopApp: no frames after this
    virtual void StopApp() = 0;
    /// app->command for a command of its range; kZmOk means handled (otherwise the Z80 gets "unsupported:XX")
    virtual int Command(uint8_t command, const uint8_t* data, size_t size) = 0;
    /// app->poll
    virtual void Poll(uint32_t nowMs) { (void)nowMs; }
    /// The module is loaded by the running app (module_open): committing a new copy restarts the app
    virtual bool UsesModule(const std::string& name) const
    {
        (void)name;
        return false;
    }
    /// The running app's state for Vdac4Esp::Save (bounded by the runtime); empty by default
    virtual void SaveState(std::vector<uint8_t>& out) const { out.clear(); }
    /// Vdac4Esp::Load of a state with this app running: restore it from SaveState without StartApp. False: it
    /// cannot be restored (the firmware then treats it as stopped)
    virtual bool LoadState(const std::string& module, const Vdac4AppInfo& info, IVdac4AppHost& host,
                           const std::vector<uint8_t>& state)
    {
        (void)module;
        (void)info;
        (void)host;
        (void)state;
        return false;
    }
};
