#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform float LIBRA_UNIFORM_blurx;
uniform float LIBRA_UNIFORM_blury;
uniform float LIBRA_UNIFORM_warpx;
uniform float LIBRA_UNIFORM_warpy;
uniform float LIBRA_UNIFORM_corner;
uniform float LIBRA_UNIFORM_smoothness;
uniform float LIBRA_UNIFORM_scanlow;
uniform float LIBRA_UNIFORM_scanhigh;
uniform float LIBRA_UNIFORM_beamlow;
uniform float LIBRA_UNIFORM_beamhigh;
uniform float LIBRA_UNIFORM_brightboost1;
uniform float LIBRA_UNIFORM_brightboost2;
uniform float LIBRA_UNIFORM_Shadowmask;
uniform float LIBRA_UNIFORM_masksize;
uniform float LIBRA_UNIFORM_MaskDark;
uniform float LIBRA_UNIFORM_MaskLight;
uniform float LIBRA_UNIFORM_slotmask;
uniform float LIBRA_UNIFORM_slotwidth;
uniform float LIBRA_UNIFORM_double_slot;
uniform float LIBRA_UNIFORM_slotms;
uniform float LIBRA_UNIFORM_GAMMA_OUT;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_glow_str;
uniform float LIBRA_UNIFORM_sat;
uniform float LIBRA_UNIFORM_contrast;
uniform float LIBRA_UNIFORM_nois;
uniform float LIBRA_UNIFORM_WP;
uniform float LIBRA_UNIFORM_inter;
uniform float LIBRA_UNIFORM_vignette;
uniform float LIBRA_UNIFORM_vpower;
uniform float LIBRA_UNIFORM_vstr;
uniform float LIBRA_UNIFORM_alloff;
uniform float LIBRA_UNIFORM_postbr;
uniform float LIBRA_UNIFORM_PRE_SCALE;
uniform float LIBRA_UNIFORM_preserve;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_rg;
uniform float LIBRA_UNIFORM_rb;
uniform float LIBRA_UNIFORM_gr;
uniform float LIBRA_UNIFORM_gb;
uniform float LIBRA_UNIFORM_br;
uniform float LIBRA_UNIFORM_bg;
uniform float LIBRA_UNIFORM_Downscale;
uniform float LIBRA_UNIFORM_quality;
uniform float LIBRA_UNIFORM_palette_fix;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 Warp(inout vec2 pos)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos *= vec2(1.0 + ((pos.y * pos.y) * LIBRA_UNIFORM_warpx), 1.0 + ((pos.x * pos.x) * LIBRA_UNIFORM_warpy));
    return (pos * 0.5) + vec2(0.5);
}

float sw(float y, float l)
{
    float beam = mix(LIBRA_UNIFORM_scanlow, LIBRA_UNIFORM_scanhigh, y);
    float scan = mix(LIBRA_UNIFORM_beamlow, LIBRA_UNIFORM_beamhigh, l);
    float ex = y * scan;
    return exp2(((-beam) * ex) * ex);
}

