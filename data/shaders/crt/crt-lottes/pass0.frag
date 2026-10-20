#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_hardScan;
uniform float LIBRA_UNIFORM_hardPix;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_maskDark;
uniform float LIBRA_UNIFORM_maskLight;
uniform float LIBRA_UNIFORM_scaleInLinearGamma;
uniform float LIBRA_UNIFORM_shadowMask;
uniform float LIBRA_UNIFORM_brightBoost;
uniform float LIBRA_UNIFORM_hardBloomScan;
uniform float LIBRA_UNIFORM_hardBloomPix;
uniform float LIBRA_UNIFORM_bloomAmount;
uniform float LIBRA_UNIFORM_shape;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 Warp(inout vec2 pos)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos *= vec2(1.0 + ((pos.y * pos.y) * LIBRA_UNIFORM_warpX), 1.0 + ((pos.x * pos.x) * LIBRA_UNIFORM_warpY));
    return (pos * 0.5) + vec2(0.5);
}

float ToLinear1(float c)
{
    if (LIBRA_UNIFORM_scaleInLinearGamma == 0.0)
    {
        return c;
    }
    float _93;
    if (c <= 0.040449999272823333740234375)
    {
        _93 = c / 12.9200000762939453125;
    }
    else
    {
        _93 = pow((c + 0.054999999701976776123046875) / 1.05499994754791259765625, 2.400000095367431640625);
    }
    return _93;
}

vec3 ToLinear(vec3 c)
{
    if (LIBRA_UNIFORM_scaleInLinearGamma == 0.0)
    {
        return c;
    }
    float param = c.x;
    float param_1 = c.y;
    float param_2 = c.z;
    return vec3(ToLinear1(param), ToLinear1(param_1), ToLinear1(param_2));
}

vec3 Fetch(inout vec2 pos, vec2 off)
{
    pos = (floor((pos * LIBRA_UNIFORM_SourceSize.xy) + off) + vec2(0.5)) / LIBRA_UNIFORM_SourceSize.xy;
    vec3 param = texture(LIBRA_TEXTURE_Source, pos).xyz * LIBRA_UNIFORM_brightBoost;
    return ToLinear(param);
}

vec2 Dist(inout vec2 pos)
{
    pos *= LIBRA_UNIFORM_SourceSize.xy;
    return -((pos - floor(pos)) - vec2(0.5));
}

float Gaus(float pos, float scale)
{
    return exp2(scale * pow(abs(pos), LIBRA_UNIFORM_shape));
}

vec3 Horz3(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 param_1 = vec2(-1.0, off);
    vec3 _251 = Fetch(param, param_1);
    vec3 b = _251;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(0.0, off);
    vec3 _258 = Fetch(param_2, param_3);
    vec3 c = _258;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(1.0, off);
    vec3 _266 = Fetch(param_4, param_5);
    vec3 d = _266;
    vec2 param_6 = pos;
    vec2 _270 = Dist(param_6);
    float dst = _270.x;
    float scale = LIBRA_UNIFORM_hardPix;
    float param_7 = dst - 1.0;
    float param_8 = scale;
    float wb = Gaus(param_7, param_8);
    float param_9 = dst + 0.0;
    float param_10 = scale;
    float wc = Gaus(param_9, param_10);
    float param_11 = dst + 1.0;
    float param_12 = scale;
    float wd = Gaus(param_11, param_12);
    return (((b * wb) + (c * wc)) + (d * wd)) / vec3((wb + wc) + wd);
}

