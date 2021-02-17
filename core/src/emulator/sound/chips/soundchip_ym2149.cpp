#include "stdafx.h"
#include "soundchip_ym2149.h"

/*

/// region <NoiseGenerator>

uint32_t SoundChip_YM2149::NoiseGenerator::reset()
{

}

/// Returns LSFR-generated random numbers for noise generator
/// \return Next random number from LSFR
///
/// <b>Information:</b>
/// The Random Number Generator of the YM2149 is a 17-bit shift register
/// 17 stage LSFR with 2 taps (17, 14)
/// Its characteristic polynomial is (x^17 + x^3 + 1), and linear span is 17.
///
/// Note: To get tap bit use the following snippet: lfsr.bit(n) => (lfsr >> n & 1)
///
/// See: https://en.wikipedia.org/wiki/Linear-feedback_shift_register
/// See: http://web.archive.org/web/20180419035811/http://www.newwaveinstruments.com/resources/articles/m_sequence_linear_feedback_shift_register_lfsr.htm
/// See: https://listengine.tuxfamily.org/lists.tuxfamily.org/hatari-devel/2012/09/msg00045.html
/// See: https://github.com/hatari/hatari/blob/master/src/sound.c
/// See: https://github.com/jotego/jt49/blob/master/hdl/jt49_noise.v - but seems bits 0 and 3 tapped, not 13, 16
uint32_t SoundChip_YM2149::NoiseGenerator::getNextRandom()
{
    _randomSeed ^= ((_randomseed >> 13) & 1) ^ ((_randomseed >> 16) & 1) << 17);
    _randomSeed >>= 1;

    return _randomSeed;
}

/// endregion </NoiseGenerator>

*/