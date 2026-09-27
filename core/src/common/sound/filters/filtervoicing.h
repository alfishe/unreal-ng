#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

/// Fixed tonal-balance EQ ("voicing") for a chip's stereo output - AY/SSG
/// today. A profile is a target curve, not an effect: an optional high-pass
/// (1st or 2nd order), an optional peaking EQ and an optional 2nd-order
/// low-pass, run in that order as biquad sections in transposed direct form
/// II with double coefficients and state (a 64 Hz pole sits at 0.998 at
/// 192 kHz - float state drifts and adds noise there).
///
/// Why it exists: the AY output used to pass through a moving-average DC
/// remover (x - mean of the last 1024 samples at 218.75 kHz) that shaped the
/// bass as a side effect. Commit 45812176 replaced it with the physically
/// correct 5 Hz coupling high-pass, which restored the full bass - and the
/// infrasonic thumps of per-frame volume steps. `Classic` recreates the old
/// balance on top of the physical model; `Flat` keeps the hardware output.
/// Design: docs/inprogress/2026-09-25-ay-tone-voicing/ay-tone-voicing-tdd.md
///
/// Difference equations (denominator 1 + a1 z^-1 + a2 z^-2, feedback
/// SUBTRACTED - the scipy / RBJ convention):
///
///     y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
///
/// Profiles are rows of one table; adding a profile is one row plus one enum
/// value. Hidden profiles (not tuned yet) are rejected by parsePreset() on
/// every user-facing input unless the caller explicitly allows them.
class FilterVoicing
{
public:
    enum class Preset : uint8_t
    {
        // Order = UI order: from no processing to the strongest colouring
        Flat = 0,      ///< No processing - the hardware line output (exact bypass)
        Classic,       ///< The pre-45812176 bass balance (as heard up to 4c49fb6)
        Headphones,    ///< Classic bass plus gently softened highs (square-wave edges)
        Warm,          ///< Between Headphones and Tv: softer bass and softer highs
        Tv,            ///< TV-speaker voicing: steep bass roll-off plus softened highs
        SmallSpeaker,  ///< Built-in speaker / cheap amplifier of a clone: thin, mid-forward
        COUNT
    };

    /// One target curve. Frequencies in Hz; a zero frequency / gain disables
    /// that section
    struct Profile
    {
        Preset preset;
        const char* id;           ///< Stable ID: settings, ini, WebAPI, QSettings
        const char* alias;        ///< Also accepted on input (nullptr = none)
        const char* label;        ///< Plain-language UI label
        const char* description;  ///< One-line explanation (tooltips, API docs)
        bool visible;             ///< Offered to users; hidden IDs are rejected on input

        int hpfOrder;  ///< 0 = no high-pass, 1 or 2
        double hpfHz;
        double hpfQ;  ///< order 2 only

        double peakHz;  ///< 0 = no peaking section
        double peakDb;
        double peakQ;

        double lpfHz;  ///< 0 = no low-pass (2nd order)
        double lpfQ;
    };

    static constexpr size_t PRESET_COUNT = static_cast<size_t>(Preset::COUNT);

    /// The profile a new sound stack starts with when nothing else is set
    /// ([SOUND] AYVoicing missing or invalid, no saved GUI preference):
    /// Classic bass with softened highs - the AY square waves are harsh
    /// through today's headphones and full-range speakers
    static constexpr Preset DEFAULT_PRESET = Preset::Headphones;

    /// The profile table. Classic is the least-squares fit to the old
    /// FilterDC response (1/3-octave smoothed, 20 Hz - 1 kHz): 0.39 dB mean,
    /// 1.3 dB max error; see the design doc §2.2.
    static const Profile& profile(Preset preset)
    {
        static constexpr std::array<Profile, PRESET_COUNT> kProfiles = {{
            {Preset::Flat, "flat", nullptr, "Flat (like real hardware line out)",
             "No voicing: the AY output as the hardware line out delivers it, including the very low bass "
             "and the thump of volume changes",
             true, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0},
            {Preset::Classic, "classic", "legacy", "Classic (softer bass)",
             "Trims the very low bass and the thump of volume changes, with a slight warm lift around "
             "100 Hz (64.2 Hz high-pass + 106.9 Hz / +3.06 dB peak)",
             true, 1, 64.2, 0.0, 106.9, 3.06, 1.0, 0.0, 0.0},
            {Preset::Headphones, "headphones", nullptr, "Headphones (soft highs)",
             "Classic bass plus gently softened highs, so the AY square waves are less harsh on "
             "headphones (critically damped low-pass: -1.9 dB at 5 kHz, -6 dB at 10 kHz)",
             true, 1, 64.2, 0.0, 106.9, 3.06, 1.0, 10000.0, 0.5},
            {Preset::Warm, "warm", nullptr, "Warm (softer bass and highs)",
             "Between Headphones and TV speaker: less deep bass and softer highs (2nd-order high-pass "
             "at 90 Hz, critically damped low-pass: -1.9 dB at 4 kHz, -6 dB at 8 kHz)",
             true, 2, 90.0, 0.6, 0.0, 0.0, 0.0, 8000.0, 0.5},
            {Preset::Tv, "tv", nullptr, "TV speaker",
             "How the music sounded through a TV set's small speaker: no deep bass, softened highs "
             "(130 Hz and 6 kHz, 2nd-order Butterworth high- and low-pass)",
             true, 2, 130.0, 0.7071, 0.0, 0.0, 0.0, 6000.0, 0.7071},
            {Preset::SmallSpeaker, "small_speaker", nullptr, "Small speaker",
             "A clone's built-in speaker or a cheap amplifier: no bass below ~250 Hz, a forward "
             "midrange around 1.5 kHz and no highs above ~4.5 kHz",
             true, 2, 250.0, 0.7071, 1500.0, 3.0, 1.0, 4500.0, 0.7071},
        }};
        const size_t index = static_cast<size_t>(preset);
        return kProfiles[index < PRESET_COUNT ? index : 0];
    }