vec3 mask(inout vec2 x, vec3 col, float l)
{
    x = floor(x / vec2(LIBRA_UNIFORM_masksize));
    if (LIBRA_UNIFORM_Shadowmask == 0.0)
    {
        float m = fract(x.x * 0.4999000132083892822265625);
        if (m < 0.4999000132083892822265625)
        {
            return vec3(1.0, LIBRA_UNIFORM_MaskDark, 1.0);
        }
        else
        {
            return vec3(LIBRA_UNIFORM_MaskDark, 1.0, LIBRA_UNIFORM_MaskDark);
        }
    }
    else
    {
        if (LIBRA_UNIFORM_Shadowmask == 1.0)
        {
            vec3 Mask = vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark);
            float line = LIBRA_UNIFORM_MaskLight;
            float odd = 0.0;
            if (fract(x.x / 6.0) < 0.5)
            {
                odd = 1.0;
            }
            if (fract((x.y + odd) / 2.0) < 0.5)
            {
                line = LIBRA_UNIFORM_MaskDark;
            }
            float m_1 = fract(x.x / 3.0);
            if (m_1 < 0.333000004291534423828125)
            {
                Mask.z = LIBRA_UNIFORM_MaskLight;
            }
            else
            {
                if (m_1 < 0.66600000858306884765625)
                {
                    Mask.y = LIBRA_UNIFORM_MaskLight;
                }
                else
                {
                    Mask.x = LIBRA_UNIFORM_MaskLight;
                }
            }
            Mask *= line;
            return Mask;
        }
        else
        {
            if (LIBRA_UNIFORM_Shadowmask == 2.0)
            {
                float m_2 = fract(x.x * 0.33329999446868896484375);
                if (m_2 < 0.33329999446868896484375)
                {
                    return vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskLight);
                }
                if (m_2 < 0.6665999889373779296875)
                {
                    return vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskLight, LIBRA_UNIFORM_MaskDark);
                }
                else
                {
                    return vec3(LIBRA_UNIFORM_MaskLight, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark);
                }
            }
        }
    }
    if (LIBRA_UNIFORM_Shadowmask == 3.0)
    {
        float m_3 = fract(x.x * 0.5);
        if (m_3 < 0.5)
        {
            return vec3(1.0);
        }
        else
        {
            return vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark);
        }
    }
    else
    {
        if (LIBRA_UNIFORM_Shadowmask == 4.0)
        {
            vec3 Mask_1 = col;
            float line_1 = LIBRA_UNIFORM_MaskLight;
            float odd_1 = 0.0;
            if (fract(x.x / 4.0) < 0.5)
            {
                odd_1 = 1.0;
            }
            if (fract((x.y + odd_1) / 2.0) < 0.5)
            {
                line_1 = LIBRA_UNIFORM_MaskDark;
            }
            float m_4 = fract(x.x / 2.0);
            if (m_4 < 0.5)
            {
                Mask_1.x = 1.0;
                Mask_1.z = 1.0;
            }
            else
            {
                Mask_1.y = 1.0;
            }
            Mask_1 *= line_1;
            return Mask_1;
        }
        else
        {
            if (LIBRA_UNIFORM_Shadowmask == 5.0)
            {
                vec3 Mask_2 = vec3(1.0);
                if (fract(x.x / 4.0) < 0.5)
                {
                    if (fract(x.y / 3.0) < 0.66600000858306884765625)
                    {
                        if (fract(x.x / 2.0) < 0.5)
                        {
                            Mask_2 = vec3(1.0, LIBRA_UNIFORM_MaskDark, 1.0);
                        }
                        else
                        {
                            Mask_2 = vec3(LIBRA_UNIFORM_MaskDark, 1.0, LIBRA_UNIFORM_MaskDark);
                        }
                    }
                    else
                    {
                        Mask_2 *= l;
                    }
                }
                else
                {
                    if (fract(x.x / 4.0) >= 0.5)
                    {
                        if (fract(x.y / 3.0) > 0.333000004291534423828125)
                        {
                            if (fract(x.x / 2.0) < 0.5)
                            {
                                Mask_2 = vec3(1.0, LIBRA_UNIFORM_MaskDark, 1.0);
                            }
                            else
                            {
                                Mask_2 = vec3(LIBRA_UNIFORM_MaskDark, 1.0, LIBRA_UNIFORM_MaskDark);
                            }
                        }
                        else
                        {
                            Mask_2 *= l;
                        }
                    }
                }
                return Mask_2;
            }
            else
            {
                if (LIBRA_UNIFORM_Shadowmask == 6.0)
                {
                    vec3 Mask_3 = vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark);
                    if (fract(x.x / 6.0) < 0.5)
                    {
                        if (fract(x.y / 4.0) < 0.75)
                        {
                            if (fract(x.x / 3.0) < 0.33329999446868896484375)
                            {
                                Mask_3.x = LIBRA_UNIFORM_MaskLight;
                            }
                            else
                            {
                                if (fract(x.x / 3.0) < 0.6665999889373779296875)
                                {
                                    Mask_3.y = LIBRA_UNIFORM_MaskLight;
                                }
                                else
                                {
                                    Mask_3.z = LIBRA_UNIFORM_MaskLight;
                                }
                            }
                        }
                        else
                        {
                        }
                    }
                    else
                    {
                        if (fract(x.x / 6.0) >= 0.5)
                        {
                            bool _513 = fract(x.y / 4.0) >= 0.5;
                            bool _523;
                            if (!_513)
                            {
                                _523 = fract(x.y / 4.0) < 0.25;
                            }
                            else
                            {
                                _523 = _513;
                            }
                            if (_523)
                            {
                                if (fract(x.x / 3.0) < 0.33329999446868896484375)
                                {
                                    Mask_3.x = LIBRA_UNIFORM_MaskLight;
                                }
                                else
                                {
                                    if (fract(x.x / 3.0) < 0.6665999889373779296875)
                                    {
                                        Mask_3.y = LIBRA_UNIFORM_MaskLight;
                                    }
                                    else
                                    {
                                        Mask_3.z = LIBRA_UNIFORM_MaskLight;
                                    }
                                }
                            }
                            else
                            {
                            }
                        }
                    }
                    return Mask_3;
                }
                else
                {
                    if (LIBRA_UNIFORM_Shadowmask == 7.0)
                    {
                        float m_5 = fract(x.x * 0.33329999446868896484375);
                        if (m_5 < 0.33329999446868896484375)
                        {
                            return vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskLight, LIBRA_UNIFORM_MaskLight * col.z);
                        }
                        if (m_5 < 0.6665999889373779296875)
                        {
                            return vec3(LIBRA_UNIFORM_MaskLight * col.x, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskLight);
                        }
                        else
                        {
                            return vec3(LIBRA_UNIFORM_MaskLight, LIBRA_UNIFORM_MaskLight * col.y, LIBRA_UNIFORM_MaskDark);
                        }
                    }
                    else
                    {
                        if (LIBRA_UNIFORM_Shadowmask == 8.0)
                        {
                            vec3 Mask_4 = vec3(LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark, LIBRA_UNIFORM_MaskDark);
                            float bright = LIBRA_UNIFORM_MaskLight;
                            float left = 0.0;
                            if (fract(x.x / 6.0) < 0.5)
                            {
                                left = 1.0;
                            }
                            float m_6 = fract(x.x / 3.0);
                            if (m_6 < 0.333000004291534423828125)
                            {
                                Mask_4.z = 0.89999997615814208984375;
                            }
                            else
                            {
                                if (m_6 < 0.66600000858306884765625)
                                {
                                    Mask_4.y = 0.89999997615814208984375;
                                }
                                else
                                {
                                    Mask_4.x = 0.89999997615814208984375;
                                }
                            }
                            bool _662 = (mod(x.y, 2.0) == 1.0) && (left == 1.0);
                            bool _673;
                            if (!_662)
                            {
                                _673 = (mod(x.y, 2.0) == 0.0) && (left == 0.0);
                            }
                            else
                            {
                                _673 = _662;
                            }
                            if (_673)
                            {
                                Mask_4 *= bright;
                            }
                            return Mask_4;
                        }
                        else
                        {
                            return vec3(1.0);
                        }
                    }
                }
            }
        }
    }
}

