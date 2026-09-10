#include "stdafx.h"
#include "pch.h"

#include "common/sound/filters/fir_designer.h"
#include "common/sound/filters/filter_decimator.h"
#include "common/sound/filters/filter_unreal.h"

/// FIR designer validation (audio-multirate plan, phase 2).
///
/// The designer must reproduce both shipped hand-baked (MATLAB-era) tables
/// bit-exactly - this is what authorizes replacing every baked table with
/// runtime design. The same math was validated in Python
/// (docs/inprogress/2026-08-17-audio-sync/generate_fir_tables.py).

/// Golden reference: the historically shipped MATLAB-designed decimator table
/// (96-tap lowpass, Fc=20kHz @ Fs=218.75kHz, Kaiser beta=5), verbatim from the
/// pre-multirate filter_decimator.h. The runtime designer must reproduce it.
static constexpr double SHIPPED_DECIMATOR_COEFFS[96] = {
     2.052603086353451149e-04,  3.210251422678139137e-04,  3.437220785102240109e-04,  2.109182111510678919e-04,
    -8.813007722059908102e-05, -4.870812384487524906e-04, -8.458617388491266806e-04, -9.899287167638743268e-04,
    -7.779213535741368686e-04, -1.752127463883414616e-04,  6.977284471930135209e-04,  1.570219993981382964e-03,
     2.091556053222377084e-03,  1.953639329633998986e-03,  1.027983115324567712e-03, -5.359199887113940504e-04,
    -2.300145079851742807e-03, -3.640391776539313954e-03, -3.945303735737179344e-03, -2.855862974611067008e-03,
    -4.599512339460455341e-04,  2.642683488913957334e-03,  5.456686481103634884e-03,  6.880934400135874096e-03,
     6.096971819493468385e-03,  2.929874713795616644e-03, -1.957650331064132720e-03, -7.140749659778805429e-03,
    -1.079993258728825789e-02, -1.130107842741509723e-02, -7.818418433736594106e-03, -7.870218846449325567e-04,
     8.004251012786215216e-03,  1.577440165148706955e-02,  1.952116805334440375e-02,  1.701080481789962739e-02,
     7.695582406293094944e-03, -6.749873188107499179e-03, -2.250139455088128945e-02, -3.433154299241062551e-02,
    -3.689163058992080135e-02, -2.621237878612778932e-02, -1.007910885111672206e-03,  3.661206987612355968e-02,
     8.127633587913619950e-02,  1.253609490622179801e-01,  1.606540571497764303e-01,  1.802624694847022313e-01,
     1.802624694847022313e-01,  1.606540571497764303e-01,  1.253609490622179801e-01,  8.127633587913619950e-02,
     3.661206987612355968e-02, -1.007910885111672206e-03, -2.621237878612778932e-02, -3.689163058992080135e-02,
    -3.433154299241062551e-02, -2.250139455088128945e-02, -6.749873188107499179e-03,  7.695582406293094944e-03,
     1.701080481789962739e-02,  1.952116805334440375e-02,  1.577440165148706955e-02,  8.004251012786215216e-03,
    -7.870218846449325567e-04, -7.818418433736594106e-03, -1.130107842741509723e-02, -1.079993258728825789e-02,
    -7.140749659778805429e-03, -1.957650331064132720e-03,  2.929874713795616644e-03,  6.096971819493468385e-03,
     6.880934400135874096e-03,  5.456686481103634884e-03,  2.642683488913957334e-03, -4.599512339460455341e-04,
    -2.855862974611067008e-03, -3.945303735737179344e-03, -3.640391776539313954e-03, -2.300145079851742807e-03,
    -5.359199887113940504e-04,  1.027983115324567712e-03,  1.953639329633998986e-03,  2.091556053222377084e-03,
     1.570219993981382964e-03,  6.977284471930135209e-04, -1.752127463883414616e-04, -7.779213535741368686e-04,
    -9.899287167638743268e-04, -8.458617388491266806e-04, -4.870812384487524906e-04, -8.813007722059908102e-05,
     2.109182111510678919e-04,  3.437220785102240109e-04,  3.210251422678139137e-04,  2.052603086353451149e-04
};