    static const char* presetId(Preset preset)
    {
        return profile(preset).id;
    }

    static bool isVisible(Preset preset)
    {
        return profile(preset).visible;
    }

    /// Parse a profile ID or alias (case-insensitive). Hidden profiles are
    /// rejected unless @p allowHidden - every user-facing input (ini, WebAPI,
    /// CLI, Lua, Python, QSettings) uses the default, so a hidden ID can never
    /// be accepted and then silently behave as Flat
    static bool parsePreset(std::string_view text, Preset& out, bool allowHidden = false)
    {
        for (size_t i = 0; i < PRESET_COUNT; i++)
        {
            const Profile& p = profile(static_cast<Preset>(i));
            if (!p.visible && !allowHidden)
                continue;
            if (equalsIgnoreCase(text, p.id) || (p.alias && equalsIgnoreCase(text, p.alias)))
            {
                out = p.preset;
                return true;
            }
        }
        return false;
    }

    /// Visit every visible profile (UI lists, API "allowed" arrays, error messages)
    template <typename Fn>
    static void forEachVisible(Fn&& fn)
    {
        for (size_t i = 0; i < PRESET_COUNT; i++)
        {
            const Profile& p = profile(static_cast<Preset>(i));
            if (p.visible)
                fn(p);
        }
    }

    /// region <Construction / configuration>
public:
    FilterVoicing() = default;
    FilterVoicing(double sampleRate, Preset preset)
    {
        _sampleRate = sampleRate;
        setPreset(preset);
    }

    /// Re-derive the coefficients for a new sample rate. The filter STATE is
    /// kept: it is a few low-frequency samples that stay valid at the new
    /// rate, and clearing it would pass the current level as a step (a click)
    void setup(double sampleRate)
    {
        _sampleRate = sampleRate;
        design();
    }

    /// Select a profile: re-derives the coefficients and clears the state
    /// (the section structure may differ between profiles)
    void setPreset(Preset preset)
    {
        _preset = preset;
        design();
        reset();
    }

    Preset preset() const
    {
        return _preset;
    }

    double sampleRate() const
    {
        return _sampleRate;
    }

    /// No section to run (Flat, or an unconfigured filter): processing is a no-op
    bool isBypass() const
    {
        return _sectionCount == 0;
    }

    void reset()
    {
        for (Section& s : _sections)
        {
            s.s1 = {0.0, 0.0};
            s.s2 = {0.0, 0.0};
        }
    }
    /// endregion </Construction / configuration>

    /// region <Processing>
public:
    /// One sample of one channel (0 = left, 1 = right)
    double process(size_t channel, double x)
    {
        for (size_t i = 0; i < _sectionCount; i++)
            x = _sections[i].process(channel, x);
        return x;
    }

    /// Interleaved stereo int16, in place, saturating
    void processInt16(int16_t* interleaved, size_t frames)
    {
        if (isBypass())
            return;
        for (size_t n = 0; n < frames; n++)
        {
            interleaved[n * 2] = toInt16(process(0, interleaved[n * 2]));
            interleaved[n * 2 + 1] = toInt16(process(1, interleaved[n * 2 + 1]));
        }
    }

    /// Interleaved stereo float, in place (native-rate taps)
    void processFloat(float* interleaved, size_t frames)
    {
        if (isBypass())
            return;
        for (size_t n = 0; n < frames; n++)
        {
            interleaved[n * 2] = static_cast<float>(process(0, interleaved[n * 2]));
            interleaved[n * 2 + 1] = static_cast<float>(process(1, interleaved[n * 2 + 1]));
        }
    }

    static int16_t toInt16(double value)
    {
        const double rounded = std::nearbyint(value);
        return static_cast<int16_t>(std::clamp(rounded, -32768.0, 32767.0));
    }

    /// Sum of |state| over all sections and channels - tests use it to tell a
    /// running filter from a freshly reset one
    double stateMagnitude() const
    {
        double sum = 0.0;
        for (size_t i = 0; i < _sectionCount; i++)
        {
            for (size_t ch = 0; ch < 2; ch++)
                sum += std::fabs(_sections[i].s1[ch]) + std::fabs(_sections[i].s2[ch]);
        }
        return sum;
    }
    /// endregion </Processing>

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kDenormalFloor = 1e-30;
    static constexpr size_t kMaxSections = 3;

