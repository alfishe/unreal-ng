#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "debugger/analyzers/ianalyzer.h"
#include "debugger/breakpoints/breakpointmanager.h"

class EmulatorContext;

/// SectionTimerAnalyzer: the time of code sections between marker addresses, the machine never stopped
/// (breakpoints-and-traces.md, "Section timing"; POC 050). Exec taps on the markers (an entry address and one or more
/// exit addresses per section), the CPU's int_accept / int_return events for the interrupts: a pass's gross time is
/// exit minus entry, its net time the gross minus the interrupts that ran inside it (acknowledge to the return's end,
/// contention and waits included). Time is global: frame * frame length + T in the frame, so a pass may cross frames.
///
/// The state machine: an entry pushes a pass; an entry of an open section drops it and the passes above it (it left
/// without its exit), unless the sections are recursive; an exit closes the innermost open pass of its section and
/// drops the passes above it; an exit with no open pass is an orphan once the section was entered. A break condition
/// stops the machine at the exit of the pass that meets it, in a run that honours breakpoints (run_frames does not:
/// nothing is stopped or recorded there). The results are read at any time (a lock per marker hit). Every change goes
/// out as NC_SECTION_TIMER: started, stopped, cleared, and break at a stop
class SectionTimerAnalyzer : public IAnalyzer, public IBreakpointTapSink
{
public:
    static constexpr const char* kId = "section-timer";
    static constexpr size_t kDefaultCapacity = 100000;
    static constexpr size_t kMaxCapacity = 10000000;

    struct Section
    {
        std::string name;
        uint16_t entry = 0;
        std::vector<uint16_t> exits;
    };
    enum class Op : uint8_t
    {
        Greater,
        Less,
        GreaterEqual,
        LessEqual,
        Equal,
        NotEqual
    };
    /// A pass of `section` whose gross (or net) time meets `op value` stops the machine at its exit
    struct BreakIf
    {
        uint16_t section = 0;
        Op op = Op::Greater;
        uint64_t value = 0;
        bool net = false;
        std::string text;  ///< as given, for the reports
    };
    struct Config
    {
        std::vector<Section> sections;
        std::vector<uint16_t> extra;  ///< taps with no action
        std::vector<BreakIf> breakIf;
        bool recursive = false;       ///< an entry of an open section nests a pass instead of dropping the open one
        uint32_t frameT = 0;          ///< T-states a frame for the global clock; 0: the machine's
        size_t capacity = kDefaultCapacity;  ///< passes kept one by one; the statistics count them all
    };
    /// The text form every automation plane takes (labels from the loaded symbols, or numbers: #8000, $8000,
    /// 0x8000, 8000h, 32768)
    struct Spec
    {
        /// "NAME" (the labels NAME and NAME.End) or "name=ENTRY:EXIT[,EXIT...]"; none: every label X with an X.End
        std::vector<std::string> sections;
        std::vector<std::string> extra;
        /// "[net:]NAME<op>T", op one of > < >= <= == !=
        std::vector<std::string> breakIf;
        bool recursive = false;
        uint32_t frameT = 0;
        size_t capacity = kDefaultCapacity;
    };
    struct Pass
    {
        uint16_t section = 0;
        uint64_t frame = 0;
        uint32_t t = 0;
        uint64_t elapsed = 0;
        uint64_t net = 0;
        uint32_t interrupts = 0;
    };
    struct Stats
    {
        uint64_t count = 0;
        uint64_t min = 0;
        uint64_t max = 0;
        uint64_t sum = 0;
        uint64_t netMin = 0;
        uint64_t netMax = 0;
        uint64_t netSum = 0;
        uint64_t interrupted = 0;  ///< passes an interrupt ran inside
        uint64_t last = 0;
        uint64_t maxFrame = 0;     ///< where the longest pass started
        uint32_t maxT = 0;
    };
    /// The pass a break condition stopped the machine at (only a stop that took place)
    struct BreakStop
    {
        bool hit = false;
        Pass pass;
        std::string condition;
    };
    /// A copy of the results, taken under the lock
    struct Result
    {
        bool active = false;
        uint32_t frameT = 0;
        std::vector<Section> sections;
        std::vector<Stats> stats;
        std::vector<Pass> passes;  ///< [offset, offset + limit) of the kept ones
        uint64_t passesTotal = 0;
        uint64_t passesKept = 0;
        uint64_t hits = 0;
        uint64_t orphans = 0;
        uint64_t dropped = 0;
        uint64_t open = 0;  ///< passes entered and not closed yet
        BreakStop stop;
    };

