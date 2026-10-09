#include "audio_character_chain.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

/// region <Constructors / Destructors>

AudioCharacterChain::AudioCharacterChain()
{
    reset();
}

/// endregion </Constructors / Destructors>

/// region <Setup>

void AudioCharacterChain::setup(double sampleRate)
{
    _sampleRate = sampleRate;
    deriveRateCoefficients();
    reset();
}

/// Map 44.1kHz-referenced coefficients to the actual sample rate.
/// coeff^(44100/fs) preserves the time constant exactly:
/// exp(-1/(tau*44100))^(44100/fs) == exp(-1/(tau*fs)).
/// At 44.1kHz the exponent is 1 - bit-identical to the shipped behavior.
void AudioCharacterChain::deriveRateCoefficients()
{
    const double ratio = 44100.0 / _sampleRate;

    _releaseEff = static_cast<float>(std::pow(_release, ratio));
    _attackEff = static_cast<float>(1.0 - std::pow(1.0 - _attack, ratio));
    _roomLpCoefEff = static_cast<float>(1.0 - std::pow(1.0 - _roomLpCoef, ratio));

    // Plan C.1: normalize the first difference so the punch tilt and the
    // envelope input keep their 44.1kHz magnitudes at every rate
    _diffNorm = static_cast<float>(_sampleRate / 44100.0);
}

void AudioCharacterChain::reset()
{
    _roomDirty = true;   // clear unconditionally
    clearPunchState();
    clearRoomState();
    _punchGain = 0.0f;
    _roomGain = 0.0f;
    _engaged = false;
    _snapPending = true;
}

/// endregion </Setup>

/// region <Punch Configuration>

void AudioCharacterChain::setPunchPreset(PunchPreset preset)
{
    _punchPreset = preset;

    switch (preset)
    {
        case PunchPreset::Paula:
        case PunchPreset::Beeper:
            // Original Paula preset - good for sampled material
            _edgeBlend = 0.08f;
            _transBoost = 0.2f;
            _attack = 0.3f;
            _release = 0.998f;
            break;

        case PunchPreset::AY:
            // Gentler for square waves - AY already has rich harmonics
            // Higher release to avoid pumping on continuous tones
            _edgeBlend = 0.03f;
            _transBoost = 0.1f;
            _attack = 0.3f;
            _release = 0.9995f;  // ~45ms @ 44.1kHz
            break;

        case PunchPreset::Custom:
            // Keep current values
            break;
    }

    deriveRateCoefficients();
}

void AudioCharacterChain::setPunchParams(float edgeBlend, float transBoost, float attack, float release)
{
    _edgeBlend = edgeBlend;
    _transBoost = transBoost;
    _attack = attack;
    _release = release;
    deriveRateCoefficients();
}

/// endregion </Punch Configuration>

/// region <Room Configuration>

const char* AudioCharacterChain::roomModeName(RoomMode mode)
{
    switch (mode)
    {
        case RoomMode::Off:       return "Off";
        case RoomMode::Room_15dB: return "Room -15dB";
        case RoomMode::Room_14dB: return "Room -14dB";
        case RoomMode::Room_13dB: return "Room -13dB";
        case RoomMode::Room_12dB: return "Room -12dB";
        case RoomMode::Room_9dB:  return "Room -9dB";
        case RoomMode::Room_6dB:  return "Room -6dB";
        case RoomMode::Room_3dB:  return "Room -3dB";
        case RoomMode::Room_2dB:  return "Room -2dB";
        case RoomMode::Room_1dB:  return "Room -1dB";
        default: return "?";
    }
}

const char* AudioCharacterChain::roomModeId(RoomMode mode)
{
    switch (mode)
    {
        case RoomMode::Off:       return "off";
        case RoomMode::Room_15dB: return "15db";
        case RoomMode::Room_14dB: return "14db";
        case RoomMode::Room_13dB: return "13db";
        case RoomMode::Room_12dB: return "12db";
        case RoomMode::Room_9dB:  return "9db";
        case RoomMode::Room_6dB:  return "6db";
        case RoomMode::Room_3dB:  return "3db";
        case RoomMode::Room_2dB:  return "2db";
        case RoomMode::Room_1dB:  return "1db";
        default: return "off";
    }
}