    /// Biquad in transposed direct form II, per-channel state
    struct Section
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        std::array<double, 2> s1{0.0, 0.0};
        std::array<double, 2> s2{0.0, 0.0};

        double process(size_t ch, double x)
        {
            const double y = b0 * x + s1[ch];
            double n1 = b1 * x - a1 * y + s2[ch];
            double n2 = b2 * x - a2 * y;
            // A decaying tail sinks into denormals after a few seconds of
            // silence (a large slowdown on x86); nothing below this is audible
            if (std::fabs(n1) < kDenormalFloor)
                n1 = 0.0;
            if (std::fabs(n2) < kDenormalFloor)
                n2 = 0.0;
            s1[ch] = n1;
            s2[ch] = n2;
            return y;
        }

        /// Set from unnormalised coefficients (a0 is the normaliser)
        void set(double nb0, double nb1, double nb2, double a0, double na1, double na2)
        {
            b0 = nb0 / a0;
            b1 = nb1 / a0;
            b2 = nb2 / a0;
            a1 = na1 / a0;
            a2 = na2 / a0;
        }
    };

    void design()
    {
        _sectionCount = 0;
        if (_sampleRate <= 0.0)
            return;

        const Profile& p = profile(_preset);
        const double nyquist = _sampleRate / 2.0;

        if (p.hpfOrder == 1 && p.hpfHz > 0.0 && p.hpfHz < nyquist)
        {
            // Bilinear first-order high-pass: b0 = 1/(1+K), b1 = -b0, a1 = (K-1)/(K+1)
            const double k = std::tan(kPi * p.hpfHz / _sampleRate);
            Section& s = _sections[_sectionCount++];
            s.set(1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0);
        }
        else if (p.hpfOrder == 2 && p.hpfHz > 0.0 && p.hpfHz < nyquist)
        {
            const double w0 = 2.0 * kPi * p.hpfHz / _sampleRate;
            const double c = std::cos(w0);
            const double alpha = std::sin(w0) / (2.0 * p.hpfQ);
            Section& s = _sections[_sectionCount++];
            s.set((1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0, 1.0 + alpha, -2.0 * c, 1.0 - alpha);
        }

        if (p.peakHz > 0.0 && p.peakDb != 0.0 && p.peakHz < nyquist)
        {
            // RBJ peaking EQ, fully normalised by a0 = 1 + alpha/A
            const double a = std::pow(10.0, p.peakDb / 40.0);
            const double w0 = 2.0 * kPi * p.peakHz / _sampleRate;
            const double c = std::cos(w0);
            const double alpha = std::sin(w0) / (2.0 * p.peakQ);
            Section& s = _sections[_sectionCount++];
            s.set(1.0 + alpha * a, -2.0 * c, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * c, 1.0 - alpha / a);
        }

        if (p.lpfHz > 0.0 && p.lpfHz < nyquist)
        {
            // Magnitude-matched low-pass (M. Vicanek, "Matched Second Order
            // Digital Filters", 2016): impulse-invariant poles, zeros fitted
            // so |H| equals the analog prototype at DC, Nyquist and f0. The
            // bilinear (RBJ) low-pass is pulled towards zero at Nyquist, so a
            // 6 kHz corner at 44.1 kHz would cut 15 kHz ~8 dB more than at
            // 192 kHz; this one stays within ~0.3 dB of the analog curve at
            // every core rate up to 15 kHz. Requires Q >= 0.5 (no real poles)
            const double w0 = 2.0 * kPi * p.lpfHz / _sampleRate;
            const double q = 1.0 / (2.0 * p.lpfQ);
            const double a1 = -2.0 * std::exp(-q * w0) * std::cos(std::sqrt(1.0 - q * q) * w0);
            const double a2 = std::exp(-2.0 * q * w0);
            const double bigA0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
            const double bigA1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
            const double bigA2 = -4.0 * a2;
            const double phi1 = std::sin(w0 / 2.0) * std::sin(w0 / 2.0);
            const double phi0 = 1.0 - phi1;
            const double phi2 = 4.0 * phi0 * phi1;
            const double r1 = (bigA0 * phi0 + bigA1 * phi1 + bigA2 * phi2) * p.lpfQ * p.lpfQ;
            const double bigB0 = bigA0;
            const double bigB1 = (r1 - bigB0 * phi0) / phi1;
            const double b0 = 0.5 * (std::sqrt(bigB0) + std::sqrt(std::max(bigB1, 0.0)));
            const double b1 = std::sqrt(bigB0) - b0;
            Section& s = _sections[_sectionCount++];
            s.set(b0, b1, 0.0, 1.0, a1, a2);
        }
    }

    static bool equalsIgnoreCase(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    double _sampleRate = 0.0;
    Preset _preset = Preset::Flat;
    std::array<Section, kMaxSections> _sections{};
    size_t _sectionCount = 0;
};