float SlotMask(inout vec2 pos, vec3 c)
{
    if (LIBRA_UNIFORM_slotmask == 0.0)
    {
        return 1.0;
    }
    pos = floor(pos / vec2(LIBRA_UNIFORM_slotms));
    float mx = pow(max(max(c.x, c.y), c.z), 1.33000004291534423828125);
    float mlen = LIBRA_UNIFORM_slotwidth * 2.0;
    float px = fract(pos.x / mlen);
    float py = floor((fract(pos.y / (2.0 * LIBRA_UNIFORM_double_slot)) * 2.0) * LIBRA_UNIFORM_double_slot);
    float slot_dark = mix(1.0 - LIBRA_UNIFORM_slotmask, 1.0 - (0.800000011920928955078125 * LIBRA_UNIFORM_slotmask), mx);
    float slot = 1.0 + ((0.699999988079071044921875 * LIBRA_UNIFORM_slotmask) * (1.0 - mx));
    if ((py == 0.0) && (px < 0.5))
    {
        slot = slot_dark;
    }
    else
    {
        if ((py == LIBRA_UNIFORM_double_slot) && (px >= 0.5))
        {
            slot = slot_dark;
        }
    }
    return slot;
}

vec3 glow0(vec2 texcoord, vec3 col)
{
    vec2 size = LIBRA_UNIFORM_SourceSize.zw / vec2(LIBRA_UNIFORM_quality);
    vec3 sum = vec3(0.0);
    float _893 = -LIBRA_UNIFORM_glow;
    for (float x = _893; x <= LIBRA_UNIFORM_glow; x += 1.0)
    {
        float factor = 1.0 / LIBRA_UNIFORM_glow;
        float _910 = -LIBRA_UNIFORM_glow;
        for (float y = _910; y <= LIBRA_UNIFORM_glow; y += 1.0)
        {
            vec2 offset = vec2(x, y) * size;
            vec3 c01 = texture(LIBRA_TEXTURE_Source, texcoord + offset).xyz * factor;
            c01 *= c01;
            sum += c01;
        }
    }
    return (sum * LIBRA_UNIFORM_glow_str) / vec3(LIBRA_UNIFORM_glow * LIBRA_UNIFORM_glow);
}

