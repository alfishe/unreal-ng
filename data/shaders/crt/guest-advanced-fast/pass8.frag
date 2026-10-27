#version 330

const float _1763[15] = float[](2.0, 3.0, 3.0, 6.0, 6.0, 2.400000095367431640625, 3.400000095367431640625, 2.400000095367431640625, 3.25, 3.400000095367431640625, 4.400000095367431640625, 4.25, 7.400000095367431640625, 6.25, 5.25);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_bloom;
uniform float LIBRA_UNIFORM_halation;
uniform float LIBRA_UNIFORM_slotms;
uniform float LIBRA_UNIFORM_mask_gamma;
uniform float LIBRA_UNIFORM_gamma_out;
uniform float LIBRA_UNIFORM_overscanX;
uniform float LIBRA_UNIFORM_overscanY;
uniform float LIBRA_UNIFORM_VShift;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_prescalex;
uniform float LIBRA_UNIFORM_c_shape;
uniform float LIBRA_UNIFORM_barspeed;
uniform float LIBRA_UNIFORM_bardir;
uniform float LIBRA_UNIFORM_sborder;
uniform float LIBRA_UNIFORM_bloom_dist;
uniform float LIBRA_UNIFORM_deconr;
uniform float LIBRA_UNIFORM_decons;
uniform float LIBRA_UNIFORM_deconrr;
uniform float LIBRA_UNIFORM_deconrg;
uniform float LIBRA_UNIFORM_deconrb;
uniform float LIBRA_UNIFORM_deconrry;
uniform float LIBRA_UNIFORM_deconrgy;
uniform float LIBRA_UNIFORM_deconrby;
uniform float LIBRA_UNIFORM_dctypex;
uniform float LIBRA_UNIFORM_dctypey;
uniform float LIBRA_UNIFORM_post_br;
uniform float LIBRA_UNIFORM_noisetype;
uniform float LIBRA_UNIFORM_maskboost;
uniform float LIBRA_UNIFORM_smoothmask;
uniform float LIBRA_UNIFORM_gamma_c;
uniform float LIBRA_UNIFORM_gamma_c2;
uniform float LIBRA_UNIFORM_smask_mit;
uniform float LIBRA_UNIFORM_mask_zoom;
uniform float LIBRA_UNIFORM_no_scanlines;
uniform float LIBRA_UNIFORM_bmask;
uniform float LIBRA_UNIFORM_bmask1;
uniform float LIBRA_UNIFORM_hmask1;
uniform float LIBRA_UNIFORM_mzoom_sh;
uniform float LIBRA_UNIFORM_mclip;
uniform float LIBRA_UNIFORM_edgemask;
uniform float LIBRA_UNIFORM_oimage;
uniform float LIBRA_UNIFORM_interm;

uniform float LIBRA_UNIFORM_IOS;
uniform float LIBRA_UNIFORM_brightboost;
uniform float LIBRA_UNIFORM_brightboost1;
uniform float LIBRA_UNIFORM_csize;
uniform float LIBRA_UNIFORM_bsize1;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_shadowMask;
uniform float LIBRA_UNIFORM_masksize;
uniform float LIBRA_UNIFORM_hiscan;
uniform float LIBRA_UNIFORM_slotmask;
uniform float LIBRA_UNIFORM_slotmask1;
uniform float LIBRA_UNIFORM_slotwidth;
uniform float LIBRA_UNIFORM_double_slot;
uniform float LIBRA_UNIFORM_mcut;
uniform float LIBRA_UNIFORM_maskDark;
uniform float LIBRA_UNIFORM_maskLight;
uniform float LIBRA_UNIFORM_maskstr;
uniform float LIBRA_UNIFORM_addnoised;
uniform float LIBRA_UNIFORM_noiseresd;
uniform float LIBRA_UNIFORM_barintensity;
uniform float LIBRA_UNIFORM_mshift;
uniform float LIBRA_UNIFORM_mask_layout;
uniform float LIBRA_UNIFORM_mask_bloom;
uniform float LIBRA_UNIFORM_pr_scan;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_BloomPass;
uniform sampler2D LIBRA_TEXTURE_LinearizePass;
uniform sampler2D LIBRA_TEXTURE_PrePass;
uniform sampler2D LIBRA_TEXTURE_StockPass;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 Overscan(inout vec2 pos, float dx, float dy)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos *= vec2(dx, dy);
    return (pos * 0.5) + vec2(0.5);
}

vec2 Warp(inout vec2 pos)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos = mix(pos, vec2(pos.x * inversesqrt(1.0 - ((LIBRA_UNIFORM_c_shape * pos.y) * pos.y)), pos.y * inversesqrt(1.0 - ((LIBRA_UNIFORM_c_shape * pos.x) * pos.x))), vec2(LIBRA_UNIFORM_warpX, LIBRA_UNIFORM_warpY) / vec2(LIBRA_UNIFORM_c_shape));
    return (pos * 0.5) + vec2(0.5);
}

