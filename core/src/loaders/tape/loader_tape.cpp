#include "stdafx.h"
#include "loader_tape.h"

#include "loader_tap.h"
#include "loader_tzx.h"

#include <algorithm>
#include <cctype>

/// region <Registry>

namespace
{
    /// Registers the built-in tape formats exactly once per process.
    ///
    /// This used to be a per-TU self-registration flag next to each loader,
    /// but registrar symbols in anonymous namespaces are invisible to other
    /// translation units — the static linker dead-stripped loader_tap.o and
    /// loader_tzx.o from binaries that never name a loader class (unreal-qt,
    /// headless automation), leaving the registry empty and every tape load
    /// failing with "no loader claims the buffer". Anchoring the calls here
    /// pulls both objects into any link that already needs loader_tape.o
    /// (via Instance()/Select()), and the magic static makes it idempotent
    /// and thread-safe (new formats add one line — design §5.7 step 3).
    void RegisterBuiltinLoaders(TapeLoaderRegistry& registry)
    {
        static const bool registered = [&registry]()
        {
            registry.Register(std::make_unique<LoaderTAP>());
            registry.Register(std::make_unique<LoaderTZX>());
            return true;
        }();
        static_cast<void>(registered);  // the flag is the once-guard, not data
    }
}

TapeLoaderRegistry& TapeLoaderRegistry::Instance()
{
    static TapeLoaderRegistry instance;
    RegisterBuiltinLoaders(instance);
    return instance;
}

void TapeLoaderRegistry::Register(std::unique_ptr<LoaderTapeBase> loader)
{
    if (loader != nullptr)
    {
        _loaders.push_back(std::move(loader));
    }
}

LoaderTapeBase* TapeLoaderRegistry::Select(std::span<const uint8_t> bytes, const std::string& fileName) const
{
    if (bytes.empty())
    {
        return nullptr;
    }

    // Content probe first: the highest score wins outright. The extension
    // only breaks ties between equally confident claimants (design §5.3) —
    // it must never override what the bytes say.
    LoaderTapeBase* best = nullptr;
    int bestScore = 0;
    bool bestMatchesExtension = false;

    for (const auto& loader : _loaders)
    {
        const TapeFormatInfo& format = loader->Format();
        if (format.Probe == nullptr)
        {
            continue;
        }

        int score = format.Probe(bytes);
        if (score <= 0)
        {
            continue;
        }

        bool matchesExtension = ExtensionOf(fileName, format);
        if (score > bestScore || (score == bestScore && matchesExtension && !bestMatchesExtension))
        {
            best = loader.get();
            bestScore = score;
            bestMatchesExtension = matchesExtension;
        }
    }

    return best;
}

std::vector<std::string> TapeLoaderRegistry::SupportedExtensions() const
{
    std::vector<std::string> result;

    for (const auto& loader : _loaders)
    {
        for (const std::string& extension : loader->Format().extensions)
        {
            if (std::find(result.begin(), result.end(), extension) == result.end())
            {
                result.push_back(extension);
            }
        }
    }

    return result;
}

/// endregion </Registry>

/// region <Helpers>

bool TapeLoaderRegistry::ExtensionOf(const std::string& fileName, const TapeFormatInfo& format)
{
    size_t dot = fileName.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= fileName.size())
    {
        return false;
    }

    std::string extension = fileName.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const std::string& candidate : format.extensions)
    {
        if (candidate == extension)
        {
            return true;
        }
    }

    return false;
}

/// endregion </Helpers>