vec3 saturation(vec3 textureColor)
{
    float luminance = length(textureColor) * 0.57749998569488525390625;
    vec3 luminanceWeighting = vec3(0.4000000059604644775390625, 0.5, 0.100000001490116119384765625);
    if (luminance < 0.5)
    {
        luminanceWeighting = (luminanceWeighting * luminanceWeighting) + (luminanceWeighting * luminanceWeighting);
    }
    luminance = dot(textureColor, luminanceWeighting);
    vec3 greyScaleColor = vec3(luminance);
    vec3 res = mix(greyScaleColor, textureColor, vec3(LIBRA_UNIFORM_sat));
    return res;
}

float corner0(inout vec2 coord)
{
    coord = ((coord - vec2(0.5)) * 1.0) + vec2(0.5);
    coord = min(coord, vec2(1.0) - coord) * vec2(1.0, LIBRA_UNIFORM_SourceSize.y / LIBRA_UNIFORM_SourceSize.x);
    vec2 cdist = vec2(LIBRA_UNIFORM_corner, LIBRA_UNIFORM_corner);
    coord = cdist - min(coord, cdist);
    float dist = sqrt(dot(coord, coord));
    return clamp((cdist.x - dist) * LIBRA_UNIFORM_smoothness, 0.0, 1.0);
}

float _noise(vec2 co)
{
    return fract(sin((float(LIBRA_UNIFORM_FrameCount) / 60.0) * dot(co, vec2(12.98980045318603515625, 78.233001708984375))) * 43758.546875);
}

mat4 contrastMatrix(float contrast)
{
    float t = (1.0 - LIBRA_UNIFORM_contrast) / 2.0;
    return mat4(vec4(LIBRA_UNIFORM_contrast, 0.0, 0.0, 0.0), vec4(0.0, LIBRA_UNIFORM_contrast, 0.0, 0.0), vec4(0.0, 0.0, LIBRA_UNIFORM_contrast, 0.0), vec4(t, t, t, 1.0));
}

mat3 vign(float l)
{
    vec2 vpos = LIBRA_VARYING_0;
    vpos *= (vec2(1.0) - vpos);
    float vig = (vpos.x * vpos.y) * LIBRA_UNIFORM_vstr;
    vig = min(pow(vig, LIBRA_UNIFORM_vpower), 1.0);
    if (LIBRA_UNIFORM_vignette == 0.0)
    {
        vig = 1.0;
    }
    return mat3(vec3(vig, 0.0, 0.0), vec3(0.0, vig, 0.0), vec3(0.0, 0.0, vig));
}

