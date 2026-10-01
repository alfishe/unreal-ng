#include "joystick.h"

#include <algorithm>
#include <cctype>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/pckey.h"
#include "emulator/ports/portdecoder.h"
#include "stdafx.h"

namespace
{
constexpr uint8_t kJoystickTTDVersion = 1;

std::string Lower(const std::string& text)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    return lower;
}

std::string Trim(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])))
        begin++;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
        end--;
    return text.substr(begin, end - begin);
}

/// pckey names the keypad as kp_8; the short spelling kp8 is what the default list and the docs use
PcKey ResolveKey(const std::string& name)
{
    PcKey key = pckey::FromName(name);
    if (key != PcKey::None)
        return key;

    const std::string lower = Lower(name);
    if (lower.size() > 2 && lower.compare(0, 2, "kp") == 0 && lower[2] != '_')
        return pckey::FromName("kp_" + lower.substr(2));
    return PcKey::None;
}
}  // namespace

Joystick::Joystick(EmulatorContext* context)
{
    _context = context;
    if (_context)
        _logger = _context->pModuleLogger;

    Reset();
    ApplyConfiguration();
}

void Joystick::Reset()
{
    // Power-on value. Fitting and the key bindings are configuration, not state - kept.
    _state.store(0x00, std::memory_order_relaxed);
}

void Joystick::ApplyConfiguration()
{
    // No context (bare unit test): keep the defaults the member initializers give
    if (!_context)
    {
        SetBindings(kDefaultKeys);
        return;
    }

    const CONFIG& config = _context->config;

    // A config that never parsed the key (unit-test contexts without an ini) keeps the device fitted
    const bool fittedByConfig = !config.input.joystickConfigured || config.input.joystick != 0;
    const bool enabledByFeature =
        !_context->pFeatureManager || _context->pFeatureManager->isEnabled(Features::kKempstonJoystick);
    SetPresent(fittedByConfig && enabledByFeature);

    // Absent key: the keypad defaults. Present and empty: the host keys are off
    const std::string spec = config.input.joystickKeysConfigured ? std::string(config.input.joystickKeys)
                                                                 : std::string(kDefaultKeys);
    if (!SetBindings(spec))
    {
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("Config: [INPUT] JoystickKeys='%s' has entries that are not 'button:key' (buttons up, down, "
                    "left, right, fire, b5, b6, b7); those were skipped",
                    spec.c_str());
    }
}

/// region <Host keys>

bool Joystick::SetBindings(const std::string& spec)
{
    std::array<uint8_t, 8> table{};
    bool allValid = true;

    size_t position = 0;
    while (position <= spec.size())
    {
        size_t comma = spec.find(',', position);
        if (comma == std::string::npos)
            comma = spec.size();
        const std::string entry = Trim(spec.substr(position, comma - position));
        position = comma + 1;

        if (entry.empty())
            continue;

        const size_t colon = entry.find(':');
        uint8_t mask = 0;
        PcKey key = PcKey::None;
        if (colon != std::string::npos)
        {
            mask = MaskFromName(Trim(entry.substr(0, colon)));
            key = ResolveKey(Trim(entry.substr(colon + 1)));
        }
        if (mask == 0 || key == PcKey::None)
        {
            allValid = false;
            continue;
        }

        for (size_t bit = 0; bit < table.size(); bit++)
        {
            if (mask & (1u << bit))
                table[bit] = static_cast<uint8_t>(key);  // a later entry for a button replaces the earlier one
        }
    }

    for (size_t bit = 0; bit < table.size(); bit++)
        _boundKey[bit].store(table[bit], std::memory_order_relaxed);
    return allValid;
}

std::string Joystick::BindingsSpec() const
{
    std::string spec;
    for (size_t bit = 0; bit < _boundKey.size(); bit++)
    {
        const auto key = static_cast<PcKey>(_boundKey[bit].load(std::memory_order_relaxed));
        if (key == PcKey::None)
            continue;
        if (!spec.empty())
            spec += ',';
        spec += NameOfBit(static_cast<uint8_t>(1u << bit)) + ":" + pckey::Name(key);
    }
    return spec;
}

bool Joystick::WantsKey(PcKey key) const
{
    // Machines whose port decoder has no #1F joystick arm never see the host keys: no journal entry, no call
    if (key == PcKey::None || !IsPresent())
        return false;
    if (_context && _context->pPortDecoder && !_context->pPortDecoder->HasKempstonJoystick())
        return false;

    const auto raw = static_cast<uint8_t>(key);
    for (const auto& bound : _boundKey)
    {
        if (bound.load(std::memory_order_relaxed) == raw)
            return true;
    }
    return false;
}

bool Joystick::OnPcKey(PcKey key, bool pressed)
{
    if (key == PcKey::None || !IsPresent())
        return false;

    uint8_t mask = 0;
    const auto raw = static_cast<uint8_t>(key);
    for (size_t bit = 0; bit < _boundKey.size(); bit++)
    {
        if (_boundKey[bit].load(std::memory_order_relaxed) == raw)
            mask |= static_cast<uint8_t>(1u << bit);
    }
    if (mask == 0)
        return false;

    if (pressed)
        Press(mask);
    else
        Release(mask);
    return true;
}

void Joystick::ReleaseBoundKeys()
{
    uint8_t mask = 0;
    for (size_t bit = 0; bit < _boundKey.size(); bit++)
    {
        if (_boundKey[bit].load(std::memory_order_relaxed) != 0)
            mask |= static_cast<uint8_t>(1u << bit);
    }
    if (mask)
        Release(mask);
}

/// endregion </Host keys>

/// region <Names>

uint8_t Joystick::MaskFromName(const std::string& name)
{
    const std::string lower = Lower(name);
    if (lower == "right")
        return kRight;
    if (lower == "left")
        return kLeft;
    if (lower == "down")
        return kDown;
    if (lower == "up")
        return kUp;
    if (lower == "fire")
        return kFire;
    if (lower == "b5")
        return 0x20;
    if (lower == "b6")
        return 0x40;
    if (lower == "b7")
        return 0x80;
    return 0;
}

std::string Joystick::NameOfBit(uint8_t mask)
{
    switch (mask)
    {
        case kRight: return "right";
        case kLeft: return "left";
        case kDown: return "down";
        case kUp: return "up";
        case kFire: return "fire";
        case 0x20: return "b5";
        case 0x40: return "b6";
        case 0x80: return "b7";
        default: return "";
    }
}

/// endregion </Names>

/// region <TTD>

size_t Joystick::TTDStateSize() const
{
    return 2;  // version, state
}

void Joystick::TTDSaveState(uint8_t* dst) const
{
    dst[0] = kJoystickTTDVersion;
    dst[1] = State();
}

void Joystick::TTDLoadState(const uint8_t* src)
{
    SetState(src[1]);
}

uint64_t Joystick::TTDHashState() const
{
    // FNV-1a over the state byte (no version byte)
    uint64_t hash = 0xcbf29ce484222325ull;
    hash ^= State();
    hash *= 0x100000001b3ull;
    return hash;
}

/// endregion </TTD>
