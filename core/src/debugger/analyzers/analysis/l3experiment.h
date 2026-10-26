#pragma once

/// @file l3experiment.h
/// @brief The experiment runner of L3 (docs/emulator/analysis/analysis-layers.md, "Experiments"): change the machine
/// (pokes, registers, input), run N frames, check a predicate over memory, the screen, watched addresses and the code
/// reached. It runs on analysis instances only (analysisinstance.h): one base holds the recording and is seeked to
/// each experiment's start frame, and every experiment gets the base's state copied into a free machine of its own
/// (MachineStateTransfer, the clone path) on a bounded pool of workers. The source instance is never touched, and two
/// experiments never share a machine. A result is observed (every check held) or refuted (one did not).
///
/// A spec (JSON):
/// @code
///   {"id": "e1", "finding": "F12", "claim": "the cheat code opens the cavern select",
///    "from": "start" | "end" | <frames after the first keyframe> | "frame:<n>",
///    "poke": {"$8100": 5}, "regs": {"a": "$10", "pc": "$8000"},
///    "input": [{"frame": 0, "key": "Q"}, {"frame": 10, "key": "Q", "up": true},
///              {"frame": 0, "joystick": "fire"}, {"frame": 20, "release_all": true}],
///    "frames": 50,
///    "watch": [{"pc": "$8123", "as": "rare"}, {"write": "$8100", "as": "w"}, {"read": "$8100", "as": "r"}],
///    "expect": {"rare": "reached", "w": ">=2", "mem:$8100": "!=5", "screen": "changed", "new_code": ">0"}}
/// @endcode
/// Watch checks: reached, not_reached, >=N, <=N, ==N (N alone: ==N). Memory: N, ==N, !=N, >=N, <=N, changed,
/// unchanged (against the value once the pokes are in). Screen ($4000-$5AFF): changed, unchanged. new_code
/// (instructions L1 never saw run): >0, 0, >=N.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "debugger/analyzers/analysis/l1model.h"
#include "emulator/state/statenode.h"

namespace analysis
{

struct TraceBundle;

struct ExperimentInput
{
    enum class Type : uint8_t
    {
        Key,
        Joystick,
        ReleaseAll,
    };
    uint32_t frame = 0;  ///< frames after the experiment's start, applied at that frame's start
    Type type = Type::Key;
    uint8_t key = 0;     ///< ZXKeysEnum
    uint8_t mask = 0;    ///< joystick bits
    bool press = true;
    std::string name;
};

struct ExperimentWatch
{
    std::string name;
    char what = 'X';      ///< X: an instruction at pc; W: a write; R: a read
    uint16_t address = 0;
};

struct ExperimentExpect
{
    enum class Subject : uint8_t
    {
        Watch,
        Memory,
        Screen,
        NewCode,
        Branch,  ///< coverage: the branch went the wanted way
    };
    enum class Op : uint8_t
    {
        Reached,
        NotReached,
        Equal,
        NotEqual,
        AtLeast,
        AtMost,
        Changed,
        Unchanged,
    };
    std::string key;   ///< as written ("rare", "mem:$8100")
    std::string text;  ///< the expectation as written
    Subject subject = Subject::Watch;
    Op op = Op::Reached;
    uint64_t value = 0;
    uint16_t address = 0;
    std::string watch;
};

/// A value held at an address before every instruction (the coverage driver: a variable pinned until a branch decided)
struct ExperimentPin
{
    uint16_t address = 0;
    uint8_t value = 0;
};

struct ExperimentSpec
{
    std::string id;
    std::string finding;  ///< the finding the result marks (empty: a new experiment finding)
    std::string claim;
    enum class From : uint8_t
    {
        Start,
        End,
        Frame,  ///< fromFrame frames after the first keyframe
    };
    From from = From::Start;
    uint64_t fromFrame = 0;
    std::vector<std::pair<uint16_t, uint8_t>> pokes;
    std::vector<std::pair<std::string, uint16_t>> regs;
    std::vector<ExperimentInput> input;
    uint32_t frames = 50;
    std::vector<ExperimentWatch> watch;
    std::vector<ExperimentExpect> expect;
    /// Coverage: pins held until the branch at branchPc ran once, and the way it should go
    std::vector<ExperimentPin> pins;
    bool branch = false;
    uint16_t branchPc = 0;
    uint8_t branchLength = 0;
    bool wantTaken = true;
    /// Coverage: keys held until the branch ran once (input sets them at frame 0)
    bool releaseInputWhenDecided = false;
};

/// Parse one spec object; false with @p error (the field and what it takes)
bool ParseExperiment(const StateNode& node, ExperimentSpec& spec, std::string& error);
/// One spec object or an array of them, as JSON text
bool ParseExperiments(const std::string& json, std::vector<ExperimentSpec>& specs, std::string& error);

struct ExperimentCheck
{
    std::string key;
    std::string expect;
    std::string actual;
    bool pass = false;
};

struct ExperimentResult
{
    std::string id;
    std::string finding;
    std::string claim;
    std::string verdict = "error";  ///< observed, refuted, error
    std::string error;
    uint64_t fromFrame = 0;         ///< the TTD frame it started from
    uint32_t framesRun = 0;
    std::vector<ExperimentCheck> checks;
    std::vector<std::pair<std::string, uint64_t>> hits;  ///< per watch
    std::vector<std::pair<std::string, uint16_t>> firstPc;  ///< per watch: the instruction of its first hit
    uint32_t newCode = 0;           ///< instructions L1 never saw run
    std::vector<std::pair<uint16_t, uint16_t>> newRanges;  ///< their Z80 runs (at most 16)
    std::vector<uint16_t> newPcs;   ///< their Z80 addresses, sorted (at most 4096)
    bool branchDecided = false;
    bool branchTaken = false;
    uint32_t worker = 0;
    uint64_t micros = 0;
};

struct ExperimentRunOptions
{
    uint32_t parallel = 0;  ///< workers (0: min(4, cores / 4), at least 1; at most 8)
    const L1Result* l1 = nullptr;  ///< new_code compares against it (null: every instruction counts as new)
    /// Test hook: called on the worker once the experiment's machine holds its state, before it runs
    std::function<void(size_t index)> onStarted;
};

struct ExperimentBatch
{
    std::vector<ExperimentResult> results;  ///< in the specs' order
    uint32_t workers = 0;
    uint32_t peakConcurrency = 0;           ///< experiments that ran at the same time, at most
    uint32_t seeks = 0;                     ///< base seeks (one per distinct start frame)
    uint64_t micros = 0;
    std::string error;                      ///< the base could not be made (every result is an error)
};

/// Run @p specs on analysis instances made from @p bundle; the calling thread waits for all of them
ExperimentBatch RunExperiments(const TraceBundle& bundle, const std::vector<ExperimentSpec>& specs,
                               const ExperimentRunOptions& options);

uint32_t DefaultExperimentWorkers();

StateNode ExperimentResultNode(const ExperimentResult& result);
/// "e1 observed: rare reached (3), mem:$8100 !=5 ($06); 2 new instructions at $8130-$8133"
std::string ExperimentResultLine(const ExperimentResult& result);

}  // namespace analysis
