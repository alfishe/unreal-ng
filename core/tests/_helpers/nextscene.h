#pragma once

/// @file nextscene.h
/// @brief A fixed pseudo-random Next picture for the renderer tests: random RAM and palettes (an eighth of the colours is the global
/// transparency colour), Layer 2 / the tilemap / the sprites clipped to overlapping regions of the grid, so that every layer wins
/// somewhere; and the list of layer settings (the six layer orders, the ULA clip, tilemap above / text / LoRes, stencil, the blend
/// modes) a test sweeps through. Header only and independent of the layer-capture sink, so the same code runs against an older
/// renderer to produce the digest a change has to keep.

#include <cstdint>
#include <random>
#include <vector>

#include "emulator/io/z80n/nextsprites.h"
#include "emulator/io/z80n/nextvideoregs.h"
#include "emulator/video/next/nextvideorenderer.h"

struct NextSceneConfig
{
    const char* name;
    uint8_t nr15;
    uint8_t nr68;
    uint8_t nr6B;
    bool ulaClip;
};

struct NextRandomScene
{
    static constexpr unsigned kPages = 16;
    std::vector<uint8_t> ram = std::vector<uint8_t>(kPages * 0x4000, 0);
    NextVideoRegs regs;
    NextSprites sprites;
    NextVideoInputs in;
    std::mt19937 random{20261010};

    NextRandomScene()
    {
        regs.Reset();
        sprites.Reset();
        in.ram = ram.data();
        in.ramPages = kPages;
        in.regs = &regs;
        in.sprites = &sprites;
        in.nr[0x14] = 0xE3;
        in.nr[0x4A] = 0xE3;
        in.nr[0x12] = 8;
        in.nr[0x42] = 0x07;
        Build();
    }

    void Build()
    {
        for (uint8_t& b : ram)
            b = static_cast<uint8_t>(random());
        for (unsigned palette = 0; palette < NextVideoRegs::kPalettes; palette++)
        {
            regs.WritePaletteControl(static_cast<uint8_t>(palette << 4));
            regs.WritePaletteIndex(0);
            for (unsigned i = 0; i < 256; i++)
            {
                uint8_t high = static_cast<uint8_t>(random());
                if ((random() & 7) == 0)
                    high = 0xE3;  // the global transparency colour
                regs.WritePaletteValue9(high);
                regs.WritePaletteValue9(static_cast<uint8_t>((random() & 1) | ((random() & 15) == 0 ? 0x80 : 0)));
            }
        }
        regs.WritePaletteControl(0);
        regs.WriteClipControl(0x0F);
        for (uint8_t v : {20, 220, 10, 150})  // Layer 2 (256 x 192): x 20-220, y 10-150
            regs.WriteClip(0, v);
        for (uint8_t v : {0, 255, 0, 255})  // sprites
            regs.WriteClip(1, v);
        for (uint8_t v : {0, 255, 0, 191})  // ULA: the whole paper
            regs.WriteClip(2, v);
        for (uint8_t v : {30, 100, 30, 200})  // tilemap: x 60-201, y 30-200
            regs.WriteClip(3, v);
        in.layer2Enable = true;
        in.nr[0x6B] = 0x80;  // tilemap on, 40 columns, 16-bit entries
        in.nr[0x6E] = 0x20;  // the map at #2000 of bank 5
        in.nr[0x6F] = 0x30;  // the tile definitions at #3000
        in.nr[0x4C] = 0x0F;
        in.nr[0x4B] = 0x00;
        in.nr[0x70] = 0x00;  // Layer 2 256 x 192
        in.border = 3;
        for (unsigned s = 0; s < 24; s++)
        {
            sprites.WriteSlotSelect(static_cast<uint8_t>(s));
            const unsigned x = random() % 300, y = random() % 240;
            sprites.WriteAttribute(static_cast<uint8_t>(x));
            sprites.WriteAttribute(static_cast<uint8_t>(y));
            sprites.WriteAttribute(static_cast<uint8_t>(((x >> 8) & 1) | ((random() & 15) << 4)));
            sprites.WriteAttribute(static_cast<uint8_t>(0x80 | (random() & 0x3F)));
        }
        sprites.WriteSlotSelect(0);
        for (unsigned i = 0; i < NextSprites::kPatternBytes; i++)
            sprites.WritePattern(static_cast<uint8_t>((random() & 3) == 0 ? 0 : random()));  // a quarter of the bytes is the transparent index
    }

    static const std::vector<unsigned>& Lines()
    {
        static const std::vector<unsigned> lines = {0, 12, 31, 32, 40, 63, 64, 80, 100, 130, 160, 200, 223, 224, 231, 240, 255};
        return lines;
    }

    static const std::vector<NextSceneConfig>& Configs()
    {
        static const std::vector<NextSceneConfig> configs = [] {
            std::vector<NextSceneConfig> list;
            for (unsigned order = 0; order < 6; order++)
                list.push_back({"order", static_cast<uint8_t>(0x03 | (order << 2)), 0x00, 0x80, false});
            for (unsigned order = 0; order < 6; order++)
                list.push_back({"ula clip", static_cast<uint8_t>(0x03 | (order << 2)), 0x00, 0x80, true});
            list.push_back({"tile above the ula", 0x03, 0x00, 0x81, false});
            list.push_back({"text mode tiles", 0x03 | (3 << 2), 0x00, 0x88, false});
            list.push_back({"lores", 0x83, 0x00, 0x80, false});
            for (unsigned order : {0u, 3u})
                list.push_back({"stencil", static_cast<uint8_t>(0x03 | (order << 2)), 0x01, 0x80, false});
            for (unsigned order : {6u, 7u})
                for (unsigned blend = 0; blend < 4; blend++)
                    list.push_back({"blend", static_cast<uint8_t>(0x03 | (order << 2)), static_cast<uint8_t>(blend << 5), 0x80, false});
            list.push_back({"blend with stencil", 0x03 | (6 << 2), 0x01 | (2 << 5), 0x80, false});
            list.push_back({"no ula", 0x03 | (1 << 2), 0x80, 0x80, false});
            return list;
        }();
        return configs;
    }

    void Apply(const NextSceneConfig& config)
    {
        in.nr[0x15] = config.nr15;
        in.nr[0x68] = config.nr68;
        in.nr[0x6B] = config.nr6B;
        regs.WriteClipControl(0x04);
        const uint8_t clip[4] = {40, 200, 20, 170};
        const uint8_t full[4] = {0, 255, 0, 191};
        for (unsigned i = 0; i < 4; i++)
            regs.WriteClip(2, config.ulaClip ? clip[i] : full[i]);
    }

    /// FNV-1a over the composed rows of every configuration and line, drawn without a layer sink
    uint64_t Digest()
    {
        uint64_t hash = 1469598103934665603ull;
        std::vector<uint32_t> row(NextVideoRenderer::kWidth);
        for (const NextSceneConfig& config : Configs())
        {
            Apply(config);
            for (unsigned y : Lines())
            {
                NextVideoRenderer::RenderLine(in, y, row.data());
                for (uint32_t v : row)
                {
                    hash ^= v;
                    hash *= 1099511628211ull;
                }
            }
        }
        return hash;
    }
};