TEST(FirDesigner_Test, ReproducesShippedDecimatorTable)
{
    // FilterDecimator: 96-tap Kaiser beta=5, Fc=20kHz @ Fs=218.75kHz
    auto h = FirDesigner::kaiser(96, 20000.0, 218750.0, 5.0);
    ASSERT_EQ(h.size(), 96u);

    for (size_t i = 0; i < 96; i++)
    {
        EXPECT_NEAR(h[i], SHIPPED_DECIMATOR_COEFFS[i], 1e-15)
            << "Designer coefficient " << i << " diverges from shipped table";
    }

    // The default-constructed decimator (44100, Reference) must carry the
    // exact shipped design - this authorizes the baked table's removal
    FilterDecimator dec;
    ASSERT_EQ(dec.taps(), 96u);
    EXPECT_DOUBLE_EQ(dec.samplesPerOutput(), 218750.0 / 44100.0);
    for (size_t i = 0; i < 96; i++)
    {
        EXPECT_NEAR(dec.coefficients()[i], SHIPPED_DECIMATOR_COEFFS[i], 1e-15)
            << "Live decimator coefficient " << i << " diverges from shipped table";
    }
}

TEST(FirDesigner_Test, DecimatorRateMatrix)
{
    // Every supported rate: correct phase step, unity DC gain, tier/BW rules
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    {
        for (auto q : {FilterDecimator::Quality::Reference, FilterDecimator::Quality::HighFidelity})
        {
            for (bool ext : {false, true})
            {
                FilterDecimator dec;
                dec.configure(rate, q, ext);

                EXPECT_DOUBLE_EQ(dec.samplesPerOutput(), 218750.0 / rate) << "rate " << rate;
                EXPECT_EQ(dec.taps(), q == FilterDecimator::Quality::HighFidelity ? 192u : 96u);

                double dc = 0.0;
                for (double c : dec.coefficients())
                    dc += c;
                EXPECT_NEAR(dc, 1.0, 1e-12) << "rate " << rate << " ext " << ext;
            }
        }
    }

    // Extended bandwidth only opens up where the rate allows it: identical
    // coefficients to default at 44.1/48k, different at >=88.2k
    FilterDecimator base, ext48, ext96;
    base.configure(96000.0);
    ext48.configure(48000.0, FilterDecimator::Quality::Reference, true);
    ext96.configure(96000.0, FilterDecimator::Quality::Reference, true);

    FilterDecimator def48;
    def48.configure(48000.0);
    EXPECT_EQ(ext48.coefficients(), def48.coefficients()) << "extended BW must be a no-op at 48k";
    EXPECT_NE(ext96.coefficients(), base.coefficients()) << "extended BW must widen passband at 96k";
}

TEST(FirDesigner_Test, DecimatorDCPassthroughAtEveryRate)
{
    // Steady DC input must emerge at unity through feed/get at every rate/tier
    for (double rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        for (auto q : {FilterDecimator::Quality::Reference, FilterDecimator::Quality::HighFidelity})
        {
            FilterDecimator dec;
            dec.configure(rate, q);

            double last = 0.0;
            int outputs = 0;
            for (int i = 0; i < 4096; i++)
            {
                dec.feedSample(0.5);
                while (dec.hasOutput())
                {
                    last = dec.getOutput();
                    outputs++;
                }
            }
            ASSERT_GT(outputs, 0);
            EXPECT_NEAR(last, 0.5, 1e-9) << "rate " << rate;

            // Output count must track the exact decimation ratio
            const double expected = 4096.0 / dec.samplesPerOutput();
            EXPECT_NEAR(static_cast<double>(outputs), expected, 1.0) << "rate " << rate;
        }
    }
}

/// Golden reference: the historically shipped MATLAB fdatool table
/// (128-tap Hamming, order 127, fs = 44100 x 64 = 2822400, fc = 11025),
/// verbatim from the pre-multirate filter_unreal.h (first half; the table is
/// symmetric, checked explicitly below).
static constexpr double SHIPPED_UNREAL_COEFFS_HEAD[64] = {
    0.000797243121022152, 0.000815206499600866, 0.000844792477531490, 0.000886460636664257,
    0.000940630171246217, 0.001007677515787512, 0.001087934129054332, 0.001181684445143001,
    0.001289164001921830, 0.001410557756409498, 0.001545998595893740, 0.001695566052785407,
    0.001859285230354019, 0.002037125945605404, 0.002229002094643918, 0.002434771244914945,
    0.002654234457752337, 0.002887136343664226, 0.003133165351783907, 0.003391954293894633,
    0.003663081102412781, 0.003946069820687711, 0.004240391822953223, 0.004545467260249598,
    0.004860666727631453, 0.005185313146989532, 0.005518683858848785, 0.005860012915564928,
    0.006208493567431684, 0.006563280932335042, 0.006923494838753613, 0.007288222831108771,
    0.007656523325719262, 0.008027428904915214, 0.008399949736219575, 0.008773077102914008,
    0.009145787031773989, 0.009517044003286715, 0.009885804729257883, 0.010251021982371376,
    0.010611648461991030, 0.010966640680287394, 0.011314962852635887, 0.011655590776166550,
    0.011987515680350414, 0.012309748033583185, 0.012621321289873522, 0.012921295559959939,
    0.013208761191466523, 0.013482842243062109, 0.013742699838008606, 0.013987535382970279,
    0.014216593638504731, 0.014429165628265581, 0.014624591374614174, 0.014802262449059521,
    0.014961624326719471, 0.015102178534818147, 0.015223484586101132, 0.015325161688957322,
    0.015406890226980602, 0.015468413001680802, 0.015509536233058410, 0.015530130313785910
};

