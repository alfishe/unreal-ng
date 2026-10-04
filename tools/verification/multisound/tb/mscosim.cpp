// mscosim: ZX-MultiSound CPLD (cpld/rtl/top.v, Verilator) vs MultiSoundLogic co-simulation.
//
//   mscosim run <scenario.msc> [--dump] [--expect <out>]   play a scenario into both, report the first difference
//   mscosim sweep <tables.h>                               decode sweep + GS map sweep from the RTL -> core-tests tables
//   mscosim dac                                            DAC transfer (duty) of the RTL vs MultiSoundLogic helpers
//   mscosim gsint                                          GS INT period and width of the RTL
//
// Timing: one tick = half a 32 MHz period (1/64 us); clk32 rises on even ticks. Host bus cycles follow the Z80
// datasheet in half T-states at the scenario's cpu clock (default 3.5 MHz), GS cycles at 16 MHz. Every cycle starts on
// an even tick. See README.md.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "verilated.h"

#include "Vms_pro1m.h"
#include "Vms_pro1m___024root.h"
#include "Vms_pro2m.h"
#include "Vms_pro2m___024root.h"
#include "Vms_classic1m.h"
#include "Vms_classic1m___024root.h"
#include "Vms_classic2m.h"
#include "Vms_classic2m___024root.h"

#include "_helpers/multisoundscenario.h"

namespace
{
using Op = MultiSoundCycle::Op;

constexpr int IdleTicks = 16;

/// One Verilated variant of top.v driven by whole bus cycles.
class IRtlBus
{
public:
    virtual ~IRtlBus() = default;
    virtual MultiSoundCycleRecord Execute(const MultiSoundCycle& cycle, std::vector<std::string>& notes) = 0;
    virtual void SetDip(uint8_t bits) = 0;
    virtual void RunIdle(int ticks) = 0;
    virtual uint8_t DacOut(int channel) const = 0;
    virtual uint8_t GsInt() const = 0;
};

template <class Model>
class RtlBus final : public IRtlBus
{
public:
    explicit RtlBus(double cpuMHz) : _cpuMHz(cpuMHz)
    {
        _m = std::make_unique<Model>(_context.get());
        _m->cfg = 0x0F;
        _m->clkx = 0;
        _m->zxdos_n = 1;
        _m->zxiodos_n = 1;
        _m->gm1_n = 1;
        IdleBus();
        _m->rst_n = 0;
        RunIdle(4);
        _m->rst_n = 1;
        RunIdle(IdleTicks);
    }

    void SetDip(uint8_t bits) override { _m->cfg = bits & 0x0F; }

    void RunIdle(int ticks) override
    {
        for (int i = 0; i < ticks; i++)
            Tick();
    }

    uint8_t DacOut(int channel) const override
    {
        switch (channel)
        {
            case 0: return _m->dac0_out;
            case 1: return _m->dac1_out;
            case 2: return _m->dac2_out;
            default: return _m->dac3_out;
        }
    }

    uint8_t GsInt() const override { return _m->gint_n; }

    MultiSoundCycleRecord Execute(const MultiSoundCycle& cycle, std::vector<std::string>& notes) override
    {
        MultiSoundCycleRecord record;
        if (_tick & 1)
            Tick();     // every cycle starts on an even tick

        switch (cycle.op)
        {
            case Op::Reset:
                _m->rst_n = 0;
                RunIdle(4);
                _m->rst_n = 1;
                RunIdle(IdleTicks - 4);
                break;
            case Op::Dip:
                SetDip(cycle.value);
                RunIdle(IdleTicks);
                break;
            case Op::Par:
                RunCycle(cycle.hostOp, cycle.address, cycle.value, cycle.gsOp, cycle.gsAddress, cycle.gsValue,
                         cycle.gsOffset, record, notes);
                break;
            case Op::GsOut: case Op::GsIn: case Op::GsMemRead: case Op::GsMemWrite:
                RunCycle(Op::Reset, 0, 0, cycle.op, cycle.address, cycle.value, 0, record, notes);
                break;
            default:
                RunCycle(cycle.op, cycle.address, cycle.value, Op::Reset, 0, 0, 0, record, notes);
                break;
        }
        CaptureState(record);
        return record;
    }

private:
    struct HostSignals { uint8_t m1 = 1, mreq = 1, iorq = 1, rd = 1, wr = 1; bool drive = false; uint16_t address = 0; };
    struct GsSignals { uint8_t m1 = 1, mreq = 1, iorq = 1, rd = 1, wr = 1; bool drive = false; };