void main()
{
    vec2 param = LIBRA_VARYING_0;
    vec2 _1030 = Warp(param);
    vec2 pos = _1030;
    vec2 tex_size = LIBRA_UNIFORM_SourceSize.xy;
    vec2 pC4 = pos + (vec2(0.5) / tex_size);
    vec2 fp = fract(pos * tex_size);
    bool _1049 = LIBRA_UNIFORM_inter < 0.5;
    bool _1056;
    if (_1049)
    {
        _1056 = tex_size.y > 400.0;
    }
    else
    {
        _1056 = _1049;
    }
    if (_1056)
    {
        fp.y = fract(((pos.y * tex_size.y) * 1.0) / LIBRA_UNIFORM_Downscale);
    }
    vec4 res = vec4(1.0);
    if (LIBRA_UNIFORM_alloff == 1.0)
    {
        res = texture(LIBRA_TEXTURE_Source, pC4);
    }
    else
    {
        vec2 texel = pos * tex_size;
        vec2 texel_floored = floor(texel);
        float scale = LIBRA_UNIFORM_PRE_SCALE;
        float region_range = 0.5 - (0.5 / scale);
        vec2 center_dist = fp - vec2(0.5);
        vec2 fpp = ((center_dist - clamp(center_dist, vec2(-region_range), vec2(region_range))) * scale) + vec2(0.5);
        vec2 mod_texel = texel_floored + fpp;
        vec2 coords = mod_texel / LIBRA_UNIFORM_SourceSize.xy;
        vec3 sample1 = texture(LIBRA_TEXTURE_Source, vec2(coords.x + (LIBRA_UNIFORM_blurx * LIBRA_UNIFORM_SourceSize.z), coords.y - (LIBRA_UNIFORM_blury * LIBRA_UNIFORM_SourceSize.w))).xyz;
        vec3 sample2 = texture(LIBRA_TEXTURE_Source, coords).xyz;
        vec3 sample3 = texture(LIBRA_TEXTURE_Source, vec2(coords.x - (LIBRA_UNIFORM_blurx * LIBRA_UNIFORM_SourceSize.z), coords.y + (LIBRA_UNIFORM_blury * LIBRA_UNIFORM_SourceSize.w))).xyz;
        vec3 color = vec3((sample1.x * 0.5) + (sample2.x * 0.5), ((sample1.y * 0.25) + (sample2.y * 0.5)) + (sample3.y * 0.25), (sample2.z * 0.5) + (sample3.z * 0.5));
        if (LIBRA_UNIFORM_palette_fix != 0.0)
        {
            if (LIBRA_UNIFORM_palette_fix == 1.0)
            {
                color *= 1.066699981689453125;
            }
            else
            {
                if (LIBRA_UNIFORM_palette_fix == 2.0)
                {
                    color *= 2.0;
                }
            }
        }
        if (LIBRA_UNIFORM_WP != 0.0)
        {
            vec3 warmer = mat3(vec3(0.4552772939205169677734375, 0.23230250179767608642578125, 0.01454569958150386810302734375), vec3(0.3675499856472015380859375, 0.707795619964599609375, 0.104915402829647064208984375), vec3(0.14139260351657867431640625, 0.059901900589466094970703125, 0.70574891567230224609375)) * color;
            warmer = mat3(vec3(3.062897205352783203125, -0.969265997409820556640625, 0.0678775012493133544921875), vec3(-1.39317905902862548828125, 1.87601077556610107421875, -0.2288548052310943603515625), vec3(-0.475751698017120361328125, 0.04155600070953369140625, 1.0693490505218505859375)) * warmer;
            vec3 cooler = mat3(vec3(0.4306190013885498046875, 0.22203789651393890380859375, 0.020185299217700958251953125), vec3(0.34154188632965087890625, 0.706638395786285400390625, 0.129550397396087646484375), vec3(0.17830909788608551025390625, 0.0713236033916473388671875, 0.93909442424774169921875)) * color;
            cooler = mat3(vec3(2.960394382476806640625, -0.978768408298492431640625, 0.084487400949001312255859375), vec3(-1.4678518772125244140625, 1.916141510009765625, -0.25459730625152587890625), vec3(-0.46851050853729248046875, 0.033454000949859619140625, 1.42161738872528076171875)) * cooler;
            float m = abs(LIBRA_UNIFORM_WP) / 100.0;
            bvec3 _1305 = bvec3(LIBRA_UNIFORM_WP < 0.0);
            vec3 comp = vec3(_1305.x ? cooler.x : warmer.x, _1305.y ? cooler.y : warmer.y, _1305.z ? cooler.z : warmer.z);
            comp = clamp(comp, vec3(0.0), vec3(1.0));
            color = mix(color, comp, vec3(m));
        }
        mat3 hue = mat3(vec3(1.0, LIBRA_UNIFORM_rg, LIBRA_UNIFORM_rb), vec3(LIBRA_UNIFORM_gr, 1.0, LIBRA_UNIFORM_gb), vec3(LIBRA_UNIFORM_br, LIBRA_UNIFORM_bg, 1.0));
        color = hue * color;
        color = (pow(color, vec3(2.7999999523162841796875)) * 2.0) - pow(color, vec3(3.599999904632568359375));
        float lum = ((color.x * 0.300000011920928955078125) + (color.y * 0.60000002384185791015625)) + (color.z * 0.100000001490116119384765625);
        float f = fract(fp.y - 0.5);
        bool _1368 = LIBRA_UNIFORM_inter > 0.5;
        bool _1374;
        if (_1368)
        {
            _1374 = tex_size.y > 400.0;
        }
        else
        {
            _1374 = _1368;
        }
        if (_1374)
        {
            color = color;
        }
        else
        {
            float param_1 = f;
            float param_2 = lum;
            float param_3 = 1.0 - f;
            float param_4 = lum;
            color = (color * sw(param_1, param_2)) + (color * sw(param_3, param_4));
        }
        float lum1 = ((color.x * 0.300000011920928955078125) + (color.y * 0.60000002384185791015625)) + (color.z * 0.100000001490116119384765625);
        vec2 param_5 = LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy;
        vec3 param_6 = color;
        float param_7 = lum1;
        vec3 _1417 = mask(param_5, param_6, param_7);
        color *= mix(_1417, vec3(1.0), vec3(lum1 * LIBRA_UNIFORM_preserve));
        if (LIBRA_UNIFORM_slotmask != 0.0)
        {
            vec2 param_8 = (LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy) * 1.00010001659393310546875;
            vec3 param_9 = color;
            float _1442 = SlotMask(param_8, param_9);
            color *= _1442;
        }
        color *= mix(LIBRA_UNIFORM_brightboost1, LIBRA_UNIFORM_brightboost2, max(max(color.x, color.y), color.z));
        color = pow(color, vec3(1.0 / LIBRA_UNIFORM_GAMMA_OUT));
        if (LIBRA_UNIFORM_glow_str != 0.0)
        {
            vec2 param_10 = coords;
            vec3 param_11 = color;
            color += glow0(param_10, param_11);
        }
        if (LIBRA_UNIFORM_sat != 1.0)
        {
            vec3 param_12 = color;
            color = saturation(param_12);
        }
        if (LIBRA_UNIFORM_corner != 0.0)
        {
            vec2 param_13 = pC4;
            float _1495 = corner0(param_13);
            color *= _1495;
        }
        if (LIBRA_UNIFORM_nois != 0.0)
        {
            vec2 param_14 = coords * 2.0;
            color *= (1.0 + (_noise(param_14) / LIBRA_UNIFORM_nois));
        }
        color *= mix(1.0, LIBRA_UNIFORM_postbr, lum);
        res = vec4(color, 1.0);
        if (LIBRA_UNIFORM_contrast != 1.0)
        {
            float param_15 = LIBRA_UNIFORM_contrast;
            res = contrastMatrix(param_15) * res;
        }
        bool _1539 = LIBRA_UNIFORM_inter > 0.5;
        bool _1545;
        if (_1539)
        {
            _1545 = LIBRA_UNIFORM_SourceSize.y > 400.0;
        }
        else
        {
            _1545 = _1539;
        }
        bool _1554;
        if (_1545)
        {
            _1554 = fract(float(LIBRA_UNIFORM_FrameCount) / 2.0) < 0.5;
        }
        else
        {
            _1554 = _1545;
        }
        if (_1554)
        {
            res *= 0.949999988079071044921875;
        }
        float param_16 = lum;
        vec4 _1563 = res;
        vec3 _1565 = _1563.xyz * vign(param_16);
        res.x = _1565.x;
        res.y = _1565.y;
        res.z = _1565.z;
    }
    FragColor = res;
}
