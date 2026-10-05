#pragma once

#include <cstdint>

enum class GSTypeKind : uint8_t;

/// Sound devices the test runner leaves out of every machine by default
enum class TestSound : uint8_t
{
    GeneralSound = 1 << 0,  ///< GS card (LLE coprocessor or the lightweight player)
    MoonSound    = 1 << 1,  ///< MoonSound (OPL4) card
    TurboSound   = 1 << 2,  ///< TurboSound slot: AY / TurboSound (2 x AY) / TSFM, whichever the config names
    All          = GeneralSound | MoonSound | TurboSound,
};

constexpr TestSound operator|(TestSound a, TestSound b)
{
    return static_cast<TestSound>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

/// Test-runner policy for the sound devices.
///
/// The shipped configs fit most of them on several models (TurboSound=FM,
/// GSType=Z80, MoonSound=1). Each one is synthesized every frame - GS is a
/// second Z80 at 12 MHz servicing a 37.5 kHz interrupt, MoonSound an OPL4,
/// TSFM two YM2203 cores, even the plain AY pair runs its generators and
/// anti-alias decimators - and together they cost more than the machine
/// itself. Tests that are not about sound gain nothing from them, so the
/// runner leaves all of them out of every machine it creates
/// (Config::SetConfigLoadedHook, installed in main() by InstallPolicy): the
/// TurboSound slot is empty (TurboSound=None - AY ports on the floating bus),
/// no GS, no MoonSound.
///
/// A test that needs a device opts back in for the machines it creates by
/// holding a SoundCardScope while the emulator is initialized (typically a
/// fixture member, or in SetUp() before Init/Create). The devices named keep
/// whatever the config says (e.g. TurboSound=FM stays TSFM); the others stay out.
class SoundCardScope
{
public:
    /// Every device as configured
    SoundCardScope();
    /// Only the named devices as configured
    explicit SoundCardScope(TestSound devices);
    ~SoundCardScope();

    SoundCardScope(const SoundCardScope&) = delete;
    SoundCardScope& operator=(const SoundCardScope&) = delete;

    /// Whether a scope for @p device is active (machines created now keep it as configured)
    static bool Active(TestSound device);

    /// Install the runner-wide policy (main(), before RUN_ALL_TESTS)
    static void InstallPolicy();

private:
    TestSound _devices;
};

/// Fits a General Sound card of the given kind in every machine created while it lives, whatever the config says.
///
/// For tests that load a recording made with a card on a model whose shipped config has none (owner decision
/// 2026-10-04: the 48K, 128K, +2, +2A, +3, Profi v5 / v3 and Sprinter configs ship without a GS): the card has to be
/// there at creation, as a user's [SLOTS] entry would put it - a personality switch (FitGeneralSoundCard) changes a
/// fitted card but cannot fill an empty slot. The kind goes into the config field the slot set is translated from;
/// NONE leaves the config alone. Implies a SoundCardScope for the GS.
class GeneralSoundFitScope
{
public:
    explicit GeneralSoundFitScope(GSTypeKind kind);
    ~GeneralSoundFitScope();

    GeneralSoundFitScope(const GeneralSoundFitScope&) = delete;
    GeneralSoundFitScope& operator=(const GeneralSoundFitScope&) = delete;

    /// The kind machines created now are fitted with (NONE: as configured)
    static GSTypeKind Kind();

private:
    uint8_t _previous;
};
