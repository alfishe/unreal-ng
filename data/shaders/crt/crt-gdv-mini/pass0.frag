#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform float LIBRA_UNIFORM_brightboost;
uniform float LIBRA_UNIFORM_sat;
uniform float LIBRA_UNIFORM_scanline;
uniform float LIBRA_UNIFORM_beam_min;
uniform float LIBRA_UNIFORM_beam_max;
uniform float LIBRA_UNIFORM_h_sharp;
uniform float LIBRA_UNIFORM_shadowMask;
uniform float LIBRA_UNIFORM_masksize;
uniform float LIBRA_UNIFORM_mcut;
uniform float LIBRA_UNIFORM_maskDark;
uniform float LIBRA_UNIFORM_maskLight;
uniform float LIBRA_UNIFORM_CGWG;
uniform float LIBRA_UNIFORM_csize;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_gamma_out;
uniform float LIBRA_UNIFORM_vignette;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 Warp(inout vec2 pos)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos *= vec2(1.0 + ((pos.y * pos.y) * LIBRA_UNIFORM_warpX), 1.0 + ((pos.x * pos.x) * LIBRA_UNIFORM_warpY));
    return (pos * 0.5) + vec2(0.5);
}

float sw(float y, float l, float v)
{
    float scan = mix(LIBRA_UNIFORM_scanline - 2.0, LIBRA_UNIFORM_scanline, y);
    float tmp = mix(LIBRA_UNIFORM_beam_min + v, LIBRA_UNIFORM_beam_max + v, l);
    float ex = y * tmp;
    return exp2(((-scan) * ex) * ex);
}