    static bool Between(int h, int from, int to) { return h >= from && h < to; }

    static HostSignals HostAt(Op op, int h, uint16_t address)
    {
        HostSignals s;
        s.address = address;
        switch (op)
        {
            case Op::M1:
                s.m1 = h < 4 ? 0 : 1;
                s.mreq = (Between(h, 1, 4) || Between(h, 5, 7)) ? 0 : 1;
                s.rd = Between(h, 1, 4) ? 0 : 1;
                if (h >= 4)
                    s.address = 0x0000;     // refresh address (I:R); M1 is high, nothing decodes it
                break;
            case Op::MemRead:
                s.mreq = s.rd = Between(h, 1, 5) ? 0 : 1;
                break;
            case Op::MemWrite:
                s.mreq = Between(h, 1, 5) ? 0 : 1;
                s.wr = Between(h, 3, 5) ? 0 : 1;
                s.drive = h >= 1;
                break;
            case Op::In:
                s.iorq = s.rd = Between(h, 2, 7) ? 0 : 1;
                break;
            case Op::Out:
                s.iorq = s.wr = Between(h, 2, 7) ? 0 : 1;
                s.drive = h >= 1;
                break;
            default:
                break;
        }
        return s;
    }

    static GsSignals GsAt(Op op, int h)
    {
        GsSignals s;
        switch (op)
        {
            case Op::GsMemRead:
                s.mreq = s.rd = Between(h, 1, 5) ? 0 : 1;
                s.drive = Between(h, 1, 5);     // the RAM / ROM drives gd
                break;
            case Op::GsMemWrite:
                s.mreq = Between(h, 1, 5) ? 0 : 1;
                s.wr = Between(h, 3, 5) ? 0 : 1;
                s.drive = h >= 1;
                break;
            case Op::GsIn:
                s.iorq = s.rd = Between(h, 2, 7) ? 0 : 1;
                break;
            case Op::GsOut:
                s.iorq = s.wr = Between(h, 2, 7) ? 0 : 1;
                s.drive = h >= 1;
                break;
            default:
                break;
        }
        return s;
    }

    void IdleBus()
    {
        _m->zxm1_n = _m->zxmreq_n = _m->zxiorq_n = _m->zxrd_n = _m->zxwr_n = 1;
        _m->gmreq_n = _m->giorq_n = _m->grd_n = _m->gwr_n = 1;
        _hostDrive = _gsDrive = false;
    }

    void Settle()
    {
        for (int pass = 0; pass < 4; pass++)
        {
            _m->eval();
            // YM2203 stand-in: drives ad when chip-selected and read
            uint8_t ad = _m->ad__en ? static_cast<uint8_t>(_m->ad__out | ~_m->ad__en) : 0xFF;
            if (!_m->ard_n && !_m->ym1_cs_n)
                ad = MultiSoundFakeYmValue(0, _m->aa0);
            else if (!_m->ard_n && !_m->ym2_cs_n)
                ad = MultiSoundFakeYmValue(1, _m->aa0);
            const uint8_t zxd = _hostDrive ? _hostData : (_m->zxd__en ? static_cast<uint8_t>(_m->zxd__out | ~_m->zxd__en) : 0xFF);
            const uint8_t gd = _gsDrive ? _gsData : (_m->gd__en ? static_cast<uint8_t>(_m->gd__out | ~_m->gd__en) : 0xFF);
            if (ad == _m->ad && zxd == _m->zxd && gd == _m->gd)
                return;
            _m->ad = ad;
            _m->zxd = zxd;
            _m->gd = gd;
        }
        _m->eval();
    }

