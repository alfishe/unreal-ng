#pragma once

/// Test-runner policy for the General Sound and MoonSound cards.
///
/// The shipped configs fit both cards on several models (GSType=Z80, MoonSound=1).
/// A GS card is a second Z80 at 12 MHz servicing a 37.5 kHz interrupt, MoonSound an
/// OPL4 synthesized every frame - together they roughly double the cost of every
/// emulated frame. Tests that are not about these cards gain nothing from them, so
/// the runner leaves both out of every machine it creates (Config::SetConfigLoadedHook,
/// installed in main() by InstallPolicy). A test that needs the
/// cards opts back in for the machines it creates by holding a SoundCardScope
/// while the emulator is initialized (typically a fixture member, or in SetUp()
/// before Init/Create).
class SoundCardScope
{
public:
    SoundCardScope();
    ~SoundCardScope();

    SoundCardScope(const SoundCardScope&) = delete;
    SoundCardScope& operator=(const SoundCardScope&) = delete;

    /// Whether a scope is active (machines created now keep the configured cards)
    static bool Active();

    /// Install the runner-wide policy (main(), before RUN_ALL_TESTS)
    static void InstallPolicy();
};
