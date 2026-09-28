#include "stdafx.h"

#include "zxpolyscreencomposer.h"

namespace
{
    constexpr unsigned WIDTH = ZXPolyScreenComposer::OUT_WIDTH;

    inline uint8_t InkIndex(uint8_t attr, bool flashPhase)
    {
        const uint8_t bright = (attr & 0x40u) ? 0x08u : 0x00u;
        const uint8_t ink = static_cast<uint8_t>((attr & 0x07u) | bright);
        const uint8_t paper = static_cast<uint8_t>(((attr >> 3) & 0x07u) | bright);
        return ((attr & 0x80u) && flashPhase) ? paper : ink;
    }

    inline uint8_t PaperIndex(uint8_t attr, bool flashPhase)
    {
        const uint8_t bright = (attr & 0x40u) ? 0x08u : 0x00u;
        const uint8_t ink = static_cast<uint8_t>((attr & 0x07u) | bright);
        const uint8_t paper = static_cast<uint8_t>(((attr >> 3) & 0x07u) | bright);
        return ((attr & 0x80u) && flashPhase) ? ink : paper;
    }

    inline void Put2x2(uint32_t* out, unsigned x, unsigned y, uint32_t color)
    {
        uint32_t* p = out + (y * 2) * WIDTH + x * 2;
        p[0] = color;
        p[1] = color;
        p[WIDTH] = color;
        p[WIDTH + 1] = color;
    }

    inline void PutQuad(uint32_t* out, unsigned x, unsigned y, uint32_t tl, uint32_t tr, uint32_t bl, uint32_t br)
    {
        uint32_t* p = out + (y * 2) * WIDTH + x * 2;
        p[0] = tl;
        p[1] = tr;
        p[WIDTH] = bl;
        p[WIDTH + 1] = br;
    }
}

void ZXPolyScreenComposer::Compose(const std::array<const uint8_t*, MODULES>& vram, uint8_t mode, bool flashPhase,
                                   const uint32_t* palette, uint32_t* out)
{
    mode &= 0x07u;

    for (unsigned y = 0; y < 192; y++)
    {
        for (unsigned x = 0; x < 256; x++)
        {
            const size_t bitmap = BitmapOffset(x, y);
            const size_t attribute = AttributeOffset(x, y);
            const uint8_t mask = static_cast<uint8_t>(0x80u >> (x & 7u));

            bool bit[MODULES];
            for (size_t m = 0; m < MODULES; m++)
                bit[m] = (vram[m][bitmap] & mask) != 0;

            const uint8_t polyIndex = static_cast<uint8_t>((bit[3] ? 0x08u : 0u) | (bit[0] ? 0x04u : 0u) |
                                                           (bit[1] ? 0x02u : 0u) | (bit[2] ? 0x01u : 0u));

            switch (mode)
            {
                case 0:
                case 1:
                case 2:
                case 3:
                {
                    const uint8_t attr = vram[mode][attribute];
                    const uint8_t index = bit[mode] ? InkIndex(attr, flashPhase) : PaperIndex(attr, flashPhase);
                    Put2x2(out, x, y, palette[index]);
                    break;
                }

                case 4:
                    Put2x2(out, x, y, palette[polyIndex]);
                    break;

                case 5:
                {
                    uint32_t c[MODULES];
                    for (size_t m = 0; m < MODULES; m++)
                    {
                        const uint8_t attr = vram[m][attribute];
                        c[m] = palette[bit[m] ? InkIndex(attr, flashPhase) : PaperIndex(attr, flashPhase)];
                    }
                    PutQuad(out, x, y, c[0], c[1], c[2], c[3]);
                    break;
                }

                case 6:
                {
                    const uint8_t attr = vram[0][attribute];
                    const uint8_t ink = InkIndex(attr, flashPhase);
                    const uint8_t paper = PaperIndex(attr, flashPhase);
                    Put2x2(out, x, y, palette[ink == paper ? ink : polyIndex]);
                    break;
                }

                case 7:
                default:
                {
                    const uint8_t attr = vram[0][attribute];
                    const uint8_t ink = InkIndex(attr, false);
                    const uint8_t paper = PaperIndex(attr, false);
                    if ((attr & 0x80u) == 0)
                    {
                        const uint32_t inkColor = palette[ink];
                        const uint32_t paperColor = palette[paper];
                        PutQuad(out, x, y, bit[0] ? inkColor : paperColor, bit[1] ? inkColor : paperColor,
                                bit[2] ? inkColor : paperColor, bit[3] ? inkColor : paperColor);
                    }
                    else
                    {
                        Put2x2(out, x, y, palette[ink == paper ? ink : polyIndex]);
                    }
                    break;
                }
            }
        }
    }
}