    void Tick()
    {
        _m->clk32 = (_tick & 1) ? 0 : 1;
        Settle();
        _tick++;
    }

    void RunCycle(Op hostOp, uint16_t address, uint8_t value, Op gsOp, uint16_t gsAddress, uint8_t gsValue,
                  uint32_t gsOffset, MultiSoundCycleRecord& record, std::vector<std::string>& notes)
    {
        const bool host = hostOp != Op::Reset;
        const bool gs = gsOp != Op::Reset;
        const int hostHalves = host ? MultiSoundTiming::HalfStates(hostOp) : 0;
        const uint32_t hostTicks = host ? MultiSoundTiming::HostTick(hostHalves, _cpuMHz) : 0;
        const uint32_t gsTicks = gs ? gsOffset + MultiSoundTiming::GsTick(MultiSoundTiming::HalfStates(gsOp)) : 0;
        uint32_t total = hostTicks > gsTicks ? hostTicks : gsTicks;
        total = (total + 1) & ~1u;

        uint8_t prevAwr = 1, prevYm1 = 1, prevYm2 = 1, prevSaa = 1, prevAa0 = 0, prevAd = 0xFF;
        uint8_t prevZxdEn = 0, prevZxdOut = 0, prevGdEn = 0, prevGdOut = 0;
        uint8_t prevRd = 1, prevGrd = 1, prevSaaClk = _m->saa_clk;
        int overlap[2] = { 0, 0 };
        int saaEdges = 0;
        bool geSampled = false;
        bool geStable = true;

        for (uint32_t t = 0; t < total; t++)
        {
            // Inputs for this tick
            if (host)
            {
                int h = 0;
                while (h + 1 < hostHalves && MultiSoundTiming::HostTick(h + 1, _cpuMHz) <= t)
                    h++;
                const HostSignals s = t < hostTicks ? HostAt(hostOp, h, address) : HostAt(Op::Reset, 0, address);
                _m->zxa = s.address;
                _m->zxm1_n = s.m1;
                _m->zxmreq_n = s.mreq;
                _m->zxiorq_n = s.iorq;
                _m->zxrd_n = s.rd;
                _m->zxwr_n = s.wr;
                _hostDrive = s.drive;
                _hostData = value;
            }
            if (gs)
            {
                GsSignals s;
                if (t >= gsOffset && t < gsTicks)
                    s = GsAt(gsOp, static_cast<int>((t - gsOffset) / 2));
                _m->ga = gsAddress;
                _m->gm1_n = s.m1;
                _m->gmreq_n = s.mreq;
                _m->giorq_n = s.iorq;
                _m->grd_n = s.rd;
                _m->gwr_n = s.wr;
                _gsDrive = s.drive;
                _gsData = gsValue;
            }

            Tick();

            // Observations
            if (host && (hostOp == Op::In || hostOp == Op::Out) && !_m->zxiorq_n)
            {
                const uint8_t ge = _m->zxiorqge_n ? 0 : 1;
                if (!geSampled)
                {
                    record.iorqge = ge;
                    geSampled = true;
                }
                else if (ge != record.iorqge)
                    geStable = false;
            }
            for (int chip = 0; chip < 2; chip++)
            {
                const uint8_t cs = chip == 0 ? _m->ym1_cs_n : _m->ym2_cs_n;
                if (!cs && !_m->awr_n)
                    overlap[chip]++;
            }
            if (prevAwr == 0 && _m->awr_n == 1)
            {
                // /WR to the chips rose: whoever had CS low at the last tick latches
                if (!prevYm1 || !prevYm2)
                {
                    record.ymWrite = static_cast<uint8_t>((!prevYm1 ? 1 : 0) | (!prevYm2 ? 2 : 0));
                    record.ymA0 = prevAa0;
                    record.ymValue = prevAd;
                }
                if (!prevSaa)
                {
                    record.saaWrite = 1;
                    record.saaA0 = prevAa0;
                    record.saaValue = prevAd;
                }
            }
            if ((_tick & 1) == 1 && !_m->zxwr_n)     // the tick just evaluated was a posedge
            {
                auto* r = _m->rootp;
                if (r->zx_multisound__DOT__sd_dac0_cs) record.sdWrite |= 1;
                if (r->zx_multisound__DOT__sd_dac1_cs) record.sdWrite |= 2;
                if (r->zx_multisound__DOT__sd_dac2_cs) record.sdWrite |= 4;
                if (r->zx_multisound__DOT__sd_dac3_cs) record.sdWrite |= 8;
            }
            if (host && hostOp == Op::In && prevRd == 0 && _m->zxrd_n == 1)
            {
                if (prevZxdEn == 0xFF)
                {
                    record.readDriven = 1;
                    record.readValue = prevZxdOut;
                }
                else if (prevZxdEn != 0)
                    notes.push_back("zxd partly driven: en=" + std::to_string(prevZxdEn));
            }
            if (gs && gsOp == Op::GsIn && prevGrd == 0 && _m->grd_n == 1)
            {
                if (prevGdEn == 0xFF)
                {
                    record.readDriven = 1;
                    record.readValue = prevGdOut;
                }
            }
            if (gs && (gsOp == Op::GsMemRead || gsOp == Op::GsMemWrite) && t >= gsOffset && (t - gsOffset) == 6)
            {
                uint8_t chip = 7;
                if (!_m->grom_n) chip = 0;
                else if (!_m->gram1_n) chip = 1;
                else if (!_m->gram2_n) chip = 2;
                else if (!_m->gram3_n) chip = 3;
                else if (!_m->gram4_n) chip = 4;
                const int selects = (!_m->grom_n) + (!_m->gram1_n) + (!_m->gram2_n) + (!_m->gram3_n) + (!_m->gram4_n);
                if (selects > 1)
                    notes.push_back("several GS memory chips selected");
                record.gsMap = static_cast<uint8_t>((chip << 4) | (_m->gma & 0x0F));
            }
            if (t + 8 >= total && !prevSaaClk && _m->saa_clk)
                saaEdges++;

            prevAwr = _m->awr_n;
            prevYm1 = _m->ym1_cs_n;
            prevYm2 = _m->ym2_cs_n;
            prevSaa = _m->saa_cs_n;
            prevAa0 = _m->aa0;
            prevAd = _m->ad__en ? static_cast<uint8_t>(_m->ad__out | ~_m->ad__en) : 0xFF;
            prevZxdEn = _m->zxd__en;
            prevZxdOut = _m->zxd__out;
            prevGdEn = _m->gd__en;
            prevGdOut = _m->gd__out;
            prevRd = _m->zxrd_n;
            prevGrd = _m->grd_n;
            prevSaaClk = _m->saa_clk;
        }
        IdleBus();

        for (int chip = 0; chip < 2; chip++)
        {
            if (overlap[chip] && !(record.ymWrite & (1 << chip)))
            {
                notes.push_back("YM chip " + std::to_string(chip) + " CS+WR glitch of " + std::to_string(overlap[chip]) +
                                " ticks (" + std::to_string(overlap[chip] * 1000 / 64) + " ns) without a latching edge");
            }
        }
        if (!geStable)
            notes.push_back("IORQGE changed during the IORQ window");
        _lastSaaEdges = saaEdges;
    }

