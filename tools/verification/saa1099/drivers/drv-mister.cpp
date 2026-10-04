// drv-mister.cpp - the MiSTer SAM Coupe core's saa1099.sv (refs/sam-coupe-mister,
// Sorgelig, after Rodriguez Jodar's SAA1099.v and SAASound) as a Verilator model,
// one clk_sys posedge with ce = 1 per chip clock, internals read through
// --public-flat-rw.
//
// Bus writes go in on extra posedges with ce = 0 (the counters do not move), so a
// write lands between two chip clocks exactly like ours: one posedge with WR low,
// one more with the data still on the bus (the envelope block samples the
// registered write strobe a cycle later). Pipeline: a tone edge clocks the noise
// LFSR and the envelope one posedge later (compare.py allows +1 clock there).
//   drv-mister <stimulus.saa> <events.txt|->
#include "cosim.h"

#include "Vsaa1099.h"
#include "Vsaa1099___024root.h"
#include "verilated.h"

namespace
{
struct Ref
{
    Vsaa1099 top;

    Ref()
    {
        top.cs_n = 0;
        top.wr_n = 1;
        top.a0 = 0;
        top.din = 0;
        top.ce = 0;
        top.rst_n = 0;
        Edge();
        Edge();
        top.rst_n = 1;
        Edge();
    }
    void Edge()
    {
        top.clk_sys = 0;
        top.eval();
        top.clk_sys = 1;
        top.eval();
    }
    void Apply(const saacosim::Write& w)
    {
        top.ce = 0;
        top.a0 = w.address ? 1 : 0;
        top.din = w.value;
        top.wr_n = 0;
        Edge(); // register write
        top.wr_n = 1;
        Edge(); // registered strobe seen by the envelope blocks, data still on the bus
    }
    void Tick()
    {
        top.ce = 1;
        Edge();
        top.ce = 0;
    }
    void Read(saacosim::GenState& s)
    {
        auto* r = top.rootp;
        s.tone[0] = r->saa1099__DOT__top__DOT__freq_gen0__DOT__out;
        s.tone[1] = r->saa1099__DOT__top__DOT__freq_gen1__DOT__out;
        s.tone[2] = r->saa1099__DOT__top__DOT__freq_gen2__DOT__out;
        s.tone[3] = r->saa1099__DOT__bottom__DOT__freq_gen0__DOT__out;
        s.tone[4] = r->saa1099__DOT__bottom__DOT__freq_gen1__DOT__out;
        s.tone[5] = r->saa1099__DOT__bottom__DOT__freq_gen2__DOT__out;
        s.noiseRaw[0] = r->saa1099__DOT__top__DOT__noise_gen__DOT__lfsr;
        s.noiseRaw[1] = r->saa1099__DOT__bottom__DOT__noise_gen__DOT__lfsr;
        s.noiseBit[0] = s.noiseRaw[0] & 1;
        s.noiseBit[1] = s.noiseRaw[1] & 1;
        s.envOn[0] = r->saa1099__DOT__top__DOT__amp2__DOT__enable;
        s.envLeft[0] = r->saa1099__DOT__top__DOT__amp2__DOT__env_l;
        s.envRight[0] = r->saa1099__DOT__top__DOT__amp2__DOT__env_r;
        s.envOn[1] = r->saa1099__DOT__bottom__DOT__amp2__DOT__enable;
        s.envLeft[1] = r->saa1099__DOT__bottom__DOT__amp2__DOT__env_l;
        s.envRight[1] = r->saa1099__DOT__bottom__DOT__amp2__DOT__env_r;
        const uint32_t o0 = r->saa1099__DOT__out0;
        const uint32_t o1 = r->saa1099__DOT__out1;
        const bool enabled = (r->saa1099__DOT__ctrl & 1) != 0;
        // MiSTer units are 4x ours (vol << 5 per voice)
        s.outLeft = enabled ? static_cast<int32_t>(((o0 & 0x7FF) + (o1 & 0x7FF)) / 4) : 0;
        s.outRight = enabled ? static_cast<int32_t>((((o0 >> 11) & 0x7FF) + ((o1 >> 11) & 0x7FF)) / 4) : 0;
    }
};
} // namespace

int main(int argc, char** argv)
{
    Verilated::commandArgs(argc, argv);
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <stimulus.saa> <events.txt|->\n", argv[0]);
        return 2;
    }
    const saacosim::Stimulus st = saacosim::LoadStimulus(argv[1]);
    saacosim::EventLog log(argv[2]);
    Ref ref;
    saacosim::RunPerClock(ref, st, log);
    return 0;
}
