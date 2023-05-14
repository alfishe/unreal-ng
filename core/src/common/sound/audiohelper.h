#pragma once

#include <stdint.h>
#include <iostream>
#include <cmath>
#include <complex>

#define __USE_SQUARE_BRACKETS_FOR_ELEMENT_ACCESS_OPERATOR
#include <3rdparty/simple-fft/include/simple_fft/fft_settings.h>
#include <3rdparty/simple-fft/include/simple_fft/fft.h>
#include "common/stringhelper.h"

typedef std::vector<float> AudioSamplesArray;
typedef std::vector<complex_type> ComplexArray1D;

class AudioHelper
{
public:
    static uint32_t detectBaseFrequencyFFT(AudioSamplesArray samples, uint32_t sampleRate);
    static uint32_t detectBaseFrequencyZeroCross(AudioSamplesArray samples, uint32_t sampleRate);

    static bool convertInt16ToFloat(const int16_t* source, float* destination, size_t sampleLen);

    /// region <Digital filters>
public:
    static void filterDCRejectionMono(int16_t* const buffer, size_t samplesLen);
    static void filterDCRejectionStereoInterleaved(int16_t* const buffer, size_t samplesLen);
    /// endregion </Digital filters>

    /// region <Debug methods>
public:
    template <typename T>
    static std::string dumpInterleavedSamples(T* samples, size_t sampleLen)
    {
        std::stringstream ss;

        for (int i = 0; i < sampleLen; i += 2)
        {
            const T left = samples[i];
            const T right = samples[i + 1];

            ss << StringHelper::Format("[%03d] L: %6s R: %6s", i / 2, std::to_string(left).c_str(), std::to_string(right).c_str()) << std::endl;
        }

        return ss.str();
    }
    /// endregion </Debug methods>
};