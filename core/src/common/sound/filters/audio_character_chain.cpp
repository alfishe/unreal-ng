#include "audio_character_chain.h"

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
    reset();
}

void AudioCharacterChain::reset()
{
    _prevOutL = _prevOutR = 0;
    _envL = _envR = 0;
    _delayL.fill(0);
    _delayR.fill(0);
    _delayIdx = 0;
    _roomLpL = _roomLpR = 0;
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
}

void AudioCharacterChain::setPunchParams(float edgeBlend, float transBoost, float attack, float release)
{
    _edgeBlend = edgeBlend;
    _transBoost = transBoost;
    _attack = attack;
    _release = release;
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
            _roomLpL += _roomLpCoef * (delayedL - _roomLpL);
            _roomLpR += _roomLpCoef * (delayedR - _roomLpR);

            // Mix
            outL += _roomLpL * _roomLevel;
            outR += _roomLpR * _roomLevel;

            _delayIdx = (_delayIdx + 1) % MAX_DELAY;
        }

        left[i] = outL;
        right[i] = outR;
    }
}

void AudioCharacterChain::processInt16(int16_t* buffer, int32_t numSamples)
{
    // Convert to float, process, convert back
    // Note: For better performance, consider keeping internal float buffer
    constexpr float toFloat = 1.0f / 32768.0f;
    constexpr float toInt16 = 32767.0f;

    for (int32_t i = 0; i < numSamples; i++)
    {
        float left = buffer[i * 2] * toFloat;
        float right = buffer[i * 2 + 1] * toFloat;

        float outL = left;
        float outR = right;

        // Punch enhancement
        if (_punchEnabled)
        {
            float diffL = outL - _prevOutL;
            float diffR = outR - _prevOutR;

            float magL = std::abs(diffL);
            float magR = std::abs(diffR);
            _envL = (magL > _envL) ? magL * _attack + _envL * (1 - _attack) : _envL * _release;
            _envR = (magR > _envR) ? magR * _attack + _envR * (1 - _attack) : _envR * _release;

            outL += diffL * _edgeBlend;
            outR += diffR * _edgeBlend;
            outL += diffL * _envL * _transBoost;
            outR += diffR * _envR * _transBoost;

            _prevOutL = left;
            _prevOutR = right;
        }

        // Room simulation
        if (_roomEnabled)
        {
            _delayL[_delayIdx] = outL;
            _delayR[_delayIdx] = outR;

            int delayedIdx = (_delayIdx - _roomDelay + MAX_DELAY) % MAX_DELAY;
            float delayedL = _delayR[delayedIdx];
            float delayedR = _delayL[delayedIdx];

            _roomLpL += _roomLpCoef * (delayedL - _roomLpL);
            _roomLpR += _roomLpCoef * (delayedR - _roomLpR);

            outL += _roomLpL * _roomLevel;
            outR += _roomLpR * _roomLevel;

            _delayIdx = (_delayIdx + 1) % MAX_DELAY;
        }

        // Clamp and convert back to int16
        buffer[i * 2] = static_cast<int16_t>(std::clamp(outL * toInt16, -32768.0f, 32767.0f));
        buffer[i * 2 + 1] = static_cast<int16_t>(std::clamp(outR * toInt16, -32768.0f, 32767.0f));
    }
}

/// endregion </Processing>
