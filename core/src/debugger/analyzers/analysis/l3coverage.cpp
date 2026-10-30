#include "debugger/analyzers/analysis/l3coverage.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "debugger/analyzers/analysis/l3experiment.h"
#include "debugger/analyzers/analysis/replaytracer.h"
#include "emulator/io/keyboard/keyboard.h"

namespace analysis
{

namespace
{

std::string Hex4(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value & 0xFFFF);
    return text;
}

std::string Hex2(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%02X", value & 0xFF);
    return text;
}

const char* CondName(uint8_t cond)
{
    static const char* const kNames[] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M", "DJNZ"};
    return cond < 9 ? kNames[cond] : "?";
}

/// One way to try: a pin, or an input held from the start
struct Attempt
{
    std::string how;
    bool pin = false;
    uint16_t address = 0;
    uint8_t value = 0;
    bool key = false;
    std::string name;  ///< key / joystick name for the spec
};

/// The 40 keys of the matrix, in half-row and bit order
std::vector<std::pair<std::string, ZXKeysEnum>> KeysOfRows(uint8_t highByte)
{
    static const std::vector<std::pair<std::string, ZXKeysEnum>> keys = {
        {"caps", ZXKEY_CAPS_SHIFT}, {"z", ZXKEY_Z}, {"x", ZXKEY_X}, {"c", ZXKEY_C}, {"v", ZXKEY_V},
        {"a", ZXKEY_A}, {"s", ZXKEY_S}, {"d", ZXKEY_D}, {"f", ZXKEY_F}, {"g", ZXKEY_G},
        {"q", ZXKEY_Q}, {"w", ZXKEY_W}, {"e", ZXKEY_E}, {"r", ZXKEY_R}, {"t", ZXKEY_T},
        {"1", ZXKEY_1}, {"2", ZXKEY_2}, {"3", ZXKEY_3}, {"4", ZXKEY_4}, {"5", ZXKEY_5},
        {"0", ZXKEY_0}, {"9", ZXKEY_9}, {"8", ZXKEY_8}, {"7", ZXKEY_7}, {"6", ZXKEY_6},
        {"p", ZXKEY_P}, {"o", ZXKEY_O}, {"i", ZXKEY_I}, {"u", ZXKEY_U}, {"y", ZXKEY_Y},
        {"enter", ZXKEY_ENTER}, {"l", ZXKEY_L}, {"k", ZXKEY_K}, {"j", ZXKEY_J}, {"h", ZXKEY_H},
        {"space", ZXKEY_SPACE}, {"sym", ZXKEY_SYM_SHIFT}, {"m", ZXKEY_M}, {"n", ZXKEY_N}, {"b", ZXKEY_B},
    };
    std::vector<std::pair<std::string, ZXKeysEnum>> out;
    for (const auto& [name, key] : keys)
    {
        uint8_t row = 0, bit = 0;
        if (Keyboard::MatrixPosition(key, row, bit) && (highByte & (1u << row)) == 0)
            out.emplace_back(name, key);
    }
    return out;
}

bool InRanges(const std::vector<ExportRange>& ranges, uint16_t z80)
{
    if (ranges.empty())
        return z80 >= 0x4000;
    for (const ExportRange& r : ranges)
        if (z80 >= r.start && z80 <= r.end)
            return true;
    return false;
}

}  // namespace

StateNode CoverBranchNode(const CoverBranch& b)
{
    StateNode node = StateNode::Object();
    node["pc"] = Hex4(b.z80);
    node["cond"] = CondName(b.cond);
    node["want"] = b.wantTaken ? "taken" : "not_taken";
    node["tested"] = b.tested;
    node["address"] = Hex4(b.address);
    node["status"] = b.status;
    node["reason"] = b.reason;
    node["how"] = b.how;
    node["attempts"] = static_cast<uint64_t>(b.attempts);
    node["from_frame"] = b.fromFrame;
    node["new_code"] = static_cast<uint64_t>(b.newCode);
    StateNode ranges = StateNode::Array();
    for (const auto& [lo, hi] : b.newRanges)
        ranges.push(Hex4(lo) + "-" + Hex4(hi));
    node["new_ranges"] = ranges;
    return node;
}