vec3 Horz5(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 param_1 = vec2(-2.0, off);
    vec3 _324 = Fetch(param, param_1);
    vec3 a = _324;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(-1.0, off);
    vec3 _331 = Fetch(param_2, param_3);
    vec3 b = _331;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(0.0, off);
    vec3 _338 = Fetch(param_4, param_5);
    vec3 c = _338;
    vec2 param_6 = pos;
    vec2 param_7 = vec2(1.0, off);
    vec3 _345 = Fetch(param_6, param_7);
    vec3 d = _345;
    vec2 param_8 = pos;
    vec2 param_9 = vec2(2.0, off);
    vec3 _353 = Fetch(param_8, param_9);
    vec3 e = _353;
    vec2 param_10 = pos;
    vec2 _357 = Dist(param_10);
    float dst = _357.x;
    float scale = LIBRA_UNIFORM_hardPix;
    float param_11 = dst - 2.0;
    float param_12 = scale;
    float wa = Gaus(param_11, param_12);
    float param_13 = dst - 1.0;
    float param_14 = scale;
    float wb = Gaus(param_13, param_14);
    float param_15 = dst + 0.0;
    float param_16 = scale;
    float wc = Gaus(param_15, param_16);
    float param_17 = dst + 1.0;
    float param_18 = scale;
    float wd = Gaus(param_17, param_18);
    float param_19 = dst + 2.0;
    float param_20 = scale;
    float we = Gaus(param_19, param_20);
    return (((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) + (e * we)) / vec3((((wa + wb) + wc) + wd) + we);
}

float Scan(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 _585 = Dist(param);
    float dst = _585.y;
    float param_1 = dst + off;
    float param_2 = LIBRA_UNIFORM_hardScan;
    return Gaus(param_1, param_2);
}

vec3 Tri(vec2 pos)
{
    vec2 param = pos;
    float param_1 = -1.0;
    vec3 a = Horz3(param, param_1);
    vec2 param_2 = pos;
    float param_3 = 0.0;
    vec3 b = Horz5(param_2, param_3);
    vec2 param_4 = pos;
    float param_5 = 1.0;
    vec3 c = Horz3(param_4, param_5);
    vec2 param_6 = pos;
    float param_7 = -1.0;
    float wa = Scan(param_6, param_7);
    vec2 param_8 = pos;
    float param_9 = 0.0;
    float wb = Scan(param_8, param_9);
    vec2 param_10 = pos;
    float param_11 = 1.0;
    float wc = Scan(param_10, param_11);
    return ((a * wa) + (b * wb)) + (c * wc);
}

vec3 Horz7(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 param_1 = vec2(-3.0, off);
    vec3 _436 = Fetch(param, param_1);
    vec3 a = _436;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(-2.0, off);
    vec3 _443 = Fetch(param_2, param_3);
    vec3 b = _443;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(-1.0, off);
    vec3 _450 = Fetch(param_4, param_5);
    vec3 c = _450;
    vec2 param_6 = pos;
    vec2 param_7 = vec2(0.0, off);
    vec3 _457 = Fetch(param_6, param_7);
    vec3 d = _457;
    vec2 param_8 = pos;
    vec2 param_9 = vec2(1.0, off);
    vec3 _464 = Fetch(param_8, param_9);
    vec3 e = _464;
    vec2 param_10 = pos;
    vec2 param_11 = vec2(2.0, off);
    vec3 _471 = Fetch(param_10, param_11);
    vec3 f = _471;
    vec2 param_12 = pos;
    vec2 param_13 = vec2(3.0, off);
    vec3 _479 = Fetch(param_12, param_13);
    vec3 g = _479;
    vec2 param_14 = pos;
    vec2 _483 = Dist(param_14);
    float dst = _483.x;
    float scale = LIBRA_UNIFORM_hardBloomPix;
    float param_15 = dst - 3.0;
    float param_16 = scale;
    float wa = Gaus(param_15, param_16);
    float param_17 = dst - 2.0;
    float param_18 = scale;
    float wb = Gaus(param_17, param_18);
    float param_19 = dst - 1.0;
    float param_20 = scale;
    float wc = Gaus(param_19, param_20);
    float param_21 = dst + 0.0;
    float param_22 = scale;
    float wd = Gaus(param_21, param_22);
    float param_23 = dst + 1.0;
    float param_24 = scale;
    float we = Gaus(param_23, param_24);
    float param_25 = dst + 2.0;
    float param_26 = scale;
    float wf = Gaus(param_25, param_26);
    float param_27 = dst + 3.0;
    float param_28 = scale;
    float wg = Gaus(param_27, param_28);
    return (((((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) + (e * we)) + (f * wf)) + (g * wg)) / vec3((((((wa + wb) + wc) + wd) + we) + wf) + wg);
}

float BloomScan(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 _601 = Dist(param);
    float dst = _601.y;
    float param_1 = dst + off;
    float param_2 = LIBRA_UNIFORM_hardBloomScan;
    return Gaus(param_1, param_2);
}

vec3 Bloom(vec2 pos)
{
    vec2 param = pos;
    float param_1 = -2.0;
    vec3 a = Horz5(param, param_1);
    vec2 param_2 = pos;
    float param_3 = -1.0;
    vec3 b = Horz7(param_2, param_3);
    vec2 param_4 = pos;
    float param_5 = 0.0;
    vec3 c = Horz7(param_4, param_5);
    vec2 param_6 = pos;
    float param_7 = 1.0;
    vec3 d = Horz7(param_6, param_7);
    vec2 param_8 = pos;
    float param_9 = 2.0;
    vec3 e = Horz5(param_8, param_9);
    vec2 param_10 = pos;
    float param_11 = -2.0;
    float wa = BloomScan(param_10, param_11);
    vec2 param_12 = pos;
    float param_13 = -1.0;
    float wb = BloomScan(param_12, param_13);
    vec2 param_14 = pos;
    float param_15 = 0.0;
    float wc = BloomScan(param_14, param_15);
    vec2 param_16 = pos;
    float param_17 = 1.0;
    float wd = BloomScan(param_16, param_17);
    vec2 param_18 = pos;
    float param_19 = 2.0;
    float we = BloomScan(param_18, param_19);
    return ((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) + (e * we);
}

vec3 Mask(inout vec2 pos)
{
    vec3 mask = vec3(LIBRA_UNIFORM_maskDark, LIBRA_UNIFORM_maskDark, LIBRA_UNIFORM_maskDark);
    if (LIBRA_UNIFORM_shadowMask == 1.0)
    {
        float line = LIBRA_UNIFORM_maskLight;
        float odd = 0.0;
        if (fract(pos.x * 0.16666667163372039794921875) < 0.5)
        {
            odd = 1.0;
        }
        if (fract((pos.y + odd) * 0.5) < 0.5)
        {
            line = LIBRA_UNIFORM_maskDark;
        }
        pos.x = fract(pos.x * 0.3333333432674407958984375);
        if (pos.x < 0.333000004291534423828125)
        {
            mask.x = LIBRA_UNIFORM_maskLight;
        }
        else
        {
            if (pos.x < 0.66600000858306884765625)
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
            pos.x = fract(pos.x * 0.3333333432674407958984375);
            if (pos.x < 0.333000004291534423828125)
            {
                mask.x = LIBRA_UNIFORM_maskLight;
            }
            else
            {
                if (pos.x < 0.66600000858306884765625)
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
                pos.x = fract(pos.x * 0.16666667163372039794921875);
                if (pos.x < 0.333000004291534423828125)
                {
                    mask.x = LIBRA_UNIFORM_maskLight;
                }
                else
                {
                    if (pos.x < 0.66600000858306884765625)
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
                    pos.x = fract(pos.x * 0.16666667163372039794921875);
                    if (pos.x < 0.333000004291534423828125)
                    {
                        mask.x = LIBRA_UNIFORM_maskLight;
                    }
                    else
                    {
                        if (pos.x < 0.66600000858306884765625)
                        {
                            mask.y = LIBRA_UNIFORM_maskLight;
                        }
                        else
                        {
                            mask.z = LIBRA_UNIFORM_maskLight;
                        }
                    }
                }
            }
        }
    }
    return mask;
}

float ToSrgb1(float c)
{
    if (LIBRA_UNIFORM_scaleInLinearGamma == 0.0)
    {
        return c;
    }
    float _146;
    if (c < 0.003130800090730190277099609375)
    {
        _146 = c * 12.9200000762939453125;
    }
    else
    {
        _146 = (1.05499994754791259765625 * pow(c, 0.416660010814666748046875)) - 0.054999999701976776123046875;
    }
    return _146;
}

vec3 ToSrgb(vec3 c)
{
    if (LIBRA_UNIFORM_scaleInLinearGamma == 0.0)
    {
        return c;
    }
    float param = c.x;
    float param_1 = c.y;
    float param_2 = c.z;
    return vec3(ToSrgb1(param), ToSrgb1(param_1), ToSrgb1(param_2));
}

void main()
{
    vec2 param = LIBRA_VARYING_0;
    vec2 _954 = Warp(param);
    vec2 pos = _954;
    vec2 param_1 = pos;
    vec3 outColor = Tri(param_1);
    vec2 param_2 = pos;
    outColor += (Bloom(param_2) * LIBRA_UNIFORM_bloomAmount);
    if (LIBRA_UNIFORM_shadowMask > 0.0)
    {
        vec2 param_3 = (LIBRA_VARYING_0 / LIBRA_UNIFORM_OutputSize.zw) * 1.00000095367431640625;
        vec3 _981 = Mask(param_3);
        outColor *= _981;
    }
    vec3 param_4 = outColor;
    FragColor = vec4(ToSrgb(param_4), 1.0);
}