vec3 Mask(inout vec2 pos, vec3 c)
{
    pos = floor(pos / vec2(LIBRA_UNIFORM_masksize));
    vec3 mask = vec3(LIBRA_UNIFORM_maskDark, LIBRA_UNIFORM_maskDark, LIBRA_UNIFORM_maskDark);
    if (LIBRA_UNIFORM_shadowMask == (-1.0))
    {
        mask = vec3(1.0);
    }
    else
    {
        if (LIBRA_UNIFORM_shadowMask == 0.0)
        {
            pos.x = fract(pos.x * 0.5);
            float mc = 1.0 - LIBRA_UNIFORM_CGWG;
            if (pos.x < 0.5)
            {
                mask.x = 1.10000002384185791015625;
                mask.y = mc;
                mask.z = 1.10000002384185791015625;
            }
            else
            {
                mask.x = mc;
                mask.y = 1.10000002384185791015625;
                mask.z = mc;
            }
        }
        else
        {
            if (LIBRA_UNIFORM_shadowMask == 1.0)
            {
                float line = LIBRA_UNIFORM_maskLight;
                float odd = 0.0;
                if (fract(pos.x / 6.0) < 0.5)
                {
                    odd = 1.0;
                }
                if (fract((pos.y + odd) / 2.0) < 0.5)
                {
                    line = LIBRA_UNIFORM_maskDark;
                }
                pos.x = fract(pos.x / 3.0);
                if (pos.x < 0.333000004291534423828125)
                {
                    mask.z = LIBRA_UNIFORM_maskLight;
                }
                else
                {
                    if (pos.x < 0.66600000858306884765625)
                    {
                        mask.y = LIBRA_UNIFORM_maskLight;
                    }
                    else
                    {
                        mask.x = LIBRA_UNIFORM_maskLight;
                    }
                }
                mask *= line;
            }
            else
            {
                if (LIBRA_UNIFORM_shadowMask == 2.0)
                {
                    pos.x = fract(pos.x / 3.0);
                    if (pos.x < 0.333000004291534423828125)
                    {
                        mask.z = LIBRA_UNIFORM_maskLight;
                    }
                    else
                    {
                        if (pos.x < 0.66600000858306884765625)
                        {
                            mask.y = LIBRA_UNIFORM_maskLight;
                        }
                        else
                        {
                            mask.x = LIBRA_UNIFORM_maskLight;
                        }
                    }
                }
                else
                {
                    if (LIBRA_UNIFORM_shadowMask == 3.0)
                    {
                        pos.x += (pos.y * 3.0);
                        pos.x = fract(pos.x / 6.0);
                        if (pos.x < 0.333000004291534423828125)
                        {
                            mask.z = LIBRA_UNIFORM_maskLight;
                        }
                        else
                        {
                            if (pos.x < 0.66600000858306884765625)
                            {
                                mask.y = LIBRA_UNIFORM_maskLight;
                            }
                            else
                            {
                                mask.x = LIBRA_UNIFORM_maskLight;
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
                            if (pos.x < 0.333000004291534423828125)
                            {
                                mask.z = LIBRA_UNIFORM_maskLight;
                            }
                            else
                            {
                                if (pos.x < 0.66600000858306884765625)
                                {
                                    mask.y = LIBRA_UNIFORM_maskLight;
                                }
                                else
                                {
                                    mask.x = LIBRA_UNIFORM_maskLight;
                                }
                            }
                        }
                        else
                        {
                            if (LIBRA_UNIFORM_shadowMask == 5.0)
                            {
                                float mx = max(max(c.x, c.y), c.z);
                                vec3 maskTmp = vec3(min((1.25 * max(mx - LIBRA_UNIFORM_mcut, 0.0)) / (1.0 - LIBRA_UNIFORM_mcut), LIBRA_UNIFORM_maskDark + ((0.20000000298023223876953125 * (1.0 - LIBRA_UNIFORM_maskDark)) * mx)));
                                float adj = ((0.800000011920928955078125 * LIBRA_UNIFORM_maskLight) - ((0.5 * ((0.800000011920928955078125 * LIBRA_UNIFORM_maskLight) - 1.0)) * mx)) + (0.75 * (1.0 - mx));
                                mask = maskTmp;
                                pos.x = fract(pos.x / 2.0);
                                if (pos.x < 0.5)
                                {
                                    mask.x = adj;
                                    mask.z = adj;
                                }
                                else
                                {
                                    mask.y = adj;
                                }
                            }
                            else
                            {
                                if (LIBRA_UNIFORM_shadowMask == 6.0)
                                {
                                    float mx_1 = max(max(c.x, c.y), c.z);
                                    vec3 maskTmp_1 = vec3(min((1.33000004291534423828125 * max(mx_1 - LIBRA_UNIFORM_mcut, 0.0)) / (1.0 - LIBRA_UNIFORM_mcut), LIBRA_UNIFORM_maskDark + ((0.2249999940395355224609375 * (1.0 - LIBRA_UNIFORM_maskDark)) * mx_1)));
                                    float adj_1 = ((0.800000011920928955078125 * LIBRA_UNIFORM_maskLight) - ((0.5 * ((0.800000011920928955078125 * LIBRA_UNIFORM_maskLight) - 1.0)) * mx_1)) + (0.75 * (1.0 - mx_1));
                                    mask = maskTmp_1;
                                    pos.x = fract(pos.x / 3.0);
                                    if (pos.x < 0.333000004291534423828125)
                                    {
                                        mask.x = adj_1;
                                    }
                                    else
                                    {
                                        if (pos.x < 0.66600000858306884765625)
                                        {
                                            mask.y = adj_1;
                                        }
                                        else
                                        {
                                            mask.z = adj_1;
                                        }
                                    }
                                }
                                else
                                {
                                    if (LIBRA_UNIFORM_shadowMask == 7.0)
                                    {
                                        float mc_1 = 1.0 - LIBRA_UNIFORM_CGWG;
                                        float mx_2 = max(max(c.x, c.y), c.z);
                                        float maskTmp_2 = min((1.60000002384185791015625 * max(mx_2 - LIBRA_UNIFORM_mcut, 0.0)) / (1.0 - LIBRA_UNIFORM_mcut), mc_1);
                                        mask = vec3(maskTmp_2);
                                        pos.x = fract(pos.x / 2.0);
                                        if (pos.x < 0.5)
                                        {
                                            mask = vec3(1.0 + (0.60000002384185791015625 * (1.0 - mx_2)));
                                        }
                                    }
                                    else
                                    {
                                        if (LIBRA_UNIFORM_shadowMask == 8.0)
                                        {
                                            float line_1 = LIBRA_UNIFORM_maskLight;
                                            float odd_1 = 0.0;
                                            if (fract(pos.x / 4.0) < 0.5)
                                            {
                                                odd_1 = 1.0;
                                            }
                                            if (fract((pos.y + odd_1) / 2.0) < 0.5)
                                            {
                                                line_1 = LIBRA_UNIFORM_maskDark;
                                            }
                                            pos.x = fract(pos.x / 2.0);
                                            if (pos.x < 0.5)
                                            {
                                                mask.x = LIBRA_UNIFORM_maskLight;
                                                mask.z = LIBRA_UNIFORM_maskLight;
                                            }
                                            else
                                            {
                                                mask.y = LIBRA_UNIFORM_maskLight;
                                            }
                                            mask *= line_1;
                                        }
                                        else
                                        {
                                            if (LIBRA_UNIFORM_shadowMask == 9.0)
                                            {
                                                vec3 Mask_1 = vec3(LIBRA_UNIFORM_maskDark);
                                                float bright = LIBRA_UNIFORM_maskLight;
                                                float left = 0.0;
                                                if (fract(pos.x / 6.0) < 0.5)
                                                {
                                                    left = 1.0;
                                                }
                                                float m = fract(pos.x / 3.0);
                                                if (m < 0.33329999446868896484375)
                                                {
                                                    Mask_1.z = 0.89999997615814208984375;
                                                }
                                                else
                                                {
                                                    if (m < 0.6665999889373779296875)
                                                    {
                                                        Mask_1.y = 0.89999997615814208984375;
                                                    }
                                                    else
                                                    {
                                                        Mask_1.x = 0.89999997615814208984375;
                                                    }
                                                }
                                                bool _619 = (mod(pos.y, 2.0) == 1.0) && (left == 1.0);
                                                bool _630;
                                                if (!_619)
                                                {
                                                    _630 = (mod(pos.y, 2.0) == 0.0) && (left == 0.0);
                                                }
                                                else
                                                {
                                                    _630 = _619;
                                                }
                                                if (_630)
                                                {
                                                    Mask_1 *= bright;
                                                }
                                                return Mask_1;
                                            }
                                            else
                                            {
                                                if (LIBRA_UNIFORM_shadowMask == 10.0)
                                                {
                                                    vec3 Mask_2 = vec3(LIBRA_UNIFORM_maskDark);
                                                    float line_2 = LIBRA_UNIFORM_maskLight;
                                                    float odd_2 = 0.0;
                                                    if (fract(pos.x / 6.0) < 0.5)
                                                    {
                                                        odd_2 = 1.0;
                                                    }
                                                    if (fract((pos.y + odd_2) / 2.0) < 0.5)
                                                    {
                                                        line_2 = 1.0;
                                                    }
                                                    float m_1 = fract(pos.x / 3.0);
                                                    float y = fract(pos.y / 2.0);
                                                    if (m_1 > 0.33329999446868896484375)
                                                    {
                                                        Mask_2.x = 1.0;
                                                        Mask_2.z = 1.0;
                                                    }
                                                    else
                                                    {
                                                        if (m_1 > 0.6665999889373779296875)
                                                        {
                                                            Mask_2.y = 1.0;
                                                        }
                                                        else
                                                        {
                                                            Mask_2 = vec3(LIBRA_UNIFORM_mcut);
                                                        }
                                                    }
                                                    if (m_1 > 0.333000004291534423828125)
                                                    {
                                                        Mask_2 *= line_2;
                                                    }
                                                    return Mask_2;
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
    return mask;
}

void main()
{
    vec2 param = LIBRA_VARYING_0;
    vec2 _745 = Warp(param);
    vec2 pos = _745;
    vec2 OGL2Pos = pos * LIBRA_UNIFORM_SourceSize.xy;
    vec2 fp = fract(OGL2Pos);
    vec2 ps = LIBRA_UNIFORM_SourceSize.zw;
    vec2 dx = vec2(ps.x, 0.0);
    vec2 dy = vec2(0.0, ps.y);
    vec2 pC4 = (floor(OGL2Pos) * ps) + (ps * 0.5);
    float f = fp.y;
    if (LIBRA_UNIFORM_OriginalSize.y > 400.0)
    {
        f = 1.0;
    }
    vec3 ul = texture(LIBRA_TEXTURE_Source, pC4).xyz;
    vec3 ur = texture(LIBRA_TEXTURE_Source, pC4 - dx).xyz;
    vec3 dl = texture(LIBRA_TEXTURE_Source, pC4 + dy).xyz;
    vec3 dr = texture(LIBRA_TEXTURE_Source, pC4 + vec2(-ps.x, ps.y)).xyz;
    float lx = fp.x;
    lx = pow(lx, LIBRA_UNIFORM_h_sharp);
    float rx = 1.0 - fp.x;
    rx = pow(rx, LIBRA_UNIFORM_h_sharp);
    vec3 color1 = ((ur * rx) + (ul * lx)) / vec3(lx + rx);
    vec3 color2 = ((dr * rx) + (dl * lx)) / vec3(lx + rx);
    color1 *= color1;
    color2 *= color2;
    float lum = dot(color1, vec3(0.300000011920928955078125, 0.60000002384185791015625, 0.100000001490116119384765625));
    float lum2 = dot(color2, vec3(0.300000011920928955078125, 0.60000002384185791015625, 0.100000001490116119384765625));
    float v = 0.0;
    if (LIBRA_UNIFORM_vignette > 0.0)
    {
        v = LIBRA_VARYING_0.x - 0.5;
        v *= v;
    }
    float param_1 = f;
    float param_2 = lum;
    float param_3 = v;
    float param_4 = 1.0 - f;
    float param_5 = lum2;
    float param_6 = v;
    vec3 color = (color1 * sw(param_1, param_2, param_3)) + (color2 * sw(param_4, param_5, param_6));
    vec2 param_7 = LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy;
    vec3 param_8 = color;
    vec3 _924 = Mask(param_7, param_8);
    color *= _924;
    color = pow(color, vec3(LIBRA_UNIFORM_gamma_out));
    float s = dot(vec3(0.300000011920928955078125, 0.60000002384185791015625, 0.100000001490116119384765625), color);
    color *= mix(1.0, LIBRA_UNIFORM_brightboost, lum);
    color = mix(vec3(s), color, vec3(LIBRA_UNIFORM_sat));
    FragColor.x = color.x;
    FragColor.y = color.y;
    FragColor.z = color.z;
}
