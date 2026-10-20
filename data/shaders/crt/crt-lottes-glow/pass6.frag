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
uniform float LIBRA_UNIFORM_DIFFUSION;

uniform sampler2D LIBRA_TEXTURE_ORIG_LINEARIZED;
uniform sampler2D LIBRA_TEXTURE_GlowPass;
uniform sampler2D LIBRA_TEXTURE_BloomPass;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 Warp(inout vec2 pos)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos *= vec2(1.0 + ((pos.y * pos.y) * LIBRA_UNIFORM_warpX), 1.0 + ((pos.x * pos.x) * LIBRA_UNIFORM_warpY));
    return (pos * 0.5) + vec2(0.5);
}

vec3 ToLinear(vec3 c)
{
    return c;
}

vec3 Fetch(inout vec2 pos, vec2 off)
{
    pos = (floor((pos * LIBRA_UNIFORM_SourceSize.xy) + off) + vec2(0.5)) / LIBRA_UNIFORM_SourceSize.xy;
    vec3 param = texture(LIBRA_TEXTURE_ORIG_LINEARIZED, pos).xyz * LIBRA_UNIFORM_brightBoost;
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
    vec3 _140 = Fetch(param, param_1);
    vec3 b = _140;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(0.0, off);
    vec3 _148 = Fetch(param_2, param_3);
    vec3 c = _148;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(1.0, off);
    vec3 _156 = Fetch(param_4, param_5);
    vec3 d = _156;
    vec2 param_6 = pos;
    vec2 _160 = Dist(param_6);
    float dst = _160.x;
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
    vec3 _216 = Fetch(param, param_1);
    vec3 a = _216;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(-1.0, off);
    vec3 _223 = Fetch(param_2, param_3);
    vec3 b = _223;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(0.0, off);
    vec3 _230 = Fetch(param_4, param_5);
    vec3 c = _230;
    vec2 param_6 = pos;
    vec2 param_7 = vec2(1.0, off);
    vec3 _237 = Fetch(param_6, param_7);
    vec3 d = _237;
    vec2 param_8 = pos;
    vec2 param_9 = vec2(2.0, off);
    vec3 _245 = Fetch(param_8, param_9);
    vec3 e = _245;
    vec2 param_10 = pos;
    vec2 _249 = Dist(param_10);
    float dst = _249.x;
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
    vec2 _324 = Dist(param);
    float dst = _324.y;
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

vec3 ToSrgb(vec3 c)
{
    return pow(c, vec3(0.4545454680919647216796875));
}

void main()
{
    vec2 param = LIBRA_VARYING_0;
    vec2 _610 = Warp(param);
    vec2 pos = _610;
    vec2 param_1 = pos;
    vec3 outColor = Tri(param_1);
    vec3 diff = texture(LIBRA_TEXTURE_GlowPass, pos).xyz;
    if (LIBRA_UNIFORM_shadowMask > 0.0)
    {
        vec2 param_2 = (LIBRA_VARYING_0 / LIBRA_UNIFORM_OutputSize.zw) * 1.00000095367431640625;
        vec3 _634 = Mask(param_2);
        outColor *= _634;
    }
    outColor += mix(vec3(0.0), texture(LIBRA_TEXTURE_BloomPass, pos).xyz, vec3(LIBRA_UNIFORM_bloomAmount));
    outColor += (diff * LIBRA_UNIFORM_DIFFUSION);
    vec3 param_3 = outColor;
    FragColor = vec4(ToSrgb(param_3), 1.0);
}