bool AudioCharacterChain::parseRoomMode(std::string_view text, RoomMode& out)
{
    std::string id;
    for (char c : text)
        id.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (!id.empty() && id[0] == '-')
        id.erase(0, 1);
    if (!id.empty() && id.find("db") == std::string::npos && id != "off")
        id += "db";

    for (int i = 0; i < static_cast<int>(RoomMode::COUNT); i++)
    {
        const auto mode = static_cast<RoomMode>(i);
        if (id == roomModeId(mode))
        {
            out = mode;
            return true;
        }
    }
    return false;
}

void AudioCharacterChain::setRoomMode(RoomMode mode)
{
    _roomMode = mode;

    // Room parameters vary by chip type:
    // - Paula: 3ms delay, 10kHz LP (rich 8-bit samples tolerate filtering)
    // - AY: 2ms delay, no LP (square wave harmonics are essential)
    int baseDelay = static_cast<int>(0.003 * _sampleRate);
    int ayDelay = static_cast<int>(0.002 * _sampleRate);

    switch (mode)
    {
        case RoomMode::Off:
            _roomEnabled = false;
            break;

        case RoomMode::Room_15dB:
            _roomEnabled = true;
            _roomLevel = 0.178f;   // -15dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_14dB:
            _roomEnabled = true;
            _roomLevel = 0.20f;    // -14dB (recommended)
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_13dB:
            _roomEnabled = true;
            _roomLevel = 0.224f;   // -13dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_12dB:
            _roomEnabled = true;
            _roomLevel = 0.25f;    // -12dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_9dB:
            _roomEnabled = true;
            _roomLevel = 0.35f;    // -9dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_6dB:
            _roomEnabled = true;
            _roomLevel = 0.50f;    // -6dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_3dB:
            _roomEnabled = true;
            _roomLevel = 0.71f;    // -3dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_2dB:
            _roomEnabled = true;
            _roomLevel = 0.79f;    // -2dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        case RoomMode::Room_1dB:
            _roomEnabled = true;
            _roomLevel = 0.89f;    // -1dB
            _roomDelay = _chipType == ChipType::AY ? ayDelay : baseDelay;
            _roomLpCoef = _chipType == ChipType::AY ? 1.0f : 0.7f;
            break;

        default:
            _roomEnabled = false;
            break;
    }

    deriveRateCoefficients();
}

/// endregion </Room Configuration>

/// region <Processing>

void AudioCharacterChain::process(float* left, float* right, int32_t numSamples)
{
    for (int32_t i = 0; i < numSamples; i++)
    {
        float outL = left[i];
        float outR = right[i];

        // Punch enhancement
        if (_punchEnabled)
        {
            float diffL = outL - _prevOutL;
            float diffR = outR - _prevOutR;

            // Envelope follower on difference magnitude
            float magL = std::abs(diffL);
            float magR = std::abs(diffR);
            _envL = (magL > _envL) ? magL * _attack + _envL * (1 - _attack) : _envL * _release;
            _envR = (magR > _envR) ? magR * _attack + _envR * (1 - _attack) : _envR * _release;

            // Edge component (constant +6dB/oct tilt)
            outL += diffL * _edgeBlend;
            outR += diffR * _edgeBlend;

            // Transient component (envelope-gated, activates on attacks)
            outL += diffL * _envL * _transBoost;
            outR += diffR * _envR * _transBoost;

            _prevOutL = left[i];
            _prevOutR = right[i];
        }

        // Room simulation
        if (_roomEnabled)
        {
            // Store current samples in delay line
            _delayL[_delayIdx] = outL;
            _delayR[_delayIdx] = outR;

            // Get delayed opposite channel
            int delayedIdx = (_delayIdx - _roomDelay + MAX_DELAY) % MAX_DELAY;
            float delayedL = _delayR[delayedIdx];  // R->L
            float delayedR = _delayL[delayedIdx];  // L->R

            // Gentle lowpass (~10kHz - air absorption, not head shadow)
            _roomLpL += _roomLpCoefEff * (delayedL - _roomLpL);
            _roomLpR += _roomLpCoefEff * (delayedR - _roomLpR);

            // Mix
            outL += _roomLpL * _roomLevel;
            outR += _roomLpR * _roomLevel;

            _delayIdx = (_delayIdx + 1) % MAX_DELAY;
        }

        left[i] = outL;
        right[i] = outR;
    }
}

namespace
{
constexpr float kToFloat = 1.0f / 32768.0f;
constexpr float kToInt16 = 32767.0f;

inline int16_t ToInt16(float value)
{
    return static_cast<int16_t>(std::clamp(value, -32768.0f, 32767.0f));
}
}  // namespace