void fetch_pixel(inout vec3 c, inout vec3 b, vec2 coord, vec2 bcoord)
{
    float stepx = LIBRA_UNIFORM_OutputSize.z;
    float stepy = LIBRA_UNIFORM_OutputSize.w;
    float ds = LIBRA_UNIFORM_decons;
    vec2 dx = vec2(stepx, 0.0);
    vec2 dy = vec2(0.0, stepy);
    float posx = (2.0 * coord.x) - 1.0;
    float posy = (2.0 * coord.y) - 1.0;
    if (LIBRA_UNIFORM_dctypex > 0.02500000037252902984619140625)
    {
        posx = sign(posx) * pow(abs(posx), 1.0499999523162841796875 - LIBRA_UNIFORM_dctypex);
        dx *= posx;
    }
    if (LIBRA_UNIFORM_dctypey > 0.02500000037252902984619140625)
    {
        posy = sign(posy) * pow(abs(posy), 1.0499999523162841796875 - LIBRA_UNIFORM_dctypey);
        dy *= posy;
    }
    vec2 rc = (dx * LIBRA_UNIFORM_deconrr) + (dy * LIBRA_UNIFORM_deconrry);
    vec2 gc = (dx * LIBRA_UNIFORM_deconrg) + (dy * LIBRA_UNIFORM_deconrgy);
    vec2 bc = (dx * LIBRA_UNIFORM_deconrb) + (dy * LIBRA_UNIFORM_deconrby);
    float r1 = texture(LIBRA_TEXTURE_Source, coord + rc).x;
    float g1 = texture(LIBRA_TEXTURE_Source, coord + gc).y;
    float b1 = texture(LIBRA_TEXTURE_Source, coord + bc).z;
    vec3 d = vec3(r1, g1, b1);
    c = clamp(mix(c, d, vec3(ds)), vec3(0.0), vec3(1.0));
    r1 = texture(LIBRA_TEXTURE_BloomPass, bcoord + rc).x;
    g1 = texture(LIBRA_TEXTURE_BloomPass, bcoord + gc).y;
    b1 = texture(LIBRA_TEXTURE_BloomPass, bcoord + bc).z;
    d = vec3(r1, g1, b1);
    b = mix(b, d, vec3(min(ds, 1.0)));
}

float igc(float mc)
{
    return pow(mc, LIBRA_UNIFORM_gamma_c);
}

