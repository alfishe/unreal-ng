#pragma once
#include "stdafx.h"

/// region <Info>

// See: https://worldofspectrum.org/faq/reference/formats.htm

/// endregion </Info>

/// region <Types>

struct SNAHeader
{
    uint8_t reg_I;

    uint16_t reg_HL1;
    uint16_t reg_DE1;
    uint16_t reg_BC1;
    uint16_t reg_AF1;

    uint16_t reg_HL;
    uint16_t reg_DE;
    uint16_t reg_BC;
    uint16_t reg_IY;
    uint16_t reg_IX;

    uint8_t reg_R;
    uint16_t reg_AF;
    uint16_t reg_SP;
    uint8_t intMode;
    uint8_t borderColor;

    uint8_t ramDump48k[49152];
};

struct SNA128Header : public SNAHeader
{
    uint16_t reg_PC;
    uint8_t port_7FFD;  // Determines current bank in #C000 - #FFFF bank
    uint8_t is_TRDOS;

    uint16_t
};

/// endregion </Types>

class LoaderSNA
{

};