void AudioCharacterChain::clearPunchState()
{
    _prevOutL = _prevOutR = 0;
    _envL = _envR = 0;
    _punchDirty = false;
}

void AudioCharacterChain::clearRoomState()
{
    if (!_roomDirty)
        return;
    _delayL.fill(0);
    _delayR.fill(0);
    _delayIdx = 0;
    _roomLpL = _roomLpR = 0;
    _roomDirty = false;
}

/// Punch switching on: the first difference starts from the current input
/// (no step against a stale or zero previous sample), the envelope from rest
void AudioCharacterChain::primePunch(const int16_t* buffer)
{
    _prevOutL = buffer[0] * kToFloat;
    _prevOutR = buffer[1] * kToFloat;
    _envL = _envR = 0;
    _punchDirty = true;
}

/// Room switching on: the delay line and its lowpass hold the current input,
/// so the delayed signal continues from it when the first real sample comes
/// out of the line - never old audio, never a jump from zero
void AudioCharacterChain::primeRoom(const int16_t* buffer)
{
    const float left = buffer[0] * kToFloat;
    const float right = buffer[1] * kToFloat;
    _delayL.fill(left);
    _delayR.fill(right);
    _delayIdx = 0;
    _roomLpL = right;   // the left output takes the delayed right channel
    _roomLpR = left;
    _roomDirty = true;
}

void AudioCharacterChain::processEngaged(int16_t* buffer, int32_t numSamples)
{
    if (numSamples <= 0)
        return;

    const bool punchOn = _active && _punchEnabled;
    const bool roomOn = _active && _roomEnabled;
    const float punchTarget = punchOn ? 1.0f : 0.0f;
    const float roomTarget = roomOn ? _roomLevel : 0.0f;

    if (_snapPending)
    {
        // First call after reset(): the settings in force, no ramp
        _snapPending = false;
        _punchGain = punchTarget;
        _roomGain = roomTarget;
    }

    // Effects that start in this call start from the current input
    if (punchOn && _punchGain == 0.0f)
        primePunch(buffer);
    if (roomOn && _roomGain == 0.0f)
        primeRoom(buffer);

    if (_punchGain == punchTarget && _roomGain == roomTarget)
    {
        // Steady: the exact per-sample path, only the stages that are on
        if (punchOn && roomOn)
            processSteady<true, true>(buffer, numSamples);
        else if (punchOn)
            processSteady<true, false>(buffer, numSamples);
        else if (roomOn)
            processSteady<false, true>(buffer, numSamples);
    }
    else
    {
        const bool wetFrom = _punchGain != 0.0f || _roomGain != 0.0f;
        const bool wetTo = punchOn || roomOn;
        if (wetFrom != wetTo)
        {
            // The whole chain switches on or off: the effects run at their
            // "on" setting and the frame crossfades between the untouched
            // input and the processed signal - both ends match their steady
            // paths exactly (the bypass is bit-exact, the processed path
            // carries the int16 round trip)
            processRamp(buffer, numSamples, std::max(_punchGain, punchTarget), std::max(_punchGain, punchTarget),
                        std::max(_roomGain, roomTarget), std::max(_roomGain, roomTarget), wetFrom ? 1.0f : 0.0f,
                        wetTo ? 1.0f : 0.0f);
        }
        else
        {
            // The chain stays on, one effect switches or the room level
            // changes: that effect's weight ramps
            processRamp(buffer, numSamples, _punchGain, punchTarget, _roomGain, roomTarget, 1.0f, 1.0f);
        }
    }

    _punchGain = punchTarget;
    _roomGain = roomTarget;
    _engaged = punchOn || roomOn;

    // Ramped out: forget the audio the effect holds, so switching it on later
    // never replays it
    if (!punchOn && _punchDirty)
        clearPunchState();
    if (!roomOn)
        clearRoomState();
}