std::string CoverBranchLine(const CoverBranch& b)
{
    std::string line = Hex4(b.z80) + " " + CondName(b.cond) + (b.wantTaken ? " never taken" : " always taken") + " (" +
                       b.tested + (b.tested == "memory" || b.tested == "port" ? " " + Hex4(b.address) : "") + "): ";
    if (b.status == "flipped")
    {
        line += "flipped by " + b.how + " (attempt " + std::to_string(b.attempts) + ")";
        if (b.newCode != 0)
        {
            line += ", " + std::to_string(b.newCode) + " new instructions at";
            for (size_t i = 0; i < b.newRanges.size() && i < 4; ++i)
                line += " " + Hex4(b.newRanges[i].first) + "-" + Hex4(b.newRanges[i].second);
        }
    }
    else if (b.status == "skipped")
        line += "skipped, " + b.reason;
    else
        line += b.status + " after " + std::to_string(b.attempts) + " attempts";
    return line;
}

CoverResult RunCoverage(const TraceBundle& bundle, const L1Result& l1, const CoverOptions& options)
{
    CoverResult out;
    std::vector<std::vector<Attempt>> plans;
    std::vector<uint8_t> lengths;
    for (const BranchCondition& b : l1.branches)
    {
        if (b.cond == 8 || (b.taken == 0) == (b.notTaken == 0) || !InRanges(options.ranges, b.z80))
            continue;
        if (out.branches.size() >= options.limit)
            break;
        ++out.candidates;
        CoverBranch branch;
        branch.z80 = b.z80;
        branch.cond = b.cond;
        branch.wantTaken = b.taken == 0;
        branch.tested = SourceKindName(b.tested);
        branch.address = b.address;
        const Instruction* ins = l1.InstructionAt(b.instruction);
        // The last frame the branch ran in (the instruction's own bytes have no data uses to date it)
        branch.fromFrame = b.lastFrame;
        std::vector<Attempt> plan;
        if (ins == nullptr)
            branch.reason = "the instruction is not in L1";
        else if (b.varies)
            branch.reason = "the flags come from more than one producer";
        else if (b.tested == SourceKind::Memory)
        {
            if (b.address < 0x4000)
                branch.reason = "it tests a ROM byte";
            else
            {
                std::vector<uint8_t> values = {b.constant, static_cast<uint8_t>(b.constant + 1),
                                               static_cast<uint8_t>(b.constant - 1), 0x00, 0x01, 0xFF, 0x80, 0x02};
                std::set<uint8_t> used;
                for (const uint8_t v : values)
                    if (used.insert(v).second)
                    {
                        Attempt a;
                        a.pin = true;
                        a.address = b.address;
                        a.value = v;
                        a.how = "poke " + Hex4(b.address) + "=" + Hex2(v) + " (held)";
                        plan.push_back(a);
                    }
            }
        }
        else if (b.tested == SourceKind::Port && (b.address & 0xFF) == 0xFE)
        {
            for (const auto& [name, key] : KeysOfRows(static_cast<uint8_t>(b.address >> 8)))
            {
                (void)key;
                Attempt a;
                a.key = true;
                a.name = name;
                a.how = "key " + name;
                plan.push_back(a);
            }
            if (plan.empty())
                branch.reason = "port " + Hex4(b.address) + " selects no half-row";
        }
        else if (b.tested == SourceKind::Port && (b.address & 0xFF) == 0x1F)
        {
            for (const char* name : {"right", "left", "down", "up", "fire"})
            {
                Attempt a;
                a.key = false;
                a.name = name;
                a.how = std::string("joystick ") + name;
                plan.push_back(a);
            }
        }
        else if (b.tested == SourceKind::Port)
            branch.reason = "port " + Hex4(b.address) + " is no keyboard or joystick";
        else
            branch.reason = std::string("it tests ") + (b.tested == SourceKind::None ? "nothing L1 traced"
                                                        : b.tested == SourceKind::Constant ? "a computed value"
                                                                                           : "a register");
        if (plan.size() > options.perBranch)
            plan.resize(options.perBranch);
        branch.status = plan.empty() ? "skipped" : "untried";
        if (!plan.empty())
            ++out.tried;
        out.branches.push_back(branch);
        plans.push_back(std::move(plan));
        lengths.push_back(ins != nullptr ? ins->length : 0);
    }

    std::set<uint16_t> fresh;
    uint32_t budget = options.budget;
    for (uint32_t round = 0; round < options.perBranch && budget > 0; ++round)
    {
        std::vector<ExperimentSpec> specs;
        std::vector<size_t> owners;
        for (size_t i = 0; i < out.branches.size() && budget > 0; ++i)
        {
            CoverBranch& branch = out.branches[i];
            if (branch.status == "flipped" || round >= plans[i].size())
                continue;
            const Attempt& a = plans[i][round];
            ExperimentSpec spec;
            spec.id = "cover:" + Hex4(branch.z80) + ":" + std::to_string(round + 1);
            spec.from = ExperimentSpec::From::Frame;
            spec.fromFrame = branch.fromFrame;
            spec.frames = options.frames;
            spec.branch = true;
            spec.branchPc = branch.z80;
            spec.branchLength = lengths[i];
            spec.wantTaken = branch.wantTaken;
            if (a.pin)
                spec.pins.push_back(ExperimentPin{a.address, a.value});
            else
            {
                StateNode event = StateNode::Object();
                event["frame"] = 0;
                event[a.key ? "key" : "joystick"] = a.name;
                StateNode node = StateNode::Object();
                StateNode input = StateNode::Array();
                input.push(event);
                node["input"] = input;
                StateNode expect = StateNode::Object();
                expect["branch"] = "flipped";
                node["expect"] = expect;
                ExperimentSpec parsed;
                std::string error;
                if (!ParseExperiment(node, parsed, error))
                    continue;
                spec.input = parsed.input;
                spec.releaseInputWhenDecided = true;
            }
            ExperimentExpect flipped;
            flipped.key = "branch";
            flipped.text = "flipped";
            flipped.subject = ExperimentExpect::Subject::Branch;
            spec.expect.push_back(flipped);
            specs.push_back(std::move(spec));
            owners.push_back(i);
            ++branch.attempts;
            --budget;
        }
        if (specs.empty())
            break;
        ExperimentRunOptions run;
        run.parallel = options.parallel;
        run.l1 = &l1;
        const ExperimentBatch batch = RunExperiments(bundle, specs, run);
        out.attempts += static_cast<uint32_t>(specs.size());
        out.workers = std::max(out.workers, batch.workers);
        out.peakConcurrency = std::max(out.peakConcurrency, batch.peakConcurrency);
        out.micros += batch.micros;
        if (!batch.error.empty())
        {
            out.error = batch.error;
            break;
        }
        for (size_t k = 0; k < batch.results.size(); ++k)
        {
            const ExperimentResult& r = batch.results[k];
            CoverBranch& branch = out.branches[owners[k]];
            if (r.verdict == "observed")
            {
                branch.status = "flipped";
                branch.how = plans[owners[k]][round].how;
                branch.newCode = r.newCode;
                branch.newRanges = r.newRanges;
                fresh.insert(r.newPcs.begin(), r.newPcs.end());
            }
            else if (r.branchDecided)
                branch.status = "not_flipped";
            else if (branch.status != "not_flipped")
                branch.status = "not_reached";
        }
    }
    for (CoverBranch& branch : out.branches)
        if (branch.status == "flipped")
            ++out.flipped;
    out.newCode = static_cast<uint32_t>(fresh.size());
    return out;
}

}  // namespace analysis