vec3 Mask(inout vec2 pos, float mx, float mb)
{
    vec3 mask = vec3(LIBRA_UNIFORM_maskDark, LIBRA_UNIFORM_maskDark, LIBRA_UNIFORM_maskDark);
    vec3 one = vec3(1.0);
    if (LIBRA_UNIFORM_shadowMask == 0.0)
    {
        float mc = 1.0 - max(LIBRA_UNIFORM_maskstr, 0.0);
        pos.x = fract(pos.x * 0.5);
        if (pos.x < 0.4900000095367431640625)
        {
            mask.x = 1.0;
            mask.y = mc;
            mask.z = 1.0;
        }
        else
        {
            mask.x = mc;
            mask.y = 1.0;
            mask.z = mc;
        }
    }
    else
    {
        if (LIBRA_UNIFORM_shadowMask == 1.0)
        {
            float line = LIBRA_UNIFORM_maskLight;
            float odd = 0.0;
            if (fract(pos.x / 6.0) < 0.4900000095367431640625)
            {
                odd = 1.0;
            }
            if (fract((pos.y + odd) / 2.0) < 0.4900000095367431640625)
            {
                line = LIBRA_UNIFORM_maskDark;
            }
            pos.x = floor(mod(pos.x, 3.0));
            if (pos.x < 0.5)
            {
                mask.x = LIBRA_UNIFORM_maskLight;
            }
            else
            {
                if (pos.x < 1.5)
                {
                    mask.y = LIBRA_UNIFORM_maskLight;
                }
                else
                {
                    mask.z = LIBRA_UNIFORM_maskLight;
                }
            }
            mask *= line;
        }
        else
        {
            if (LIBRA_UNIFORM_shadowMask == 2.0)
            {
                pos.x = floor(mod(pos.x, 3.0));
                if (pos.x < 0.5)
                {
                    mask.x = LIBRA_UNIFORM_maskLight;
                }
                else
                {
                    if (pos.x < 1.5)
                    {
                        mask.y = LIBRA_UNIFORM_maskLight;
                    }
                    else
                    {
                        mask.z = LIBRA_UNIFORM_maskLight;
                    }
                }
            }
            else
            {
                if (LIBRA_UNIFORM_shadowMask == 3.0)
                {
                    pos.x += (pos.y * 3.0);
                    pos.x = fract(pos.x / 6.0);
                    if (pos.x < 0.300000011920928955078125)
                    {
                        mask.x = LIBRA_UNIFORM_maskLight;
                    }
                    else
                    {
                        if (pos.x < 0.60000002384185791015625)
                        {
                            mask.y = LIBRA_UNIFORM_maskLight;
                        }
                        else
                        {
                            mask.z = LIBRA_UNIFORM_maskLight;
                        }
                    }
                }
                else
                {
                    if (LIBRA_UNIFORM_shadowMask == 4.0)
                    {
                        pos = floor(pos * vec2(1.0, 0.5));
                        pos.x += (pos.y * 3.0);
                        pos.x = fract(pos.x / 6.0);
                        if (pos.x < 0.300000011920928955078125)
                        {
                            mask.x = LIBRA_UNIFORM_maskLight;
                        }
                        else
                        {
                            if (pos.x < 0.60000002384185791015625)
                            {
                                mask.y = LIBRA_UNIFORM_maskLight;
                            }
                            else
                            {
                                mask.z = LIBRA_UNIFORM_maskLight;
                            }
                        }
                    }
                    else
                    {
                        if (LIBRA_UNIFORM_shadowMask == 5.0)
                        {
                            mask = vec3(0.0);
                            pos.x = fract(pos.x / 2.0);
                            if (pos.x < 0.4900000095367431640625)
                            {
                                mask.x = 1.0;
                                mask.z = 1.0;
                            }
                            else
                            {
                                mask.y = 1.0;
                            }
                            mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                        }
                        else
                        {
                            if (LIBRA_UNIFORM_shadowMask == 6.0)
                            {
                                mask = vec3(0.0);
                                pos.x = floor(mod(pos.x, 3.0));
                                if (pos.x < 0.5)
                                {
                                    mask.x = 1.0;
                                }
                                else
                                {
                                    if (pos.x < 1.5)
                                    {
                                        mask.y = 1.0;
                                    }
                                    else
                                    {
                                        mask.z = 1.0;
                                    }
                                }
                                mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                            }
                            else
                            {
                                if (LIBRA_UNIFORM_shadowMask == 7.0)
                                {
                                    mask = vec3(0.0);
                                    pos.x = fract(pos.x / 2.0);
                                    if (pos.x < 0.4900000095367431640625)
                                    {
                                        mask = vec3(0.0);
                                    }
                                    else
                                    {
                                        mask = vec3(1.0);
                                    }
                                    mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                }
                                else
                                {
                                    if (LIBRA_UNIFORM_shadowMask == 8.0)
                                    {
                                        mask = vec3(0.0);
                                        pos.x = fract(pos.x / 3.0);
                                        if (pos.x < 0.300000011920928955078125)
                                        {
                                            mask = vec3(0.0);
                                        }
                                        else
                                        {
                                            if (pos.x < 0.60000002384185791015625)
                                            {
                                                mask = vec3(1.0);
                                            }
                                            else
                                            {
                                                mask = vec3(1.0);
                                            }
                                        }
                                        mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                    }
                                    else
                                    {
                                        if (LIBRA_UNIFORM_shadowMask == 9.0)
                                        {
                                            mask = vec3(0.0);
                                            pos.x = fract(pos.x / 3.0);
                                            if (pos.x < 0.300000011920928955078125)
                                            {
                                                mask = vec3(0.0);
                                            }
                                            else
                                            {
                                                if (pos.x < 0.60000002384185791015625)
                                                {
                                                    mask.x = 1.0;
                                                    mask.z = 1.0;
                                                }
                                                else
                                                {
                                                    mask.y = 1.0;
                                                }
                                            }
                                            mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                        }
                                        else
                                        {
                                            if (LIBRA_UNIFORM_shadowMask == 10.0)
                                            {
                                                mask = vec3(0.0);
                                                pos.x = fract(pos.x * 0.25);
                                                if (pos.x < 0.20000000298023223876953125)
                                                {
                                                    mask = vec3(0.0);
                                                }
                                                else
                                                {
                                                    if (pos.x < 0.4000000059604644775390625)
                                                    {
                                                        mask.x = 1.0;
                                                    }
                                                    else
                                                    {
                                                        if (pos.x < 0.699999988079071044921875)
                                                        {
                                                            mask.y = 1.0;
                                                        }
                                                        else
                                                        {
                                                            mask.z = 1.0;
                                                        }
                                                    }
                                                }
                                                mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                            }
                                            else
                                            {
                                                if (LIBRA_UNIFORM_shadowMask == 11.0)
                                                {
                                                    mask = vec3(0.0);
                                                    pos.x = fract(pos.x * 0.25);
                                                    if (pos.x < 0.20000000298023223876953125)
                                                    {
                                                        mask.x = 1.0;
                                                    }
                                                    else
                                                    {
                                                        if (pos.x < 0.4000000059604644775390625)
                                                        {
                                                            mask.x = 1.0;
                                                            mask.y = 1.0;
                                                        }
                                                        else
                                                        {
                                                            if (pos.x < 0.699999988079071044921875)
                                                            {
                                                                mask.y = 1.0;
                                                                mask.z = 1.0;
                                                            }
                                                            else
                                                            {
                                                                mask.z = 1.0;
                                                            }
                                                        }
                                                    }
                                                    mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                                }
                                                else
                                                {
                                                    if (LIBRA_UNIFORM_shadowMask == 12.0)
                                                    {
                                                        mask = vec3(0.0);
                                                        pos.x = floor(mod(pos.x, 7.0));
                                                        if (pos.x < 0.5)
                                                        {
                                                            mask = vec3(0.0);
                                                        }
                                                        else
                                                        {
                                                            if (pos.x < 2.5)
                                                            {
                                                                mask.x = 1.0;
                                                            }
                                                            else
                                                            {
                                                                if (pos.x < 4.5)
                                                                {
                                                                    mask.y = 1.0;
                                                                }
                                                                else
                                                                {
                                                                    mask.z = 1.0;
                                                                }
                                                            }
                                                        }
                                                        mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                                    }
                                                    else
                                                    {
                                                        if (LIBRA_UNIFORM_shadowMask == 13.0)
                                                        {
                                                            mask = vec3(0.0);
                                                            pos.x = floor(mod(pos.x, 6.0));
                                                            if (pos.x < 0.5)
                                                            {
                                                                mask = vec3(0.0);
                                                            }
                                                            else
                                                            {
                                                                if (pos.x < 1.5)
                                                                {
                                                                    mask.x = 1.0;
                                                                }
                                                                else
                                                                {
                                                                    if (pos.x < 2.5)
                                                                    {
                                                                        mask.x = 1.0;
                                                                        mask.y = 1.0;
                                                                    }
                                                                    else
                                                                    {
                                                                        if (pos.x < 3.5)
                                                                        {
                                                                            mask = vec3(1.0);
                                                                        }
                                                                        else
                                                                        {
                                                                            if (pos.x < 4.5)
                                                                            {
                                                                                mask.y = 1.0;
                                                                                mask.z = 1.0;
                                                                            }
                                                                            else
                                                                            {
                                                                                mask.z = 1.0;
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                            mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                                        }
                                                        else
                                                        {
                                                            mask = vec3(0.0);
                                                            pos.x = floor(mod(pos.x, 5.0));
                                                            if (pos.x < 0.5)
                                                            {
                                                                mask = vec3(0.0);
                                                            }
                                                            else
                                                            {
                                                                if (pos.x < 1.5)
                                                                {
                                                                    mask.x = 1.0;
                                                                }
                                                                else
                                                                {
                                                                    if (pos.x < 2.5)
                                                                    {
                                                                        mask.x = 1.0;
                                                                        mask.y = 1.0;
                                                                    }
                                                                    else
                                                                    {
                                                                        if (pos.x < 3.5)
                                                                        {
                                                                            mask.y = 1.0;
                                                                            mask.z = 1.0;
                                                                        }
                                                                        else
                                                                        {
                                                                            mask.z = 1.0;
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                            mask = clamp(mix(mix(one, mask, vec3(LIBRA_UNIFORM_mcut)), mix(one, mask, vec3(LIBRA_UNIFORM_maskstr)), vec3(mx)), vec3(0.0), vec3(1.0));
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if (LIBRA_UNIFORM_mask_layout > 0.5)
    {
        mask = mask.xzy;
    }
    float maskmin = min(min(mask.x, mask.y), mask.z);
    return ((mask - vec3(maskmin)) * (1.0 + ((LIBRA_UNIFORM_maskboost - 1.0) * mb))) + vec3(maskmin);
}

float SlotMask(inout vec2 pos, float m, float swidth)
{
    if ((LIBRA_UNIFORM_slotmask + LIBRA_UNIFORM_slotmask1) == 0.0)
    {
        return 1.0;
    }
    else
    {
        pos.y = floor(pos.y / LIBRA_UNIFORM_slotms);
        float mlen = swidth * 2.0;
        float px = floor(mod(pos.x, 0.999989986419677734375 * mlen));
        float py = floor((fract(pos.y / (2.0 * LIBRA_UNIFORM_double_slot)) * 2.0) * LIBRA_UNIFORM_double_slot);
        float slot_dark = mix(1.0 - LIBRA_UNIFORM_slotmask1, 1.0 - LIBRA_UNIFORM_slotmask, m);
        float slot = 1.0;
        if ((py == 0.0) && (px < swidth))
        {
            slot = slot_dark;
        }
        else
        {
            if ((py == LIBRA_UNIFORM_double_slot) && (px >= swidth))
            {
                slot = slot_dark;
            }
        }
        return slot;
    }
}

vec3 plant(vec3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06;
    return (tar * r) / vec3(t);
}

vec3 gc2(vec3 c, float w3)
{
    float mc = max(max(c.x, c.y), c.z);
    float gp = 1.0 / (1.0 + ((LIBRA_UNIFORM_gamma_c2 - 1.0) * mix(0.375, 1.0, w3)));
    float mg = pow(mc, gp);
    return (c * mg) / vec3(mc + 1.0000000133514319600180897396058e-10);
}

vec3 _noise(inout vec3 v)
{
    if (LIBRA_UNIFORM_addnoised < 0.0)
    {
        v.z = -LIBRA_UNIFORM_addnoised;
    }
    else
    {
        v.z = mod(v.z, 6001.0) / 1753.0;
    }
    v = (fract(v) + fract(v * 10000.0)) + fract(v * 9.9999997473787516355514526367188e-05);
    v += vec3(0.123450003564357757568359375, 0.6789000034332275390625, 0.3141590058803558349609375);
    v = fract((v * dot(v, v)) * 123.45600128173828125);
    v = fract((v * dot(v, v)) * 123.45600128173828125);
    v = fract((v * dot(v, v)) * 123.45600128173828125);
    v = fract((v * dot(v, v)) * 123.45600128173828125);
    return v;
}

float humbar(inout float pos)
{
    if (LIBRA_UNIFORM_barintensity == 0.0)
    {
        return 1.0;
    }
    else
    {
        float _1002;
        if (LIBRA_UNIFORM_barintensity >= 0.0)
        {
            _1002 = pos;
        }
        else
        {
            _1002 = 1.0 - pos;
        }
        pos = _1002;
        pos = fract(pos + (mod(float(LIBRA_UNIFORM_FrameCount), LIBRA_UNIFORM_barspeed) / (LIBRA_UNIFORM_barspeed - 1.0)));
        float _1028;
        if (LIBRA_UNIFORM_barintensity < 0.0)
        {
            _1028 = pos;
        }
        else
        {
            _1028 = 1.0 - pos;
        }
        pos = _1028;
        return (1.0 - LIBRA_UNIFORM_barintensity) + (LIBRA_UNIFORM_barintensity * pos);
    }
}

float corner(inout vec2 pos)
{
    pos = abs((pos - vec2(0.5)) * 2.0);
    vec2 aspect = vec2(1.0, LIBRA_UNIFORM_OutputSize.x / LIBRA_UNIFORM_OutputSize.y);
    float b = (LIBRA_UNIFORM_bsize1 * 0.0500000007450580596923828125) + 0.0005000000237487256526947021484375;
    pos.y += (b * (aspect.y - 1.0));
    vec2 crn = max(vec2(LIBRA_UNIFORM_csize), vec2((2.0 * b) + 0.00150000001303851604461669921875));
    vec2 cp = max(pos - (vec2(1.0) - (crn * aspect)), vec2(0.0)) / aspect;
    float cd = sqrt(dot(cp, cp));
    pos = max(pos, (vec2(1.0) - crn) + vec2(cd));
    float res = mix(1.0, 0.0, smoothstep(1.0 - b, 1.0, sqrt(max(pos.x, pos.y))));
    return pow(res, LIBRA_UNIFORM_sborder);
}

void main()
{
    vec4 SourceSize = LIBRA_UNIFORM_OriginalSize;
    float gamma_in = 1.0 / texture(LIBRA_TEXTURE_LinearizePass, vec2(0.25)).w;
    float intera = texture(LIBRA_TEXTURE_LinearizePass, vec2(0.75, 0.25)).w;
    bool _1444 = intera < 0.3499999940395355224609375;
    bool _1451;
    if (_1444)
    {
        _1451 = LIBRA_UNIFORM_interm == 5.0;
    }
    else
    {
        _1451 = _1444;
    }
    bool inter6 = _1451;
    bool _1457 = (intera < 0.3499999940395355224609375) && (!inter6);
    bool _1465;
    if (!_1457)
    {
        _1465 = LIBRA_UNIFORM_no_scanlines > 0.02500000037252902984619140625;
    }
    else
    {
        _1465 = _1457;
    }
    bool _1472;
    if (_1465)
    {
        _1472 = LIBRA_UNIFORM_hiscan < 0.5;
    }
    else
    {
        _1472 = _1465;
    }
    bool interb = _1472;
    vec2 texcoord = LIBRA_VARYING_0;
    if ((LIBRA_UNIFORM_IOS > 0.0) && (!interb))
    {
        vec2 ofactor = LIBRA_UNIFORM_OutputSize.xy / LIBRA_UNIFORM_OriginalSize.xy;
        vec2 _1498;
        if (LIBRA_UNIFORM_IOS < 2.5)
        {
            _1498 = floor(ofactor);
        }
        else
        {
            _1498 = ceil(ofactor);
        }
        vec2 intfactor = _1498;
        vec2 diff = ofactor / intfactor;
        float scan = diff.y;
        vec2 param = texcoord;
        float param_1 = scan;
        float param_2 = scan;
        vec2 _1520 = Overscan(param, param_1, param_2);
        texcoord = _1520;
        bool _1523 = LIBRA_UNIFORM_IOS == 1.0;
        bool _1530;
        if (!_1523)
        {
            _1530 = LIBRA_UNIFORM_IOS == 3.0;
        }
        else
        {
            _1530 = _1523;
        }
        if (_1530)
        {
            texcoord = vec2(LIBRA_VARYING_0.x, texcoord.y);
        }
    }
    texcoord.y -= (LIBRA_UNIFORM_VShift * LIBRA_UNIFORM_OriginalSize.w);
    vec2 param_3 = texcoord;
    float param_4 = (LIBRA_UNIFORM_OriginalSize.x - LIBRA_UNIFORM_overscanX) / LIBRA_UNIFORM_OriginalSize.x;
    float param_5 = (LIBRA_UNIFORM_OriginalSize.y - LIBRA_UNIFORM_overscanY) / LIBRA_UNIFORM_OriginalSize.y;
    vec2 _1568 = Overscan(param_3, param_4, param_5);
    texcoord = _1568;
    vec2 pos1 = LIBRA_VARYING_0;
    vec2 param_6 = texcoord;
    vec2 _1574 = Warp(param_6);
    vec2 pos = _1574;
    vec2 param_7 = LIBRA_VARYING_0;
    vec2 _1578 = Warp(param_7);
    vec2 pos0 = _1578;
    vec2 posb = (pos0 - vec2(0.5)) * 2.0;
    posb = max(abs(posb), abs((pos - vec2(0.5)) * 2.0)) * sign(posb);
    posb = (posb * 0.5) + vec2(0.5);
    vec3 color = texture(LIBRA_TEXTURE_Source, pos1).xyz;
    vec3 Bloom = texture(LIBRA_TEXTURE_BloomPass, pos).xyz;
    if ((((((abs(LIBRA_UNIFORM_deconrr) + abs(LIBRA_UNIFORM_deconrry)) + abs(LIBRA_UNIFORM_deconrg)) + abs(LIBRA_UNIFORM_deconrgy)) + abs(LIBRA_UNIFORM_deconrb)) + abs(LIBRA_UNIFORM_deconrby)) > 0.20000000298023223876953125)
    {
        vec3 param_8 = color;
        vec3 param_9 = Bloom;
        vec2 param_10 = pos1;
        vec2 param_11 = pos;
        fetch_pixel(param_8, param_9, param_10, param_11);
        color = param_8;
        Bloom = param_9;
    }
    float param_12 = max(max(color.x, color.y), color.z);
    float cm = igc(param_12);
    float mx1 = texture(LIBRA_TEXTURE_Source, pos1).w;
    float colmx = max(mx1, cm);
    float w3 = min((max((cm - 0.0005000000237487256526947021484375) * 1.0004999637603759765625, 0.0) + 9.9999997473787516355514526367188e-05) / (colmx + 0.0005000000237487256526947021484375), 1.0);
    if (interb)
    {
        w3 = 1.0;
    }
    vec2 dx = vec2(0.001000000047497451305389404296875, 0.0);
    float mx0 = texture(LIBRA_TEXTURE_Source, pos1 - dx).w;
    float mx2 = texture(LIBRA_TEXTURE_Source, pos1 + dx).w;
    float mxg = max(max(mx0, mx1), max(mx2, cm));
    float mx = pow(mxg, 1.39999997615814208984375 / gamma_in);
    float cx = pow(colmx, 1.39999997615814208984375 / gamma_in);
    dx = vec2(LIBRA_UNIFORM_OriginalSize.z, 0.0) * 0.25;
    mx0 = texture(LIBRA_TEXTURE_Source, pos1 - dx).w;
    mx2 = texture(LIBRA_TEXTURE_Source, pos1 + dx).w;
    float mb = 1.0 - min(abs(mx0 - mx2) / (0.5 + mx1), 1.0);
    vec3 one = vec3(1.0);
    vec3 orig1 = color;
    vec3 cmask = one;
    vec3 cmask1 = one;
    vec3 cmask2 = one;
    float mwidth = _1763[int(LIBRA_UNIFORM_shadowMask)];
    float mask_compensate = fract(mwidth);
    float mwidth1 = mwidth;
    if (LIBRA_UNIFORM_shadowMask > (-0.5))
    {
        vec2 maskcoord = LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy;
        vec2 scoord = maskcoord;
        mwidth = floor(mwidth) * LIBRA_UNIFORM_masksize;
        float swidth = mwidth;
        bool zoomed = abs(LIBRA_UNIFORM_mask_zoom) > 0.75;
        float mscale = 1.0;
        vec2 maskcoord0 = maskcoord;
        maskcoord.y = floor(maskcoord.y / LIBRA_UNIFORM_masksize);
        mwidth1 = max(mwidth + LIBRA_UNIFORM_mask_zoom, 2.0);
        if (LIBRA_UNIFORM_mshift > 0.25)
        {
            float stagg_lvl = 1.0;
            if (fract(LIBRA_UNIFORM_mshift) > 0.25)
            {
                stagg_lvl = 2.0;
            }
            float next_line = float(floor(mod(maskcoord.y, 2.0 * stagg_lvl)) < stagg_lvl);
            maskcoord0.x += ((next_line * 0.5) * mwidth1);
        }
        maskcoord = maskcoord0 / vec2(LIBRA_UNIFORM_masksize);
        if (!zoomed)
        {
            vec2 param_13 = floor(maskcoord);
            float param_14 = mx;
            float param_15 = mb;
            vec3 _1864 = Mask(param_13, param_14, param_15);
            cmask = _1864;
        }
        else
        {
            mscale = mwidth1 / mwidth;
            float mlerp = fract(maskcoord.x / mscale);
            if (LIBRA_UNIFORM_mzoom_sh > 0.02500000037252902984619140625)
            {
                mlerp = clamp(((1.0 + LIBRA_UNIFORM_mzoom_sh) * mlerp) - (0.5 * LIBRA_UNIFORM_mzoom_sh), 0.0, 1.0);
            }
            float mcoord = floor(maskcoord.x / mscale);
            bool _1899 = LIBRA_UNIFORM_shadowMask == 12.0;
            bool _1906;
            if (_1899)
            {
                _1906 = LIBRA_UNIFORM_mask_zoom == (-2.0);
            }
            else
            {
                _1906 = _1899;
            }
            if (_1906)
            {
                mcoord = ceil(maskcoord.x / mscale);
            }
            vec2 param_16 = vec2(mcoord, maskcoord.y);
            float param_17 = mx;
            float param_18 = mb;
            vec3 _1923 = Mask(param_16, param_17, param_18);
            vec2 param_19 = vec2(mcoord + 1.0, maskcoord.y);
            float param_20 = mx;
            float param_21 = mb;
            vec3 _1934 = Mask(param_19, param_20, param_21);
            cmask = mix(_1923, _1934, vec3(mlerp));
        }
        float sm_offset = 0.0;
        bool _1942 = LIBRA_UNIFORM_shadowMask == 0.0;
        bool _1949;
        if (!_1942)
        {
            _1949 = LIBRA_UNIFORM_shadowMask == 2.0;
        }
        else
        {
            _1949 = _1942;
        }
        bool _1956;
        if (!_1949)
        {
            _1956 = LIBRA_UNIFORM_shadowMask == 5.0;
        }
        else
        {
            _1956 = _1949;
        }
        bool _1963;
        if (!_1956)
        {
            _1963 = LIBRA_UNIFORM_shadowMask == 6.0;
        }
        else
        {
            _1963 = _1956;
        }
        bool _1970;
        if (!_1963)
        {
            _1970 = LIBRA_UNIFORM_shadowMask == 8.0;
        }
        else
        {
            _1970 = _1963;
        }
        bool _1977;
        if (!_1970)
        {
            _1977 = LIBRA_UNIFORM_shadowMask == 11.0;
        }
        else
        {
            _1977 = _1970;
        }
        bool bsm_offset = _1977;
        if (zoomed)
        {
            if ((LIBRA_UNIFORM_mask_layout < 0.5) && bsm_offset)
            {
                sm_offset = 1.0;
            }
            else
            {
                if (bsm_offset)
                {
                    sm_offset = -1.0;
                }
            }
        }
        swidth = round(mwidth1);
        if (LIBRA_UNIFORM_slotwidth > 0.5)
        {
            swidth = LIBRA_UNIFORM_slotwidth;
        }
        float smask = 1.0;
        vec2 param_22 = scoord + vec2(sm_offset, 0.0);
        float param_23 = mx;
        float param_24 = swidth;
        float _2013 = SlotMask(param_22, param_23, param_24);
        smask = _2013;
        smask = clamp(smask + mix(LIBRA_UNIFORM_smask_mit, 0.0, w3 * pow(colmx, 0.300000011920928955078125)), 0.0, 1.0);
        cmask2 = cmask;
        cmask *= smask;
        cmask1 = cmask;
        if (abs(LIBRA_UNIFORM_mask_bloom) > 0.02500000037252902984619140625)
        {
            float maxbl = max(max(max(Bloom.x, Bloom.y), Bloom.z), mxg);
            maxbl *= max(mix(1.0, 2.0 - colmx, LIBRA_UNIFORM_bloom_dist), 0.0);
            if (LIBRA_UNIFORM_mask_bloom > 0.02500000037252902984619140625)
            {
                cmask = max(min(cmask + vec3(maxbl * LIBRA_UNIFORM_mask_bloom), vec3(1.0)), cmask);
            }
            else
            {
                vec3 param_25 = pow(Bloom, vec3(0.3499999940395355224609375));
                float param_26 = maxbl;
                cmask = max(mix(cmask, (cmask * (1.0 - (0.5 * maxbl))) + plant(param_25, param_26), vec3(-LIBRA_UNIFORM_mask_bloom)), cmask);
            }
        }
        color = pow(color, vec3(LIBRA_UNIFORM_mask_gamma / gamma_in));
        color *= cmask;
        color = min(color, vec3(1.0));
        color = pow(color, vec3(gamma_in / LIBRA_UNIFORM_mask_gamma));
        cmask = min(cmask, vec3(1.0));
        cmask1 = min(cmask1, vec3(1.0));
    }
    float dark_compensate = mix(max((clamp(mix(LIBRA_UNIFORM_mcut, LIBRA_UNIFORM_maskstr, mx), 0.0, 1.0) - 1.0) + mask_compensate, 0.0) + 1.0, 1.0, mx);
    if (LIBRA_UNIFORM_shadowMask < (-0.5))
    {
        dark_compensate = 1.0;
    }
    float bb = mix(LIBRA_UNIFORM_brightboost, LIBRA_UNIFORM_brightboost1, mx) * dark_compensate;
    color *= bb;
    vec3 Glow = texture(LIBRA_TEXTURE_BloomPass, pos).xyz;
    vec3 Ref = texture(LIBRA_TEXTURE_LinearizePass, pos).xyz;
    float maxb = texture(LIBRA_TEXTURE_BloomPass, pos).w;
    float vig = texture(LIBRA_TEXTURE_PrePass, clamp(pos, vec2(0.0) + (LIBRA_UNIFORM_OriginalSize.zw * 0.5), vec2(1.0) - (LIBRA_UNIFORM_OriginalSize.zw * 0.5))).w;
    if (LIBRA_UNIFORM_pr_scan > 0.02500000037252902984619140625)
    {
        float mbl = max(max(Bloom.x, Bloom.y), Bloom.z);
        vec3 param_27 = orig1;
        float param_28 = mbl;
        Bloom = mix(Bloom, mix(Bloom, plant(param_27, param_28), vec3(min(2.5 * (1.0 - w3), 1.0))), vec3(min(2.0 * LIBRA_UNIFORM_pr_scan, 1.0)));
    }
    vec3 Bloom1 = Bloom;
    vec3 bcmask = mix(one, cmask1, vec3(LIBRA_UNIFORM_bmask1));
    vec3 hcmask = mix(one, cmask1, vec3(LIBRA_UNIFORM_hmask1));
    if (abs(LIBRA_UNIFORM_bloom) > 0.02500000037252902984619140625)
    {
        if (LIBRA_UNIFORM_bloom < (-0.00999999977648258209228515625))
        {
            vec3 param_29 = Bloom;
            float param_30 = maxb;
            Bloom1 = plant(param_29, param_30);
        }
        Bloom1 = min(Bloom1 * (orig1 + color), max(((vec3(colmx) + orig1) - color) * 0.5, Bloom1 * 0.001000000047497451305389404296875));
        Bloom1 = (Bloom1 + mix(Bloom1, mix(orig1 * colmx, Bloom1, vec3(0.5)), vec3(1.0) - color)) * 0.5;
        Bloom1 = (bcmask * Bloom1) * max(mix(1.0, 2.0 - colmx, LIBRA_UNIFORM_bloom_dist), 0.0);
        color = pow(pow(color, vec3(LIBRA_UNIFORM_mask_gamma / gamma_in)) + (pow(Bloom1, vec3(LIBRA_UNIFORM_mask_gamma / gamma_in)) * abs(LIBRA_UNIFORM_bloom)), vec3(gamma_in / LIBRA_UNIFORM_mask_gamma));
    }
    if (LIBRA_UNIFORM_halation > 0.00999999977648258209228515625)
    {
        Bloom = (Bloom + (Bloom * Bloom)) * 0.5;
        float mbl_1 = max(max(Bloom.x, Bloom.y), Bloom.z);
        float cmxh = 0.5 * (colmx + (colmx * colmx));
        mbl_1 = mix(mix(cmxh, mix(cmxh, mbl_1, mbl_1), colmx), mbl_1, mb);
        vec3 param_31 = Bloom;
        float param_32 = mix(sqrt(mbl_1 * cmxh), max(mbl_1 - (0.1500000059604644775390625 * (1.0 - colmx)), 0.4000000059604644775390625 * cmxh), pow(colmx, 0.25));
        Bloom = plant(param_31, param_32) * mix(0.425000011920928955078125, 1.0, colmx);
        vec3 param_33 = vec3(0.324999988079071044921875) + (orig1 / vec3(w3));
        float param_34 = 0.5 * (1.0 + w3);
        Bloom = (((vec3(3.0 - colmx) - color) * plant(param_33, param_34)) * hcmask) * Bloom;
        color = pow(pow(color, vec3(LIBRA_UNIFORM_mask_gamma / gamma_in)) + (pow(Bloom, vec3(LIBRA_UNIFORM_mask_gamma / gamma_in)) * LIBRA_UNIFORM_halation), vec3(gamma_in / LIBRA_UNIFORM_mask_gamma));
    }
    else
    {
        if (LIBRA_UNIFORM_halation < (-0.00999999977648258209228515625))
        {
            float mbl_2 = max(max(Bloom.x, Bloom.y), Bloom.z);
            vec3 param_35 = ((Bloom + Ref) + orig1) + ((Bloom * Bloom) * Bloom);
            float param_36 = min(mbl_2 * mbl_2, 0.75);
            Bloom = plant(param_35, param_36);
            Bloom = (hcmask * (2.0 * mix(1.0, w3, 0.5 * colmx))) * Bloom;
            color -= (Bloom * LIBRA_UNIFORM_halation);
        }
    }
    color = min(color, vec3(1.0));
    vec3 param_37 = color;
    float param_38 = w3;
    color = gc2(param_37, param_38);
    if (LIBRA_UNIFORM_smoothmask > 0.125)
    {
        float w4 = pow(w3, 0.425000011920928955078125 + (0.300000011920928955078125 * LIBRA_UNIFORM_smoothmask));
        w4 = max(w4 - ((0.17499999701976776123046875 * colmx) * LIBRA_UNIFORM_smoothmask), 0.20000000298023223876953125);
        vec3 param_39 = orig1;
        float param_40 = 1.0 + ((0.17499999701976776123046875 * colmx) * LIBRA_UNIFORM_smoothmask);
        color = mix(min(color / vec3(w4), plant(param_39, param_40)) * w4, color, vec3(w4));
    }
    Glow = mix(Glow, color * 0.25, vec3(colmx));
    if (LIBRA_UNIFORM_glow >= 0.0)
    {
        color += ((Glow * 0.5) * LIBRA_UNIFORM_glow);
    }
    else
    {
        cmask2 *= cmask2;
        color += ((min(cmask2, vec3(1.0)) * (-LIBRA_UNIFORM_glow)) * Glow);
    }
    color = min(color, vec3(1.0));
    if (LIBRA_UNIFORM_edgemask > 0.0500000007450580596923828125)
    {
        mx0 = texture(LIBRA_TEXTURE_Source, pos1 - dx).w;
        mx0 = texture(LIBRA_TEXTURE_Source, pos1 - (dx * (1.0 - (0.75 * sqrt(mx0))))).w;
        mx2 = texture(LIBRA_TEXTURE_Source, pos1 + dx).w;
        mx2 = texture(LIBRA_TEXTURE_Source, pos1 + (dx * (1.0 - (0.75 * sqrt(mx2))))).w;
        float mx3 = texture(LIBRA_TEXTURE_Source, pos1 - (dx * 4.0)).w;
        float mx4 = texture(LIBRA_TEXTURE_Source, pos1 + (dx * 4.0)).w;
        mx4 = max(pow(abs(mx3 - mx4), 0.550000011920928955078125 - (0.4000000059604644775390625 * cx)), min(max(mx3, mx4) / min(0.100000001490116119384765625 + cx, 1.0), 1.0));
        mb = 1.0 - abs(pow(mx0, 1.0 - (0.64999997615814208984375 * mx2)) - pow(mx2, 1.0 - (0.64999997615814208984375 * mx0)));
        mb = (mx4 * LIBRA_UNIFORM_edgemask) * (1.00010001659393310546875 - (mb * mb));
        vec3 temp = mix(color, orig1, vec3(mb));
        color = max(temp + mix(mix(temp * 1.625, temp, vec3(cx)) * (3.5 * mb), vec3(0.0), pow(color, vec3(0.75) - vec3(0.5 * colmx))), color);
    }
    color *= mix(1.0, mix(0.5 * (1.0 + w3), w3, mx), LIBRA_UNIFORM_pr_scan);
    color = min(color, max(orig1, color) * mix(one, cmask1, vec3(LIBRA_UNIFORM_mclip)));
    color = pow(color, vec3(1.0 / LIBRA_UNIFORM_gamma_out));
    float rc = (0.60000002384185791015625 * sqrt(max(max(color.x, color.y), color.z))) + 0.4000000059604644775390625;
    if (abs(LIBRA_UNIFORM_addnoised) > 0.00999999977648258209228515625)
    {
        vec3 param_41 = vec3(floor((LIBRA_UNIFORM_OutputSize.xy * LIBRA_VARYING_0) / vec2(LIBRA_UNIFORM_noiseresd)), float(LIBRA_UNIFORM_FrameCount));
        vec3 _2758 = _noise(param_41);
        vec3 noise0 = _2758;
        if (LIBRA_UNIFORM_noisetype < 0.5)
        {
            color = mix(color, noise0, vec3((0.25 * abs(LIBRA_UNIFORM_addnoised)) * rc));
        }
        else
        {
            color = min(color * mix(1.0, 1.5 * noise0.x, 0.5 * abs(LIBRA_UNIFORM_addnoised)), vec3(1.0));
        }
    }
    colmx = max(max(orig1.x, orig1.y), orig1.z);
    color += (mix(cmask2, color * (0.125 * (1.0 - colmx)), vec3(min(20.0 * colmx, 1.0))) * LIBRA_UNIFORM_bmask);
    float param_42 = mix(pos.y, pos.x, LIBRA_UNIFORM_bardir);
    float _2827 = humbar(param_42);
    vec2 param_43 = posb;
    float _2835 = corner(param_43);
    FragColor = mix(vec4((((color * vig) * _2827) * LIBRA_UNIFORM_post_br) * _2835, 1.0), texture(LIBRA_TEXTURE_StockPass, (floor(pos1 * LIBRA_UNIFORM_OriginalSize.xy) + vec2(0.5)) * LIBRA_UNIFORM_OriginalSize.zw), vec4(step(pos1.x, LIBRA_UNIFORM_oimage - 0.00025000001187436282634735107421875)));
}
