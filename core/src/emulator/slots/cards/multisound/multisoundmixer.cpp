#include "multisoundmixer.h"

#include <algorithm>
#include <cmath>

#include "emulator/slots/cards/multisound/multisounddacs.h"

using namespace MultiSoundBoard;

static_assert(MultiSoundMixer::kDacChannelFullScale == MultiSoundDacs::kChannelFullScale, "DAC units disagree");

namespace
{

double Parallel(double a, double b)
{
    return a * b / (a + b);
}

int16_t ToSample(double level)
{
    return static_cast<int16_t>(std::clamp<long>(std::lround(level * 32767.0), -32768L, 32767L));
}

// The resistance each coupling capacitor drives (schematic findings: one capacitor per source pin)
const double kFmCouplingOhms = Parallel(kFmInputOhms, kFmInputOhms);                       // C10 -> R18 + R24
const double kSsgCenterCouplingOhms = Parallel(kSsgCenterInputOhms, kSsgCenterInputOhms);  // C8 -> R16 + R27
const double kSaaCouplingOhms = kSaaSourceOhms + kSaaSeriesOhms + kSaaInputOhms;            // C2 after the ladder
const double kDacCouplingOhms = kDacSeriesOhms + kDacInputOhms;                             // C3 -> R6
} // namespace

void MultiSoundMixer::Configure(const MultiSoundMixerConfig& cfg)
{
    _cfg = cfg;
    _cfg.outputRate = std::max<uint32_t>(_cfg.outputRate, 1);
    DesignFilters();
}

double MultiSoundMixer::FmFullScale(double trimDb)
{
    return kFmBaseGain * std::pow(10.0, trimDb / 20.0);
}

void MultiSoundMixer::SetFmTrimDb(double db)
{
    _trim.fmDb = db;
    _fmLevel = FmFullScale(db);
}

void MultiSoundMixer::Reset()
{
    DesignFilters();
}

void MultiSoundMixer::SetOutputRate(uint32_t rate)
{
    _cfg.outputRate = std::max<uint32_t>(rate, 1);
    DesignFilters();
}

void MultiSoundMixer::SetRenderMode(MultiSoundRenderMode mode)
{
    _cfg.renderMode = mode;
    DesignFilters();
}

void MultiSoundMixer::DesignFilters()
{
    const double fs = _cfg.outputRate;
    for (size_t chip = 0; chip < 2; chip++)
    {
        _couplingFm[chip] = MultiSoundRcFilter::HighPass1(CouplingCornerHz(kFmCouplingOhms), fs);
        _couplingSsgCenter[chip] = MultiSoundRcFilter::HighPass1(CouplingCornerHz(kSsgCenterCouplingOhms), fs);
        for (size_t side = 0; side < 2; side++)
            _couplingSsgSide[chip][side] = MultiSoundRcFilter::HighPass1(CouplingCornerHz(kSsgSideInputOhms), fs);
    }
    for (size_t side = 0; side < 2; side++)
    {
        _couplingSaa[side] = MultiSoundRcFilter::HighPass1(CouplingCornerHz(kSaaCouplingOhms), fs);
        _couplingMidi[side] = MultiSoundRcFilter::HighPass1(CouplingCornerHz(kMidiInputOhms), fs);
        _couplingDac[side] = MultiSoundRcFilter::HighPass1(CouplingCornerHz(kDacCouplingOhms), fs);

        double b[3];
        double a[3];
        SaaLadder(b, a);
        _saaLadder[side].Design(b, a, SaaCornerHz(), fs);
    }
}

namespace
{
/// `count` values of a row all +0.0 (a missing row reads 0)
template <typename T>
bool RowSilent(const T* row, size_t count)
{
    if (!row)
        return true;
    for (size_t i = 0; i < count; i++)
    {
        if (row[i] != 0 || std::signbit(static_cast<double>(row[i])))
            return false;
    }
    return true;
}
} // namespace