TEST(FirDesigner_Test, ReproducesShippedUnrealFilterTable)
{
    // UnrealFilter: 128-tap Hamming, fc=11025 @ fs=2822400 (44100 x 64)
    auto h = FirDesigner::hamming(128, 11025.0, 2822400.0);
    ASSERT_EQ(h.size(), 128u);

    for (size_t i = 0; i < 64; i++)
    {
        EXPECT_NEAR(h[i], SHIPPED_UNREAL_COEFFS_HEAD[i], 1e-15)
            << "Designer coefficient " << i << " diverges from shipped table";
        // Symmetry is mathematical, not bit-exact (window cos() arguments
        // differ at mirrored indices) - compare the tail to the golden head
        EXPECT_NEAR(h[127 - i], SHIPPED_UNREAL_COEFFS_HEAD[i], 1e-15)
            << "Designer tail coefficient " << (127 - i) << " diverges from shipped table";
    }

    // A live UnrealFilter at the default 44100 must carry the shipped design
    UnrealFilter filter;
    for (size_t i = 0; i < 64; i++)
    {
        EXPECT_NEAR(filter.coefficients()[i], SHIPPED_UNREAL_COEFFS_HEAD[i], 1e-15)
            << "Live UnrealFilter coefficient " << i << " diverges from shipped table";
    }
}

TEST(FirDesigner_Test, UnrealFilterRedesignsOnRateChange)
{
    UnrealFilter filter;
    const double coeff0At44k = filter.coefficients()[0];

    // Rate change must redesign: fc stays absolute (11025 Hz), so the shape
    // narrows relative to fs and the coefficients change
    filter.setTimings(3'500'000, 1'750'000, 96000);
    EXPECT_NE(filter.coefficients()[0], coeff0At44k);

    double dc = 0.0;
    for (size_t i = 0; i < 128; i++)
        dc += filter.coefficients()[i];
    EXPECT_NEAR(dc, 1.0, 1e-12);

    // Returning to 44100 must restore the shipped design exactly
    filter.setTimings(3'500'000, 1'750'000, 44100);
    EXPECT_DOUBLE_EQ(filter.coefficients()[0], coeff0At44k);
}

TEST(FirDesigner_Test, DCGainIsUnityAtEveryRate)
{
    for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0})
    {
        auto k = FirDesigner::kaiser(96, 20000.0, 218750.0, 5.0);
        auto kh = FirDesigner::kaiser(192, 20000.0, 218750.0, 9.0);  // HighFidelity tier
        auto hm = FirDesigner::hamming(128, 11025.0, rate * 64.0);

        for (auto* h : {&k, &kh, &hm})
        {
            double sum = 0.0;
            for (double x : *h)
                sum += x;
            EXPECT_NEAR(sum, 1.0, 1e-12) << "rate " << rate;
        }
    }
}

TEST(FirDesigner_Test, HighFidelityTierStopband)
{
    // beta=9 / 192 taps must beat beta=5 / 96 taps decisively: measure worst
    // stopband leakage via direct DTFT above the stopband edge
    auto reference = FirDesigner::kaiser(96, 20000.0, 218750.0, 5.0);
    auto hifi = FirDesigner::kaiser(192, 20000.0, 218750.0, 9.0);

    auto worstStopbandDb = [](const std::vector<double>& h, double fs, double fStart) {
        double worst = -300.0;
        for (double f = fStart; f < fs / 2.0; f += 500.0)
        {
            double re = 0.0, im = 0.0;
            for (size_t n = 0; n < h.size(); n++)
            {
                const double w = 2.0 * M_PI * f / fs * static_cast<double>(n);
                re += h[n] * std::cos(w);
                im -= h[n] * std::sin(w);
            }
            const double mag = std::sqrt(re * re + im * im);
            worst = std::max(worst, 20.0 * std::log10(std::max(mag, 1e-300)));
        }
        return worst;
    };

    const double refDb = worstStopbandDb(reference, 218750.0, 26000.0);
    const double hifiDb = worstStopbandDb(hifi, 218750.0, 26000.0);

    EXPECT_LT(refDb, -50.0) << "Reference tier stopband";
    EXPECT_LT(hifiDb, -85.0) << "HighFidelity tier must reach ~90 dB rejection";
    EXPECT_LT(hifiDb, refDb - 25.0) << "HighFidelity must decisively beat Reference";
}