template <bool Punch, bool Room>
void AudioCharacterChain::processSteady(int16_t* buffer, int32_t numSamples)
{
    if constexpr (Punch)
        _punchDirty = true;
    if constexpr (Room)
        _roomDirty = true;

    for (int32_t i = 0; i < numSamples; i++)
    {
        const float left = buffer[i * 2] * kToFloat;
        const float right = buffer[i * 2 + 1] * kToFloat;

        float outL = left;
        float outR = right;

        // Punch enhancement
        if constexpr (Punch)
        {
            // Normalized first difference (plan C.1): rate-invariant punch
            float diffL = (outL - _prevOutL) * _diffNorm;
            float diffR = (outR - _prevOutR) * _diffNorm;

            float magL = std::abs(diffL);
            float magR = std::abs(diffR);
            _envL = (magL > _envL) ? magL * _attackEff + _envL * (1 - _attackEff) : _envL * _releaseEff;
            _envR = (magR > _envR) ? magR * _attackEff + _envR * (1 - _attackEff) : _envR * _releaseEff;

            outL += diffL * _edgeBlend;
            outR += diffR * _edgeBlend;
            outL += diffL * _envL * _transBoost;
            outR += diffR * _envR * _transBoost;

            _prevOutL = left;
            _prevOutR = right;
        }

        // Room simulation
        if constexpr (Room)
        {
            _delayL[_delayIdx] = outL;
            _delayR[_delayIdx] = outR;

            const int delayedIdx = (_delayIdx - _roomDelay + MAX_DELAY) & (MAX_DELAY - 1);
            float delayedL = _delayR[delayedIdx];
            float delayedR = _delayL[delayedIdx];

            _roomLpL += _roomLpCoefEff * (delayedL - _roomLpL);
            _roomLpR += _roomLpCoefEff * (delayedR - _roomLpR);

            outL += _roomLpL * _roomLevel;
            outR += _roomLpR * _roomLevel;

            _delayIdx = (_delayIdx + 1) & (MAX_DELAY - 1);
        }

        // Clamp and convert back to int16
        buffer[i * 2] = ToInt16(outL * kToInt16);
        buffer[i * 2 + 1] = ToInt16(outR * kToInt16);
    }
}

/// One switch frame: the punch weight, the room level and the dry / wet
/// weight move linearly from their old to their new values across the call
/// (sample n at (n + 0.5) / numSamples, as VoicingStage crossfades)
void AudioCharacterChain::processRamp(int16_t* buffer, int32_t numSamples, float punchFrom, float punchTo,
                                      float roomFrom, float roomTo, float wetFrom, float wetTo)
{
    const bool punch = punchFrom != 0.0f || punchTo != 0.0f;
    const bool room = roomFrom != 0.0f || roomTo != 0.0f;
    if (punch)
        _punchDirty = true;
    if (room)
        _roomDirty = true;

    const float step = 1.0f / static_cast<float>(numSamples);
    for (int32_t i = 0; i < numSamples; i++)
    {
        const float t = (static_cast<float>(i) + 0.5f) * step;
        const float punchGain = punchFrom + (punchTo - punchFrom) * t;
        const float roomGain = roomFrom + (roomTo - roomFrom) * t;
        const float wet = wetFrom + (wetTo - wetFrom) * t;

        const float dryL = buffer[i * 2];
        const float dryR = buffer[i * 2 + 1];
        const float left = dryL * kToFloat;
        const float right = dryR * kToFloat;

        float outL = left;
        float outR = right;

        if (punch)
        {
            const float diffL = (outL - _prevOutL) * _diffNorm;
            const float diffR = (outR - _prevOutR) * _diffNorm;

            const float magL = std::abs(diffL);
            const float magR = std::abs(diffR);
            _envL = (magL > _envL) ? magL * _attackEff + _envL * (1 - _attackEff) : _envL * _releaseEff;
            _envR = (magR > _envR) ? magR * _attackEff + _envR * (1 - _attackEff) : _envR * _releaseEff;

            outL += punchGain * (diffL * _edgeBlend + diffL * _envL * _transBoost);
            outR += punchGain * (diffR * _edgeBlend + diffR * _envR * _transBoost);

            _prevOutL = left;
            _prevOutR = right;
        }

        if (room)
        {
            _delayL[_delayIdx] = outL;
            _delayR[_delayIdx] = outR;

            const int delayedIdx = (_delayIdx - _roomDelay + MAX_DELAY) & (MAX_DELAY - 1);
            const float delayedL = _delayR[delayedIdx];
            const float delayedR = _delayL[delayedIdx];

            _roomLpL += _roomLpCoefEff * (delayedL - _roomLpL);
            _roomLpR += _roomLpCoefEff * (delayedR - _roomLpR);

            outL += _roomLpL * roomGain;
            outR += _roomLpR * roomGain;

            _delayIdx = (_delayIdx + 1) & (MAX_DELAY - 1);
        }

        buffer[i * 2] = ToInt16((1.0f - wet) * dryL + wet * (outL * kToInt16));
        buffer[i * 2 + 1] = ToInt16((1.0f - wet) * dryR + wet * (outR * kToInt16));
    }
}

/// endregion </Processing>