    explicit SectionTimerAnalyzer(EmulatorContext* context);
    ~SectionTimerAnalyzer() override;

    std::string getName() const override { return "SectionTimerAnalyzer"; }
    std::string getUUID() const override { return "section-timer"; }
    void onActivate(AnalyzerManager* manager) override;
    void onDeactivate() override;

    /// The machine's timer (its debug manager's analyzer), null without one
    static SectionTimerAnalyzer* Find(EmulatorContext* context);

    /// What every plane does: resolve the spec, stop a running timer, configure, activate (false with the reason;
    /// a timer running before keeps running when the spec does not resolve)
    bool Start(const Spec& spec, std::string& error);
    /// Disarm the taps; the results stay readable. False when it was not running
    bool Stop();
    /// Drop the results (and the open passes); a running timer goes on measuring
    void Clear();
    bool IsActive() const;

    /// Resolve a text spec to a config: false with the reason (an unknown label, no section, a bad condition)
    bool Resolve(const Spec& spec, Config& config, std::string& error) const;
    /// Set while deactivated (activate arms the taps); clears the results
    void configure(const Config& config);
    const Config& config() const { return _config; }

    /// The results now: the statistics, the kept passes from `offset` (at most `limit`), the counters
    Result GetResult(size_t offset = 0, size_t limit = SIZE_MAX) const;
    /// GetResult as JSON: {"active", "frame_t", "sections": [{name, entry, exits, count, min, max, avg, net_min,
    /// net_max, net_avg, interrupted, last, max_frame, max_t}], "passes": [[name, frame, t, elapsed, net, interrupts]],
    /// "passes_total", "passes_kept", "hits", "orphan", "dropped", "open", "stopped": {...} or null}
    static std::string ToJson(const Result& result);
    /// A short table for a terminal or an agent: a line per section, then the counters and the stop
    static std::string ToText(const Result& result);
    /// The POC's form: ToJson of every kept pass
    std::string resultJson() const { return ToJson(GetResult()); }

    void OnTap(const BreakpointTapHit& hit) override;

private:
    static constexpr uint32_t kCookieAccept = 0xFFFFFFF0u;
    static constexpr uint32_t kCookieReturn = 0xFFFFFFF1u;

    /// An address's actions in order: exits first, so an address that ends one section and opens the next closes
    /// the old pass first. Entry: section + 1, exit: -(section + 1)
    struct Marker
    {
        uint16_t address = 0;
        std::vector<int32_t> actions;
    };
    struct Open
    {
        uint16_t section = 0;
        uint64_t serial = 0;
        uint64_t start = 0;
        uint64_t frame = 0;
        uint32_t t = 0;
        uint32_t interrupts = 0;
        uint64_t interruptT = 0;
    };
    struct Pending
    {
        uint64_t start = 0;
        std::vector<uint64_t> inside;  ///< the serials of the passes open at the accept
    };

    bool ResolveAddress(const std::string& text, uint16_t& address, std::string& error) const;
    uint64_t Now() const;
    void Entry(uint16_t section);
    void Exit(uint16_t section);
    void Accept(uint32_t acknowledgeT);
    void Return();
    void Reset();
    /// Posts NC_SECTION_TIMER (`stop` for "break"); never under _lock
    void Publish(const char* event, const BreakStop* stop = nullptr) const;

    EmulatorContext* _context = nullptr;
    BreakpointManager* _breakpoints = nullptr;
    uint8_t _sink = BRK_NO_TAP;
    bool _active = false;

    Config _config;
    uint32_t _frameT = 0;
    std::vector<Marker> _markers;
    std::vector<Open> _open;
    std::vector<Pending> _pending;
    std::vector<bool> _entered;
    uint64_t _serial = 0;

    // The results: written on the emulation thread under the lock, read by any thread
    mutable std::mutex _lock;
    std::vector<Pass> _passes;
    std::vector<Stats> _stats;
    uint64_t _passesTotal = 0;
    uint64_t _hits = 0;
    uint64_t _orphans = 0;
    uint64_t _dropped = 0;
    BreakStop _stop;
    bool _stopPending = false;  ///< Exit stopped the machine: OnTap publishes _stop once out of the lock
};