    void CaptureState(MultiSoundCycleRecord& record)
    {
        auto* r = _m->rootp;
        record.ymChip = r->zx_multisound__DOT__ym_chip_sel;
        record.ymReadStatus = r->zx_multisound__DOT__ym_get_stat;
        record.fmMuted = _m->fm1_ena ? 0 : 1;
        // The SAA clock as the chip sees it: activity on the saa_clk pin over the cycle's last 8 ticks (one 8 MHz
        // period). Directive lines run idle ticks only: use the enable there.
        record.saaClock = r->zx_multisound__DOT__saa_clk_en;
        if (_lastSaaEdges >= 0 && (_lastSaaEdges > 0) != (record.saaClock != 0))
            std::fprintf(stderr, "note: saa_clk activity (%d edges) disagrees with saa_clk_en\n", _lastSaaEdges);
        _lastSaaEdges = -1;
        record.romLock = r->zx_multisound__DOT__rom_m1_access;
        record.gsData = r->zx_multisound__DOT__gs_regdata;
        record.gsCommand = r->zx_multisound__DOT__gs_regcmd;
        record.gsPage = r->zx_multisound__DOT__gs_reg00;
        record.gsOutput = r->zx_multisound__DOT__gs_reg_out;
        record.dataFlag = r->zx_multisound__DOT__gs_flag_data;
        record.commandFlag = r->zx_multisound__DOT__gs_flag_cmd;
        record.dacSample = { r->zx_multisound__DOT__dac0, r->zx_multisound__DOT__dac1, r->zx_multisound__DOT__dac2,
                             r->zx_multisound__DOT__dac3 };
        record.dacVolume = { r->zx_multisound__DOT__vol0, r->zx_multisound__DOT__vol1, r->zx_multisound__DOT__vol2,
                             r->zx_multisound__DOT__vol3 };
        if (_m->fm1_ena != _m->fm2_ena)
            std::fprintf(stderr, "note: fm1_ena != fm2_ena\n");
    }

