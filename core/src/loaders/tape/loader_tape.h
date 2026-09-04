#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tapetypes.h"

#include <memory>

/// region <Documentation>

/// The unified tape loader contract (design §5.3).
///
/// Every tape format implements LoaderTapeBase and produces a TapeImage from
/// a memory buffer. Loaders never touch the filesystem — `sourceName` is
/// diagnostics and an extension hint only — which is what makes fixtures,
/// fuzzing and archive containers (design Q5) work without temp files.
///
/// Loader selection is content-probed, with the extension as a tie-breaker:
/// Warajevo `.tap` files carry different framing behind the same extension,
/// the TAP-family variants are routinely renamed in the wild, and archives
/// hand us buffers whose names may be meaningless.
///
/// Registry loaders are context-free (no EmulatorContext) — the contract is
/// pure data in, TapeImage out. Loaders report anomalies through
/// TapeImage::status / parseWarnings instead of loggers.

/// endregion </Documentation>

class LoaderTapeBase
{
public:
    virtual ~LoaderTapeBase() = default;

    /// Whole-image decode from memory. `sourceName` is diagnostics + extension
    /// hint only. Never reads the filesystem, never throws.
    virtual TapeImage Load(std::span<const uint8_t> bytes, const std::string& sourceName) = 0;

    virtual const TapeFormatInfo& Format() const = 0;
};

/// Process-wide loader registry. The built-in formats are registered on
/// first use of Instance() (one line per loader — design §5.7 step 3);
/// Emulator::LoadTape validates against SupportedExtensions() and
/// Tape::EnsureImageLoaded() selects by content probe. Registration is
/// anchored in loader_tape.cpp because anonymous-namespace self-registrars
/// get dead-stripped from static archives in binaries that never name a
/// loader class.
class TapeLoaderRegistry
{
public:
    static TapeLoaderRegistry& Instance();

    TapeLoaderRegistry(const TapeLoaderRegistry&) = delete;
    TapeLoaderRegistry& operator=(const TapeLoaderRegistry&) = delete;

    /// Called once per format at startup. Ownership passes to the registry.
    void Register(std::unique_ptr<LoaderTapeBase> loader);

    /// Content probe first, extension as tie-breaker. Returns nullptr when no
    /// loader claims the buffer (caller emits Unsupported + the conversion hint).
    LoaderTapeBase* Select(std::span<const uint8_t> bytes, const std::string& fileName) const;

    /// All extensions any registered loader accepts — drives
    /// Emulator::LoadTape validation and file dialogs.
    std::vector<std::string> SupportedExtensions() const;

private:
    TapeLoaderRegistry() = default;

    static bool ExtensionOf(const std::string& fileName, const TapeFormatInfo& format);

    std::vector<std::unique_ptr<LoaderTapeBase>> _loaders;
};
