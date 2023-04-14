#pragma once

#include <stdint.h>
#include <iostream>
#include <cmath>
#include <complex>

#define __USE_SQUARE_BRACKETS_FOR_ELEMENT_ACCESS_OPERATOR
#include <3rdparty/simple-fft/include/simple_fft/fft_settings.h>
#include <3rdparty/simple-fft/include/simple_fft/fft.h>

typedef std::vector<float> AudioSamplesArray;
typedef std::vector<complex_type> ComplexArray1D;

class AudioHelper
{
public:
    static uint32_t detectBaseFrequencyFFT(AudioSamplesArray samples, uint32_t sampleRate);
    static uint32_t detectBaseFrequencyZeroCross(AudioSamplesArray samples, uint32_t sampleRate);
};