    std::unique_ptr<VerilatedContext> _context = std::make_unique<VerilatedContext>();
    std::unique_ptr<Model> _m;
    double _cpuMHz;
    uint64_t _tick = 0;
    bool _hostDrive = false;
    bool _gsDrive = false;
    uint8_t _hostData = 0xFF;
    uint8_t _gsData = 0xFF;
    int _lastSaaEdges = -1;
};

std::unique_ptr<IRtlBus> MakeRtl(const MultiSoundOptions& options, double cpuMHz)
{
    const bool classic = options.ctrlMask == MultiSoundCtrlMask::Classic;
    const bool twoMb = options.gsRam == MultiSoundGsRam::TwoMb;
    std::unique_ptr<IRtlBus> bus;
    if (classic)
        bus = twoMb ? std::unique_ptr<IRtlBus>(new RtlBus<Vms_classic2m>(cpuMHz)) : std::unique_ptr<IRtlBus>(new RtlBus<Vms_classic1m>(cpuMHz));
    else
        bus = twoMb ? std::unique_ptr<IRtlBus>(new RtlBus<Vms_pro2m>(cpuMHz)) : std::unique_ptr<IRtlBus>(new RtlBus<Vms_pro1m>(cpuMHz));
    bus->SetDip(MultiSoundDipBits(options));
    return bus;
}

bool ReadFile(const std::string& path, std::string& text)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

std::string FirstFieldDifference(const std::string& a, const std::string& b)
{
    std::stringstream sa(a), sb(b);
    std::string wa, wb;
    while (sa >> wa)
    {
        if (!(sb >> wb))
            return wa;
        if (wa != wb)
            return "rtl " + wa + " vs logic " + wb;
    }
    return "";
}

int Run(int argc, char** argv)
{
    if (argc < 3)
        return 2;
    const std::string path = argv[2];
    bool dump = false;
    std::string expectPath;
    for (int i = 3; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--dump") == 0)
            dump = true;
        else if (std::strcmp(argv[i], "--expect") == 0 && i + 1 < argc)
            expectPath = argv[++i];
    }

    std::string text;
    if (!ReadFile(path, text))
    {
        std::fprintf(stderr, "cannot read %s\n", path.c_str());
        return 2;
    }
    MultiSoundScenario scenario;
    std::string error;
    if (!ParseMultiSoundScenario(text, scenario, error))
    {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
        return 2;
    }

    std::unique_ptr<IRtlBus> rtl = MakeRtl(scenario.options, scenario.cpuMHz);
    MultiSoundLogicBus logic(scenario);
    std::string expected;
    int differences = 0;
    for (const MultiSoundCycle& cycle : scenario.cycles)
    {
        std::vector<std::string> notes;
        const MultiSoundCycleRecord rtlRecord = rtl->Execute(cycle, notes);
        const MultiSoundCycleRecord logicRecord = logic.Execute(cycle);
        const std::string cycleText = FormatMultiSoundCycle(cycle);
        const std::string rtlText = FormatMultiSoundRecord(rtlRecord);
        const std::string logicText = FormatMultiSoundRecord(logicRecord);
        expected += cycleText + " => " + rtlText + "\n";
        if (dump)
            std::printf("%4d %-36s => %s\n", cycle.sourceLine, cycleText.c_str(), rtlText.c_str());
        for (const std::string& note : notes)
            std::printf("     ; line %d: %s\n", cycle.sourceLine, note.c_str());
        if (rtlText != logicText && differences++ == 0)
        {
            std::printf("FIRST DIFFERENCE at %s:%d '%s': %s\n  rtl:   %s\n  logic: %s\n", path.c_str(), cycle.sourceLine,
                        cycleText.c_str(), FirstFieldDifference(rtlText, logicText).c_str(), rtlText.c_str(),
                        logicText.c_str());
        }
    }
    if (!expectPath.empty())
    {
        std::ofstream out(expectPath, std::ios::binary);
        out << "# RTL records of " << path.substr(path.find_last_of('/') + 1)
            << " (generated by tools/verification/multisound: mscosim run --expect; do not edit)\n"
            << expected;
    }
    std::printf("%s: %zu cycles, %d differing\n", path.c_str(), scenario.cycles.size(), differences);
    return differences ? 1 : 0;
}

struct SweepCounts
{
    uint64_t hash = MultiSoundHashSeed;
    uint32_t iorqge = 0, ymWrites = 0, saaWrites = 0, sdWrites = 0, drivenReads = 0;