void MultiSoundMixer::Mix(const MultiSoundMixerInput& in, const MultiSoundMixerOutput& out)
{
    const bool coupled = _cfg.acCoupling;
    const bool authentic = _cfg.renderMode == MultiSoundRenderMode::Authentic;
    auto couple = [coupled](MultiSoundRcFilter& filter, double x) { return coupled ? filter.Process(x) : x; };
    // A stream whose input rows are all zeros with every filter on its path at rest: each sample would be +0.0 through
    // filters that stay at rest - its output rows are zeros. The streams do not share state, so each runs on its own
    const bool skip = _skipSilent;
    auto atRest = [coupled, skip](const MultiSoundRcFilter& filter) { return skip && (!coupled || filter.AtRest()); };
    const size_t frames = in.frames;
    auto zeros = [frames](int16_t* row) { std::fill_n(row, frames * 2, int16_t(0)); };

    // FM per chip: centred, weight 1.0, each through its own coupling capacitor
    for (size_t chip = 0; chip < 2; chip++)
    {
        if (!out.fm[chip])
            continue;
        if (atRest(_couplingFm[chip]) && RowSilent(in.fm[chip], frames))
        {
            zeros(out.fm[chip]);
            continue;
        }
        for (size_t i = 0; i < frames; i++)
        {
            const double fm = in.fm[chip] ? in.fm[chip][i] : 0.0f;
            const double level = couple(_couplingFm[chip], fm * _fmLevel) * kWeightFm;
            out.fm[chip][i * 2] = ToSample(level);
            out.fm[chip][i * 2 + 1] = ToSample(level);
        }
    }

    // SSG per chip: A -> L, B -> both, C -> R
    for (size_t chip = 0; chip < 2; chip++)
    {
        if (!out.ssg[chip])
            continue;
        if (atRest(_couplingSsgSide[chip][0]) && atRest(_couplingSsgCenter[chip]) && atRest(_couplingSsgSide[chip][1]) &&
            RowSilent(in.ssg[chip][0], frames) && RowSilent(in.ssg[chip][1], frames) && RowSilent(in.ssg[chip][2], frames))
        {
            zeros(out.ssg[chip]);
            continue;
        }
        for (size_t i = 0; i < frames; i++)
        {
            double channel[3] = {};
            for (int c = 0; c < 3; c++)
                channel[c] = in.ssg[chip][c] ? in.ssg[chip][c][i] : 0.0f;
            const double a = couple(_couplingSsgSide[chip][0], channel[0] * kSsgChannelFullScale);
            const double b = couple(_couplingSsgCenter[chip], channel[1] * kSsgChannelFullScale);
            const double c = couple(_couplingSsgSide[chip][1], channel[2] * kSsgChannelFullScale);
            out.ssg[chip][i * 2] = ToSample(a * kWeightSsgSide + b * kWeightSsgCenter);
            out.ssg[chip][i * 2 + 1] = ToSample(c * kWeightSsgSide + b * kWeightSsgCenter);
        }
    }

    // SAA: each side through its own ladder (Authentic) and coupling capacitor
    if (out.saa)
    {
        if (atRest(_couplingSaa[0]) && atRest(_couplingSaa[1]) &&
            (!authentic || (_saaLadder[0].AtRest() && _saaLadder[1].AtRest())) &&
            RowSilent(in.saa, frames * 2))
            zeros(out.saa);
        else
            for (size_t i = 0; i < frames; i++)
            {
                for (size_t side = 0; side < 2; side++)
                {
                    double v = in.saa ? in.saa[i * 2 + side] * kSaaUnit : 0.0;
                    if (authentic)
                        v = _saaLadder[side].Process(v);
                    out.saa[i * 2 + side] = ToSample(couple(_couplingSaa[side], v) * kWeightSaa);
                }
            }
    }

    if (out.midi)
    {
        if (atRest(_couplingMidi[0]) && atRest(_couplingMidi[1]) && RowSilent(in.midi, frames * 2))
            zeros(out.midi);
        else
            for (size_t i = 0; i < frames; i++)
            {
                for (size_t side = 0; side < 2; side++)
                {
                    const double v = in.midi ? in.midi[i * 2 + side] * kMidiFullScale : 0.0;
                    out.midi[i * 2 + side] = ToSample(couple(_couplingMidi[side], v) * kWeightMidi);
                }
            }
    }

    if (out.dac)
    {
        if (atRest(_couplingDac[0]) && atRest(_couplingDac[1]) && RowSilent(in.dac, frames * 2))
            zeros(out.dac);
        else
            for (size_t i = 0; i < frames; i++)
            {
                for (size_t side = 0; side < 2; side++)
                {
                    const double v = in.dac ? in.dac[i * 2 + side] * kDacUnit : 0.0;
                    out.dac[i * 2 + side] = ToSample(couple(_couplingDac[side], v) * kWeightDac);
                }
            }
    }

    // External input: DC-coupled (no capacitor)
    if (out.external)
    {
        for (size_t i = 0; i < frames; i++)
        {
            for (size_t side = 0; side < 2; side++)
            {
                const double v = in.external ? in.external[i * 2 + side] * kLevelPerVolt : 0.0;
                out.external[i * 2 + side] = ToSample(v * kWeightExternal);
            }
        }
    }
}
