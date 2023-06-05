#include "filter_unreal.h"

/// region <Constructors / destructors>

/// Initialize helper array in constructor
UnrealFilter::UnrealFilter()
{
    double sum = 0.0;

    // Calculate discrete time step response.
    // Step response is obtained by summing the impulse response values.
    // See: https://en.wikipedia.org/wiki/Step_response
    // See: https://lpsa.swarthmore.edu/Transient/TransInputs/TransStep.html
    for (size_t i = 0; i < FILTER_ARRAY_SIZE; i++)
    {
        _stepResponseCoefficients[i] = (size_t)(sum * 0x10000);
        sum += _oversamplingFIRCoefficients[i];
    }
}

/// endregion <Constructors / destructors>

/// region <Methods>
/// Applies timing parameters to following interpolation / decimation calculations
/// @param systemClockRate System / Z80 clock speed (in Hz)
/// @param audioChipClockRate  AY / other chip clock speed (in Hz)
/// @param outputSampleRate Output audio sample rate (in Hz)
void UnrealFilter::setTimings(size_t systemClockRate, size_t audioChipClockRate, size_t outputSampleRate)
{
    _systemClockRate = systemClockRate;
    _audioChipClockRate = audioChipClockRate;
    _outputSampleRate = outputSampleRate;
}

/// Interpolate samples between outputs using oversampling and FIR-based interpolation curve
/// @param startTick
/// @param endTick
/// @param left Left channel sample
/// @param right Right channel sample
/// @details Here is a step-by-step breakdown of what the code does:
///
/// 1.The function first checks if the endTick is in the same discrete range as the current tick value.
///   If it is, it performs a smooth transition from the current mix value to the new mix value by computing
///   the difference between the filter values at the endTick and the current tick, and then adding the scaled
///   difference to the sound buffer. The tick value is then updated to the endTick value.
///
/// 2.If the endTick is not in the same discrete range as the current tick, it computes the difference between
///   the current tick and the next discrete range boundary and adds the scaled difference to the sound buffer.
///   The tick value is then updated to the next discrete range boundary.
///
/// 3.It then checks if the endTick is still not in the same discrete range as the current tick value. If it is,
///   it loops through the remaining discrete ranges between the current tick and the endTick and adds the scaled
///   value to the sound buffer for each range. The tick value is updated to the endTick value.
///
/// 4.Finally, it computes the difference between the filter value at the endTick and the last discrete range
///   boundary and adds the scaled difference to the sound buffer. The tick value is then updated to the endTick value.
///
/// In summary, the function updates the sound buffer with scaled samples based on the difference between the current
/// and endTick values, while making sure to smoothly transition between discrete ranges to prevent audio artifacts.
void UnrealFilter::interpolate(uint32_t startTick, uint32_t endTick, uint32_t left, uint32_t right)
{
    uint32_t tick = startTick;
    uint32_t scale;

    // Same as: !((startTick ^ endTick) % OVERSAMPLING_FACTOR)
    // Same as: (startTick % OVERSAMPLING_FACTOR) != (endTick % OVERSAMPLING_FACTOR)
    if (!((tick ^ endTick) & ~OVERSAMPLING_FACTOR_BITMASK))  // Input signal changed faster than single sample of output audio signal
    {
        // s1 - output signal sample at left decimation node
        // s2 - output signal sample at right decimation node
        // left/right - input signal sample within [startTick.. endTick]

        // Apply coefficients from the second FIR sum table half
        size_t startIndex = (tick & OVERSAMPLING_FACTOR_BITMASK) + OVERSAMPLING_FACTOR;
        size_t endIndex = (endTick & OVERSAMPLING_FACTOR_BITMASK) + OVERSAMPLING_FACTOR;

        // Right decimation node calculated from FIR transfer characteristic
        scale = _stepResponseCoefficients[endIndex] - _stepResponseCoefficients[startIndex];
        s2_l = left * scale;
        s2_r = right * scale;

        // Apply coefficients from the first FIR sum table half
        startIndex = tick & OVERSAMPLING_FACTOR_BITMASK;
        endIndex = endTick & OVERSAMPLING_FACTOR_BITMASK;

        // Left decimation point calculated from FIR transfer characteristic
        scale = _stepResponseCoefficients[endIndex] - _stepResponseCoefficients[startIndex];
        s1_l = left * scale;
        s1_r = right * scale;
    }
    else
    {
        // If startTick and endTick differs within oversampling window
        // We need to interpolate between samples
        size_t index = (tick & OVERSAMPLING_FACTOR_BITMASK) + OVERSAMPLING_FACTOR;
        scale = _filterSumFullUnsigned - _stepResponseCoefficients[index];

        uint16_t outputLeft = left * scale + s2_l;
        uint16_t outputRight = right * scale + s2_r;
        // TODO: write to output buffer

        index = tick & OVERSAMPLING_FACTOR_BITMASK;
        scale = _filterSumHalfUnsigned - _stepResponseCoefficients[index];
        s2_l = s1_l + left * scale;
        s2_r = s1_r + right * scale;

        // Round up startTick to next OVERSAMPLING_FACTOR multiplier
        tick = (tick | OVERSAMPLING_FACTOR_BITMASK) + 1;

        // If we still within oversampling window - interpolation is required
        if (((tick ^ endTick) & ~OVERSAMPLING_FACTOR_BITMASK))
        {
            uint32_t val_l = left * _filterSumHalfUnsigned;
            uint32_t val_r = right * _filterSumHalfUnsigned;

            // Perform interpolation until meet oversampling window end
            do
            {
                uint16_t outputLeft = s2_l + val_l;
                uint16_t outputRight = s2_r + val_r;
                // TODO: write to output buffer

                tick += OVERSAMPLING_FACTOR;
                s2_l = val_l;
                s2_r = val_r;
            }
            while ((endTick ^ tick) & ~OVERSAMPLING_FACTOR_BITMASK); // Same as while (endTick % OVERSAMPLING_FACTOR_BITMASK == tick % OVERSAMPLING_FACTOR_BITMASK)
        }

        tick = endTick;

        index = (endTick & OVERSAMPLING_FACTOR_BITMASK) + OVERSAMPLING_FACTOR;
        scale = _stepResponseCoefficients[index] - _filterSumHalfUnsigned;
        s2_l += left * scale;
        s2_r += right * scale;

        index = endTick & OVERSAMPLING_FACTOR_BITMASK;
        scale = _stepResponseCoefficients[index];
        s1_l = left * scale;
        s1_r = right * scale;
    }
}

void UnrealFilter::applyFilter(uint16_t* input, uint16_t* output, size_t samplesLen)
{
    static double state[FILTER_ARRAY_SIZE] = { 0 };

    for (size_t i = 0; i < samplesLen; i++)
    {
        double sampleResult = 0.0;

    }
}

/// endregion </Methods>