    void Add(const MultiSoundCycleRecord& r)
    {
        hash = HashMultiSoundRecord(hash, r);
        iorqge += r.iorqge == 1;
        ymWrites += r.ymWrite != 0;
        saaWrites += r.saaWrite != 0;
        sdWrites += r.sdWrite != 0;
        drivenReads += r.readDriven != 0;
    }
};

int Sweep(int argc, char** argv)
{
    if (argc < 3)
        return 2;
    std::ofstream out(argv[2], std::ios::binary);
    out << "#pragma once\n\n"
           "// ZX-MultiSound decode sweep and GS memory map sweep, generated from the card's CPLD source (cpld/rtl/top.v at\n"
           "// d7f3ac2) in Verilator by tools/verification/multisound (mscosim sweep). Do not edit; regenerate.\n"
           "// Sequences: ForEachMultiSoundSweepCycle / ForEachMultiSoundGsMapCycle (_helpers/multisoundscenario.h).\n\n"
           "#include <cstdint>\n\n"
           "struct MultiSoundSweepRow\n{\n    uint8_t classic;\n    uint8_t dip;\n    uint64_t hash;\n"
           "    uint32_t iorqge, ymWrites, saaWrites, sdWrites, drivenReads;\n};\n\n"
           "struct MultiSoundWitnessRow\n{\n    uint16_t port;\n    uint8_t lock;\n    uint8_t write;\n    uint16_t code;\n};\n\n"
           "inline constexpr MultiSoundSweepRow MultiSoundSweepRows[] = {\n";

    std::vector<std::string> witness;
    int mismatches = 0;
    for (int classic = 0; classic < 2; classic++)
    {
        MultiSoundScenario header;
        header.options.ctrlMask = classic ? MultiSoundCtrlMask::Classic : MultiSoundCtrlMask::Pro;
        std::unique_ptr<IRtlBus> rtl = MakeRtl(header.options, header.cpuMHz);
        for (uint8_t dip = 0; dip < 16; dip++)
        {
            MultiSoundLogicBus logic(header);
            SweepCounts counts;
            uint8_t lock = 0;
            int reported = 0;
            ForEachMultiSoundSweepCycle(dip, [&](const MultiSoundCycle& cycle)
            {
                std::vector<std::string> notes;
                const MultiSoundCycleRecord r = rtl->Execute(cycle, notes);
                const MultiSoundCycleRecord l = logic.Execute(cycle);
                counts.Add(r);
                if (cycle.op == Op::M1)
                    lock = cycle.address < 0x4000 ? 1 : 0;
                if (r.Bytes() != l.Bytes())
                {
                    mismatches++;
                    if (reported++ < 3)
                    {
                        std::printf("MISMATCH classic=%d dip=%X %s\n  rtl:   %s\n  logic: %s\n", classic, dip,
                                    FormatMultiSoundCycle(cycle).c_str(), FormatMultiSoundRecord(r).c_str(),
                                    FormatMultiSoundRecord(l).c_str());
                    }
                }
                const uint16_t code = MultiSoundEventCode(r);
                if (!classic && dip == 0x0F && (cycle.op == Op::Out || cycle.op == Op::In) && code != 0 &&
                    IsMultiSoundWitnessPort(cycle.address))
                {
                    char line[64];
                    std::snprintf(line, sizeof(line), "{0x%04X,%u,%u,0x%03X}", cycle.address, lock,
                                  cycle.op == Op::Out ? 1u : 0u, code);
                    witness.push_back(line);
                }
            });
            char row[200];
            std::snprintf(row, sizeof(row), "    { %d, 0x%X, 0x%016llXull, %u, %u, %u, %u, %u },\n", classic, dip,
                          static_cast<unsigned long long>(counts.hash), counts.iorqge, counts.ymWrites, counts.saaWrites,
                          counts.sdWrites, counts.drivenReads);
            out << row;
            std::printf("classic=%d dip=%X hash=%016llX ge=%u ym=%u saa=%u sd=%u rd=%u\n", classic, dip,
                        static_cast<unsigned long long>(counts.hash), counts.iorqge, counts.ymWrites, counts.saaWrites,
                        counts.sdWrites, counts.drivenReads);
            std::fflush(stdout);
        }
    }
    out << "};\n\n// The all-enabled pro sweep's events (MultiSoundEventCode) at the witness ports (A12-A9 = 0)\n"
           "inline constexpr MultiSoundWitnessRow MultiSoundWitnessRows[] = {\n";
    for (size_t i = 0; i < witness.size(); i++)
        out << (i % 6 == 0 ? "    " : " ") << witness[i] << "," << (i % 6 == 5 || i + 1 == witness.size() ? "\n" : "");
    out << "};\n\n";

    for (int twoMb = 0; twoMb < 2; twoMb++)
    {
        MultiSoundScenario header;
        header.options.gsRam = twoMb ? MultiSoundGsRam::TwoMb : MultiSoundGsRam::OneMb;
        std::unique_ptr<IRtlBus> rtl = MakeRtl(header.options, header.cpuMHz);
        MultiSoundLogicBus logic(header);
        uint64_t hash = MultiSoundHashSeed;
        bool reported = false;
        ForEachMultiSoundGsMapCycle([&](const MultiSoundCycle& cycle)
        {
            std::vector<std::string> notes;
            const MultiSoundCycleRecord r = rtl->Execute(cycle, notes);
            const MultiSoundCycleRecord l = logic.Execute(cycle);
            hash = HashMultiSoundRecord(hash, r);
            for (const std::string& note : notes)
                std::printf("note: %s\n", note.c_str());
            if (r.Bytes() != l.Bytes())
            {
                mismatches++;
                if (!reported)
                {
                    std::printf("MISMATCH gsmap %s %s\n  rtl:   %s\n  logic: %s\n", twoMb ? "2M" : "1M",
                                FormatMultiSoundCycle(cycle).c_str(), FormatMultiSoundRecord(r).c_str(),
                                FormatMultiSoundRecord(l).c_str());
                    reported = true;
                }
            }
        });
        char line[120];
        std::snprintf(line, sizeof(line), "inline constexpr uint64_t MultiSoundGsMapHash%s = 0x%016llXull;\n",
                      twoMb ? "2Mb" : "1Mb", static_cast<unsigned long long>(hash));
        out << line;
        std::printf("gsmap %s hash=%016llX\n", twoMb ? "2M" : "1M", static_cast<unsigned long long>(hash));
    }
    std::printf("witness rows: %zu, logic mismatches: %d\n", witness.size(), mismatches);
    return mismatches ? 1 : 0;
}

int Dac()
{
    // Mean of dac0_out over whole 32 MHz periods vs 0.5 + 0.5 * level / 128 * gain / 64
    static constexpr uint8_t samples[] = { 0x00, 0x01, 0x40, 0x7E, 0x7F, 0x80, 0x81, 0xC0, 0xFE, 0xFF };
    static constexpr uint8_t volumes[] = { 0, 1, 16, 32, 62, 63 };
    double worst = 0.0;
    std::printf("sample vol  measured  expected\n");
    for (uint8_t volume : volumes)
    {
        for (uint8_t sample : samples)
        {
            MultiSoundScenario header;
            std::unique_ptr<IRtlBus> rtl = MakeRtl(header.options, header.cpuMHz);
            std::vector<std::string> notes;
            MultiSoundCycle cycle;
            cycle.op = Op::Out;
            cycle.address = 0x000F;
            cycle.value = sample;
            rtl->Execute(cycle, notes);
            if (volume != 63)
            {
                cycle.op = Op::GsOut;
                cycle.address = 0x0006;
                cycle.value = volume;
                rtl->Execute(cycle, notes);
            }
            rtl->RunIdle(256);
            const int periods = 128 * 64 * 8;
            long high = 0;
            for (int i = 0; i < periods * 2; i++)
            {
                rtl->RunIdle(1);
                high += rtl->DacOut(0);
            }
            const double measured = static_cast<double>(high) / (periods * 2);
            const double expected = 0.5 + 0.5 * MultiSoundLogic::SampleLevel(MultiSoundLogic::ConvertSample(sample)) / 128.0 *
                                              MultiSoundLogic::VolumeGain64(volume) / 64.0;
            const double error = measured > expected ? measured - expected : expected - measured;
            worst = error > worst ? error : worst;
            std::printf("  %02X    %2u   %.5f   %.5f\n", sample, volume, measured, expected);
        }
    }
    std::printf("worst |measured - expected| = %.6f\n", worst);
    return worst < 0.001 ? 0 : 1;
}

int GsInt()
{
    MultiSoundScenario header;
    std::unique_ptr<IRtlBus> rtl = MakeRtl(header.options, header.cpuMHz);
    std::vector<uint64_t> falls, rises;
    uint8_t previous = rtl->GsInt();
    for (uint64_t tick = 0; tick < 64 * 200; tick++)
    {
        rtl->RunIdle(1);
        const uint8_t now = rtl->GsInt();
        if (previous && !now) falls.push_back(tick);
        if (!previous && now) rises.push_back(tick);
        previous = now;
    }
    for (size_t i = 1; i < falls.size() && i < 6; i++)
    {
        const uint64_t period = falls[i] - falls[i - 1];
        std::printf("GS INT period %llu ticks = %.3f clk32 = %.2f Hz\n", static_cast<unsigned long long>(period),
                    period / 2.0, 64e6 / static_cast<double>(period));
    }
    for (size_t i = 0; i < rises.size() && i < falls.size() && i < 3; i++)
    {
        if (rises[i] > falls[i])
            std::printf("GS INT low %llu ticks = %.1f ns\n", static_cast<unsigned long long>(rises[i] - falls[i]),
                        (rises[i] - falls[i]) * 1000.0 / 64.0);
    }
    return 0;
}
}

// Verilator's runtime asks for the simulation time in legacy (non-context) calls
double sc_time_stamp() { return 0.0; }

int main(int argc, char** argv)
{
    Verilated::commandArgs(1, argv);
    if (argc >= 2 && std::strcmp(argv[1], "run") == 0)
        return Run(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "sweep") == 0)
        return Sweep(argc, argv);
    if (argc >= 2 && std::strcmp(argv[1], "dac") == 0)
        return Dac();
    if (argc >= 2 && std::strcmp(argv[1], "gsint") == 0)
        return GsInt();
    std::fprintf(stderr, "usage: mscosim run <scenario.msc> [--dump] [--expect <out>] | sweep <tables.h> | dac | gsint\n");
    return 2;
}
