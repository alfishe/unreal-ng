#version 330

const float _585[12] = float[](4.0, 2.0, 1.0, 1.0, 7.0, 4.0, 3.0, 2.0, 13.0, 7.0, 5.0, 4.0);
const uint _618[39] = uint[](1u, 1u, 9u, 1u, 1u, 9u, 1u, 1u, 9u, 1u, 1u, 9u, 5u, 9u, 5u, 5u, 9u, 5u, 5u, 9u, 5u, 5u, 9u, 5u, 9u, 5u, 1u, 9u, 5u, 1u, 9u, 5u, 1u, 9u, 5u, 1u, 0u, 0u, 0u);
const uint _658[36] = uint[](2385u, 1425u, 345u, 354u, 594u, 2181u, 147u, 147u, 147u, 147u, 147u, 147u, 10048785u, 5609745u, 1136025u, 2385u, 1425u, 345u, 345u, 405u, 2385u, 354u, 594u, 2181u, 0u, 0u, 0u, 10048785u, 5609745u, 1136025u, 2516097u, 1468545u, 338313u, 2385u, 1425u, 345u);
const uint _678[12] = uint[](4u, 6u, 0u, 0u, 4u, 4u, 4u, 6u, 0u, 4u, 6u, 4u);
const uint _691[12] = uint[](15u, 63u, 0u, 0u, 15u, 15u, 15u, 63u, 0u, 15u, 63u, 15u);
const float _724[12] = float[](6.0, 2.0, 1.0, 1.0, 12.0, 6.0, 2.0, 2.0, 12.0, 12.0, 6.0, 6.0);
const float _731[12] = float[](4.0, 2.0, 1.0, 1.0, 8.0, 4.0, 2.0, 2.0, 8.0, 8.0, 4.0, 4.0);
const uint _772[36] = uint[](5869845u, 9785625u, 5314965u, 354u, 594u, 2181u, 147u, 147u, 147u, 147u, 147u, 147u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
const uint _785[36] = uint[](1136025u, 1153365u, 10048785u, 2181u, 1161u, 354u, 147u, 147u, 147u, 147u, 147u, 147u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
const float _1328[12] = float[](4.0, 2.0, 1.0, 1.0, 7.0, 4.0, 3.0, 2.0, 7.0, 7.0, 5.0, 4.0);
const float _1334[12] = float[](4.0, 4.0, 1.0, 1.0, 8.0, 6.0, 4.0, 4.0, 6.0, 6.0, 4.0, 4.0);
const uint _1447[36] = uint[](2385u, 1425u, 345u, 354u, 594u, 2181u, 147u, 147u, 147u, 147u, 147u, 147u, 10048785u, 5609745u, 1136025u, 2385u, 1425u, 345u, 345u, 405u, 2385u, 354u, 594u, 2181u, 10048785u, 5609745u, 1136025u, 10048785u, 5609745u, 1136025u, 2516097u, 1468545u, 338313u, 2385u, 1425u, 345u);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_HDRMode;
uniform float LIBRA_UNIFORM_BrightnessNits;
uniform float LIBRA_UNIFORM_hcrt_r709_gamma_out;
uniform float LIBRA_UNIFORM_hcrt_srgb_gamma_out;
uniform float LIBRA_UNIFORM_hcrt_p3_gamma_out;
uniform float LIBRA_UNIFORM_hcrt_adobe_gamma_out;
uniform float LIBRA_UNIFORM_hcrt_r709_gamma_cutoff;
uniform float LIBRA_UNIFORM_hcrt_srgb_gamma_cutoff;
uniform float LIBRA_UNIFORM_hcrt_h_size;
uniform float LIBRA_UNIFORM_hcrt_v_size;
uniform float LIBRA_UNIFORM_hcrt_h_cent;
uniform float LIBRA_UNIFORM_hcrt_v_cent;

uniform float LIBRA_UNIFORM_hcrt_colour_space;
uniform float LIBRA_UNIFORM_hcrt_max_nits;
uniform float LIBRA_UNIFORM_hcrt_lcd_resolution;
uniform float LIBRA_UNIFORM_hcrt_lcd_subpixel;
uniform float LIBRA_UNIFORM_hcrt_red_vertical_convergence;
uniform float LIBRA_UNIFORM_hcrt_green_vertical_convergence;
uniform float LIBRA_UNIFORM_hcrt_blue_vertical_convergence;
uniform float LIBRA_UNIFORM_hcrt_red_horizontal_convergence;
uniform float LIBRA_UNIFORM_hcrt_green_horizontal_convergence;
uniform float LIBRA_UNIFORM_hcrt_blue_horizontal_convergence;
uniform float LIBRA_UNIFORM_hcrt_bloom_strength;
uniform float LIBRA_UNIFORM_hcrt_crt_screen_type;
uniform float LIBRA_UNIFORM_hcrt_crt_resolution;
uniform float LIBRA_UNIFORM_hcrt_red_scanline_min;
uniform float LIBRA_UNIFORM_hcrt_red_scanline_max;
uniform float LIBRA_UNIFORM_hcrt_red_scanline_attack;
uniform float LIBRA_UNIFORM_hcrt_green_scanline_min;
uniform float LIBRA_UNIFORM_hcrt_green_scanline_max;
uniform float LIBRA_UNIFORM_hcrt_green_scanline_attack;
uniform float LIBRA_UNIFORM_hcrt_blue_scanline_min;
uniform float LIBRA_UNIFORM_hcrt_blue_scanline_max;
uniform float LIBRA_UNIFORM_hcrt_blue_scanline_attack;
uniform float LIBRA_UNIFORM_hcrt_red_beam_sharpness;
uniform float LIBRA_UNIFORM_hcrt_red_beam_attack;
uniform float LIBRA_UNIFORM_hcrt_green_beam_sharpness;
uniform float LIBRA_UNIFORM_hcrt_green_beam_attack;
uniform float LIBRA_UNIFORM_hcrt_blue_beam_sharpness;
uniform float LIBRA_UNIFORM_hcrt_blue_beam_attack;

uniform sampler2D LIBRA_TEXTURE_SourceSDR;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

uint GetApertureGrilleMask(uint lcd_resolution, uint crt_resolution, uint lcd_subpixel, vec2 current_pos)
{
    int index = int((lcd_resolution * 4u) + crt_resolution);
    float maskSize = _585[index];
    uint mask = uint(floor(mod(current_pos.x, maskSize)));
    if ((lcd_resolution == 2u) && (crt_resolution == 0u))
    {
        if (mask < 13u)
        {
            return _618[(int(mask)) * 3 + int(lcd_subpixel)];
        }
        return uint(int(mask < 13u));
    }
    uint pattern = _658[((int(lcd_resolution)) * 4 + int(crt_resolution)) * 3 + int(lcd_subpixel)];
    if (pattern == 147u)
    {
        return 147u;
    }
    uint shift = _678[(int(lcd_resolution)) * 4 + int(crt_resolution)];
    uint mask_bits = _691[(int(lcd_resolution)) * 4 + int(crt_resolution)];
    if ((shift == 0u) || (mask_bits == 0u))
    {
        return pattern;
    }
    return (pattern >> (mask * shift)) & mask_bits;
}

uint GetShadowMask(uint lcd_resolution, uint crt_resolution, uint lcd_subpixel, vec2 current_pos)
{
    int index = int((lcd_resolution * 4u) + crt_resolution);
    float maskSizeX = _724[index];
    float maskSizeY = _731[index];
    uint mask = uint(floor(mod(current_pos.x, maskSizeX)));
    uint shadow_y = uint(floor(mod(current_pos.y, maskSizeY)));
    if (lcd_resolution == 0u)
    {
        uint split_point = uint((crt_resolution == 1u) ? 1 : 2);
        uint pattern;
        if (shadow_y < split_point)
        {
            pattern = _772[((int(lcd_resolution)) * 4 + int(crt_resolution)) * 3 + int(lcd_subpixel)];
        }
        else
        {
            pattern = _785[((int(lcd_resolution)) * 4 + int(crt_resolution)) * 3 + int(lcd_subpixel)];
        }
        if ((crt_resolution == 2u) || (crt_resolution == 3u))
        {
            return pattern;
        }
        uint shift_amount = uint((crt_resolution == 0u) ? 4 : 6);
        uint mask_bits = uint((crt_resolution == 0u) ? 15 : 63);
        return (pattern >> (mask * shift_amount)) & mask_bits;
    }
    if (lcd_resolution == 1u)
    {
        if (crt_resolution == 0u)
        {
            if (shadow_y < 4u)
            {
                if (lcd_subpixel == 0u)
                {
                    int _841;
                    if (mask < 12u)
                    {
                        int _846;
                        if (mask < 2u)
                        {
                            _846 = 5;
                        }
                        else
                        {
                            int _853;
                            if (mask < 6u)
                            {
                                _853 = 1;
                            }
                            else
                            {
                                _853 = (mask < 10u) ? 9 : 5;
                            }
                            _846 = _853;
                        }
                        _841 = _846;
                    }
                    else
                    {
                        _841 = 0;
                    }
                    return uint(_841);
                }
                if (lcd_subpixel == 1u)
                {
                    int _873;
                    if (mask < 12u)
                    {
                        int _878;
                        if (mask < 2u)
                        {
                            _878 = 9;
                        }
                        else
                        {
                            int _884;
                            if (mask < 6u)
                            {
                                _884 = 1;
                            }
                            else
                            {
                                _884 = (mask < 10u) ? 5 : 9;
                            }
                            _878 = _884;
                        }
                        _873 = _878;
                    }
                    else
                    {
                        _873 = 0;
                    }
                    return uint(_873);
                }
                if (lcd_subpixel == 2u)
                {
                    int _903;
                    if (mask < 12u)
                    {
                        int _908;
                        if (mask < 2u)
                        {
                            _908 = 5;
                        }
                        else
                        {
                            int _914;
                            if (mask < 6u)
                            {
                                _914 = 9;
                            }
                            else
                            {
                                _914 = (mask < 10u) ? 1 : 5;
                            }
                            _908 = _914;
                        }
                        _903 = _908;
                    }
                    else
                    {
                        _903 = 0;
                    }
                    return uint(_903);
                }
            }
            else
            {
                if (lcd_subpixel == 0u)
                {
                    int _934;
                    if (mask < 12u)
                    {
                        int _939;
                        if (mask < 4u)
                        {
                            _939 = 9;
                        }
                        else
                        {
                            _939 = (mask < 8u) ? 5 : 1;
                        }
                        _934 = _939;
                    }
                    else
                    {
                        _934 = 0;
                    }
                    return uint(_934);
                }
                if (lcd_subpixel == 1u)
                {
                    int _958;
                    if (mask < 12u)
                    {
                        int _963;
                        if (mask < 4u)
                        {
                            _963 = 5;
                        }
                        else
                        {
                            _963 = (mask < 8u) ? 9 : 1;
                        }
                        _958 = _963;
                    }
                    else
                    {
                        _958 = 0;
                    }
                    return uint(_958);
                }
                if (lcd_subpixel == 2u)
                {
                    int _981;
                    if (mask < 12u)
                    {
                        int _986;
                        if (mask < 4u)
                        {
                            _986 = 1;
                        }
                        else
                        {
                            _986 = (mask < 8u) ? 5 : 9;
                        }
                        _981 = _986;
                    }
                    else
                    {
                        _981 = 0;
                    }
                    return uint(_981);
                }
            }
            return 0u;
        }
        else
        {
            if (shadow_y < uint((crt_resolution == 1u) ? 2 : 1))
            {
                if (crt_resolution == 1u)
                {
                    int _1015;
                    if (lcd_subpixel == 0u)
                    {
                        _1015 = 5869845;
                    }
                    else
                    {
                        _1015 = (lcd_subpixel == 1u) ? 9785625 : 5314965;
                    }
                    uint pattern_1 = uint(_1015);
                    return (pattern_1 >> (mask * 4u)) & 15u;
                }
                else
                {
                    int _1037;
                    if (lcd_subpixel == 0u)
                    {
                        _1037 = 354;
                    }
                    else
                    {
                        _1037 = (lcd_subpixel == 1u) ? 594 : 2181;
                    }
                    uint pattern_2 = uint(_1037);
                    return (pattern_2 >> (mask * 6u)) & 63u;
                }
            }
            else
            {
                if (crt_resolution == 1u)
                {
                    int _1063;
                    if (lcd_subpixel == 0u)
                    {
                        _1063 = 1136025;
                    }
                    else
                    {
                        _1063 = (lcd_subpixel == 1u) ? 1153365 : 10048785;
                    }
                    uint pattern_3 = uint(_1063);
                    return (pattern_3 >> (mask * 4u)) & 15u;
                }
                else
                {
                    int _1085;
                    if (lcd_subpixel == 0u)
                    {
                        _1085 = 2181;
                    }
                    else
                    {
                        _1085 = (lcd_subpixel == 1u) ? 1161 : 354;
                    }
                    uint pattern_4 = uint(_1085);
                    return (pattern_4 >> (mask * 6u)) & 63u;
                }
            }
        }
    }
    if (lcd_resolution == 2u)
    {
        if ((crt_resolution == 0u) || (crt_resolution == 1u))
        {
            if (shadow_y < 4u)
            {
                if (lcd_subpixel == 0u)
                {
                    int _1122;
                    if (mask < 12u)
                    {
                        int _1127;
                        if (mask < 2u)
                        {
                            _1127 = 5;
                        }
                        else
                        {
                            int _1133;
                            if (mask < 6u)
                            {
                                _1133 = 1;
                            }
                            else
                            {
                                _1133 = (mask < 10u) ? 9 : 5;
                            }
                            _1127 = _1133;
                        }
                        _1122 = _1127;
                    }
                    else
                    {
                        _1122 = 0;
                    }
                    return uint(_1122);
                }
                if (lcd_subpixel == 1u)
                {
                    int _1152;
                    if (mask < 12u)
                    {
                        int _1157;
                        if (mask < 2u)
                        {
                            _1157 = 9;
                        }
                        else
                        {
                            int _1163;
                            if (mask < 6u)
                            {
                                _1163 = 1;
                            }
                            else
                            {
                                _1163 = (mask < 10u) ? 5 : 9;
                            }
                            _1157 = _1163;
                        }
                        _1152 = _1157;
                    }
                    else
                    {
                        _1152 = 0;
                    }
                    return uint(_1152);
                }
                if (lcd_subpixel == 2u)
                {
                    int _1182;
                    if (mask < 12u)
                    {
                        int _1187;
                        if (mask < 2u)
                        {
                            _1187 = 5;
                        }
                        else
                        {
                            int _1193;
                            if (mask < 6u)
                            {
                                _1193 = 9;
                            }
                            else
                            {
                                _1193 = (mask < 10u) ? 1 : 5;
                            }
                            _1187 = _1193;
                        }
                        _1182 = _1187;
                    }
                    else
                    {
                        _1182 = 0;
                    }
                    return uint(_1182);
                }
            }
            else
            {
                if (lcd_subpixel == 0u)
                {
                    int _1213;
                    if (mask < 12u)
                    {
                        int _1218;
                        if (mask < 4u)
                        {
                            _1218 = 9;
                        }
                        else
                        {
                            _1218 = (mask < 8u) ? 5 : 1;
                        }
                        _1213 = _1218;
                    }
                    else
                    {
                        _1213 = 0;
                    }
                    return uint(_1213);
                }
                if (lcd_subpixel == 1u)
                {
                    int _1236;
                    if (mask < 12u)
                    {
                        int _1241;
                        if (mask < 4u)
                        {
                            _1241 = 5;
                        }
                        else
                        {
                            _1241 = (mask < 8u) ? 9 : 1;
                        }
                        _1236 = _1241;
                    }
                    else
                    {
                        _1236 = 0;
                    }
                    return uint(_1236);
                }
                if (lcd_subpixel == 2u)
                {
                    int _1259;
                    if (mask < 12u)
                    {
                        int _1264;
                        if (mask < 4u)
                        {
                            _1264 = 1;
                        }
                        else
                        {
                            _1264 = (mask < 8u) ? 5 : 9;
                        }
                        _1259 = _1264;
                    }
                    else
                    {
                        _1259 = 0;
                    }
                    return uint(_1259);
                }
            }
            return 0u;
        }
        else
        {
            if (shadow_y < 2u)
            {
                int _1285;
                if (lcd_subpixel == 0u)
                {
                    _1285 = 5869845;
                }
                else
                {
                    _1285 = (lcd_subpixel == 1u) ? 9785625 : 5314965;
                }
                uint pattern_5 = uint(_1285);
                return (pattern_5 >> (mask * 4u)) & 15u;
            }
            else
            {
                int _1304;
                if (lcd_subpixel == 0u)
                {
                    _1304 = 1136025;
                }
                else
                {
                    _1304 = (lcd_subpixel == 1u) ? 1153365 : 10048785;
                }
                uint pattern_6 = uint(_1304);
                return (pattern_6 >> (mask * 4u)) & 15u;
            }
        }
    }
    return 0u;
}

uint GetSlotMask(uint lcd_resolution, uint crt_resolution, uint lcd_subpixel, vec2 current_pos)
{
    int index = int((lcd_resolution * 4u) + crt_resolution);
    float maskSizeX = _1328[index];
    float maskSizeY = _1334[index];
    uint slot_x = uint(floor(mod(current_pos.x / maskSizeX, 2.0)));
    uint slot_y = uint(floor(mod(current_pos.y, maskSizeY)));
    uint element = (slot_y * 2u) + slot_x;
    uint mask = uint(floor(mod(current_pos.x, maskSizeX)));
    if (lcd_resolution == 0u)
    {
        if ((element == 3u) || (element == 6u))
        {
            return 0u;
        }
    }
    else
    {
        if (lcd_resolution == 1u)
        {
            if (crt_resolution == 0u)
            {
                if ((element == 7u) || (element == 14u))
                {
                    return 0u;
                }
            }
            else
            {
                if (crt_resolution == 1u)
                {
                    if ((element == 5u) || (element == 10u))
                    {
                        return 0u;
                    }
                }
                else
                {
                    if ((element == 3u) || (element == 6u))
                    {
                        return 0u;
                    }
                }
            }
        }
        else
        {
            if (lcd_resolution == 2u)
            {
                if (crt_resolution == 2u)
                {
                    if ((element == 3u) || (element == 6u))
                    {
                        return 0u;
                    }
                }
                else
                {
                    if ((element == 5u) || (element == 10u))
                    {
                        return 0u;
                    }
                }
            }
        }
    }
    uint pattern = _1447[((int(lcd_resolution)) * 4 + int(crt_resolution)) * 3 + int(lcd_subpixel)];
    bool _1455 = lcd_resolution == 0u;
    bool _1463;
    if (_1455)
    {
        _1463 = (crt_resolution == 2u) || (crt_resolution == 3u);
    }
    else
    {
        _1463 = _1455;
    }
    if (_1463)
    {
        return 147u;
    }
    if (lcd_resolution == 1u)
    {
        if (crt_resolution == 2u)
        {
            return (pattern >> (mask * 4u)) & 15u;
        }
        else
        {
            if (crt_resolution == 3u)
            {
                return (pattern >> (mask * 6u)) & 63u;
            }
            else
            {
                return (pattern >> (mask * 4u)) & 15u;
            }
        }
    }
    else
    {
        if (lcd_resolution == 2u)
        {
            if (crt_resolution == 2u)
            {
                return (pattern >> (mask * 6u)) & 63u;
            }
            else
            {
                return (pattern >> (mask * 4u)) & 15u;
            }
        }
        else
        {
            if (lcd_resolution == 0u)
            {
                if (crt_resolution == 0u)
                {
                    return (pattern >> (mask * 4u)) & 15u;
                }
                else
                {
                    if (crt_resolution == 1u)
                    {
                        return (pattern >> (mask * 6u)) & 63u;
                    }
                }
            }
        }
    }
    return (pattern >> (mask * 4u)) & 15u;
}

vec4 BeamControlPoints(float beam_attack, bool falloff)
{
    float inner_attack = clamp(beam_attack, 0.0, 1.0);
    float outer_attack = clamp(beam_attack - 1.0, 0.0, 1.0);
    vec4 _186;
    if (falloff)
    {
        _186 = vec4(0.0, 0.0, 0.0, 1.0) + vec4(0.0, outer_attack, inner_attack, 0.0);
    }
    else
    {
        _186 = vec4(0.0, 1.0, 1.0, 1.0) - vec4(0.0, inner_attack, outer_attack, 0.0);
    }
    return _186;
}

float Bezier(float t0, vec4 control_points)
{
    vec4 t = vec4(1.0, t0, t0 * t0, (t0 * t0) * t0);
    return dot(t, control_points * mat4(vec4(1.0, 0.0, 0.0, 0.0), vec4(-3.0, 3.0, 0.0, 0.0), vec4(3.0, -6.0, 3.0, 0.0), vec4(-1.0, 3.0, -3.0, 1.0)));
}

float ScanlineChannel(uint ch, vec2 tex_coord, vec2 source_size, float scanline_size, float source_tex_coord_x, float narrowed_source_pixel_offset, float vertical_convergence, float beam_attack, float scanline_min, float scanline_max, float scanline_attack, float vertical_bias)
{
    float current_source_position_y = (tex_coord.y * source_size.y) - vertical_convergence;
    float center_line = (floor(current_source_position_y) + 0.5) + vertical_bias;
    float distance_to_line = current_source_position_y - center_line;
    if (abs(distance_to_line) > 1.5)
    {
        return 0.0;
    }
    float source_tex_coord_y = center_line / source_size.y;
    vec2 tc0 = vec2(source_tex_coord_x, source_tex_coord_y);
    vec2 tc1 = vec2(source_tex_coord_x + (1.0 / source_size.x), source_tex_coord_y);
    float sdr_channel_0 = texture(LIBRA_TEXTURE_SourceSDR, tc0)[ch];
    float sdr_channel_1 = texture(LIBRA_TEXTURE_SourceSDR, tc1)[ch];
    float horiz_interp = Bezier(narrowed_source_pixel_offset, BeamControlPoints(beam_attack, sdr_channel_0 > sdr_channel_1));
    float sdr_channel = mix(sdr_channel_0, sdr_channel_1, horiz_interp);
    float signal_strength = clamp(sdr_channel, 0.0, 1.0);
    float beam_width_adjustment = 0.5 / scanline_size;
    float raw_distance = abs(distance_to_line) - beam_width_adjustment;
    float distance_adjusted = max(0.0, raw_distance);
    float effective_distance = distance_adjusted * 2.0;
    float beam_width = mix(scanline_min, scanline_max, signal_strength);
    float channel_scanline_distance = clamp(effective_distance / beam_width, 0.0, 1.0);
    vec4 channel_control_points = vec4(1.0, 1.0, signal_strength * scanline_attack, 0.0);
    float luminance = Bezier(channel_scanline_distance, channel_control_points);
    return luminance * sdr_channel;
}

vec3 ScanlineColour(vec2 tex_coord, vec2 source_size, float scanline_size, vec3 source_tex_coord_x, vec3 narrowed_source_pixel_offset, vec3 vertical_convergence, vec3 beam_attack, vec3 scanline_min, vec3 scanline_max, vec3 scanline_attack, float vertical_bias)
{
    return vec3(ScanlineChannel(0u, tex_coord, source_size, scanline_size, source_tex_coord_x.x, narrowed_source_pixel_offset.x, vertical_convergence.x, beam_attack.x, scanline_min.x, scanline_max.x, scanline_attack.x, vertical_bias), ScanlineChannel(1u, tex_coord, source_size, scanline_size, source_tex_coord_x.y, narrowed_source_pixel_offset.y, vertical_convergence.y, beam_attack.y, scanline_min.y, scanline_max.y, scanline_attack.y, vertical_bias), ScanlineChannel(2u, tex_coord, source_size, scanline_size, source_tex_coord_x.z, narrowed_source_pixel_offset.z, vertical_convergence.z, beam_attack.z, scanline_min.z, scanline_max.z, scanline_attack.z, vertical_bias));
}

vec3 GenerateScanline(vec2 tex_coord, vec2 source_size, float scanline_size, vec3 horizontal_convergence, vec3 vertical_convergence, vec3 beam_sharpness, vec3 beam_attack, vec3 scanline_min, vec3 scanline_max, vec3 scanline_attack)
{
    vec3 current_source_position_x = vec3(tex_coord.x * source_size.x) - horizontal_convergence;
    vec3 current_source_center_x = floor(current_source_position_x) + vec3(0.5);
    vec3 source_tex_coord_x = current_source_center_x / vec3(source_size.x);
    vec3 source_pixel_offset = fract(current_source_position_x);
    vec3 narrowed_source_pixel_offset = clamp(((source_pixel_offset - vec3(0.5)) * beam_sharpness) + vec3(0.5), vec3(0.0), vec3(1.0));
    vec3 total_light = ScanlineColour(tex_coord, source_size, scanline_size, source_tex_coord_x, narrowed_source_pixel_offset, vertical_convergence, beam_attack, scanline_min, scanline_max, scanline_attack, 0.0);
    total_light += ScanlineColour(tex_coord, source_size, scanline_size, source_tex_coord_x, narrowed_source_pixel_offset, vertical_convergence, beam_attack, scanline_min, scanline_max, scanline_attack, 1.0);
    total_light += ScanlineColour(tex_coord, source_size, scanline_size, source_tex_coord_x, narrowed_source_pixel_offset, vertical_convergence, beam_attack, scanline_min, scanline_max, scanline_attack, -1.0);
    return total_light;
}

vec3 To2020(vec3 linear_709)
{
    uint space = uint(LIBRA_UNIFORM_hcrt_colour_space);
    if (space == 4u)
    {
        return max(linear_709, vec3(0.0));
    }
    else
    {
        if (space == 3u)
        {
            return max(linear_709 * mat3(vec3(0.753832995891571044921875, 0.19859699904918670654296875, 0.0475700013339519500732421875), vec3(0.0457439981400966644287109375, 0.9417769908905029296875, 0.012478999793529510498046875), vec3(-0.001210000016726553440093994140625, 0.0176020003855228424072265625, 0.983609020709991455078125)), vec3(0.0));
        }
        else
        {
            if (space == 2u)
            {
                return max(linear_709 * mat3(vec3(0.87733399868011474609375, 0.077494002878665924072265625, 0.045171998441219329833984375), vec3(0.096623003482818603515625, 0.891526997089385986328125, 0.011850000359117984771728515625), vec3(0.0229209996759891510009765625, 0.0430370010435581207275390625, 0.9340419769287109375)), vec3(0.0));
            }
            else
            {
                return max(linear_709 * mat3(vec3(0.6274039745330810546875, 0.329281985759735107421875, 0.043313600122928619384765625), vec3(0.06909699738025665283203125, 0.919539988040924072265625, 0.0113612003624439239501953125), vec3(0.01639159955084323883056640625, 0.0880132019519805908203125, 0.895595014095306396484375)), vec3(0.0));
            }
        }
    }
}

vec3 InverseTonemap(vec3 sdr_linear, float max_nits, float paper_white_nits)
{
    float input_val = max(sdr_linear.x, max(sdr_linear.y, sdr_linear.z));
    if (input_val < 9.9999997473787516355514526367188e-05)
    {
        return sdr_linear;
    }
    float peak_ratio = max_nits / paper_white_nits;
    float numerator = input_val;
    float denominator = 1.0 - (input_val * (1.0 - (1.0 / peak_ratio)));
    float tonemapped_val = numerator / max(denominator, 9.9999997473787516355514526367188e-05);
    return sdr_linear * (tonemapped_val / input_val);
}

float LinearToST2084_1(float channel)
{
    float ST2084 = pow((0.8359375 + (18.8515625 * pow(channel, 0.1593017578125))) / (1.0 + (18.6875 * pow(channel, 0.1593017578125))), 78.84375);
    return ST2084;
}

vec3 LinearToST2084(vec3 colour)
{
    vec3 positive = max(colour, vec3(0.0));
    return vec3(LinearToST2084_1(positive.x), LinearToST2084_1(positive.y), LinearToST2084_1(positive.z));
}

float LinearTo709_1(float channel)
{
    float _450;
    if (channel >= (LIBRA_UNIFORM_hcrt_r709_gamma_cutoff * 0.001000000047497451305389404296875))
    {
        _450 = (1.09899997711181640625 * pow(channel, 1.0 / LIBRA_UNIFORM_hcrt_r709_gamma_out)) - 0.098999999463558197021484375;
    }
    else
    {
        _450 = channel * 4.5;
    }
    return _450;
}

vec3 LinearTo709(vec3 colour)
{
    return vec3(LinearTo709_1(colour.x), LinearTo709_1(colour.y), LinearTo709_1(colour.z));
}

float LinearTosRGB_1(float channel)
{
    float _418;
    if (channel > (LIBRA_UNIFORM_hcrt_srgb_gamma_cutoff * 0.001000000047497451305389404296875))
    {
        _418 = (1.05499994754791259765625 * pow(channel, 1.0 / LIBRA_UNIFORM_hcrt_srgb_gamma_out)) - 0.054999999701976776123046875;
    }
    else
    {
        _418 = channel * 12.9200000762939453125;
    }
    return _418;
}

vec3 LinearTosRGB(vec3 colour)
{
    return vec3(LinearTosRGB_1(colour.x), LinearTosRGB_1(colour.y), LinearTosRGB_1(colour.z));
}

vec3 LinearToAdobe(vec3 colour)
{
    return pow(colour, vec3(1.0 / LIBRA_UNIFORM_hcrt_adobe_gamma_out));
}

vec3 LinearToDCIP3(vec3 colour)
{
    return pow(colour, vec3(1.0 / LIBRA_UNIFORM_hcrt_p3_gamma_out));
}

void main()
{
    uint screen_type = uint(LIBRA_UNIFORM_hcrt_crt_screen_type);
    uint crt_resolution = uint(LIBRA_UNIFORM_hcrt_crt_resolution);
    uint lcd_resolution = uint(LIBRA_UNIFORM_hcrt_lcd_resolution);
    uint lcd_subpixel_layout = uint(LIBRA_UNIFORM_hcrt_lcd_subpixel);
    vec2 source_size = LIBRA_UNIFORM_SourceSize.xy;
    vec2 output_size = LIBRA_UNIFORM_OutputSize.xy;
    vec2 tex_coord = LIBRA_VARYING_0 - vec2(0.5);
    tex_coord *= vec2(LIBRA_UNIFORM_hcrt_h_size, LIBRA_UNIFORM_hcrt_v_size);
    tex_coord += vec2(0.5);
    tex_coord += (vec2(LIBRA_UNIFORM_hcrt_h_cent, LIBRA_UNIFORM_hcrt_v_cent) / output_size);
    vec2 current_position = LIBRA_VARYING_0 * output_size;
    uint colour_mask = 0u;
    if (screen_type == 0u)
    {
        uint param = lcd_resolution;
        uint param_1 = crt_resolution;
        uint param_2 = lcd_subpixel_layout;
        vec2 param_3 = current_position;
        colour_mask = GetApertureGrilleMask(param, param_1, param_2, param_3);
    }
    else
    {
        if (screen_type == 1u)
        {
            uint param_4 = lcd_resolution;
            uint param_5 = crt_resolution;
            uint param_6 = lcd_subpixel_layout;
            vec2 param_7 = current_position;
            colour_mask = GetShadowMask(param_4, param_5, param_6, param_7);
        }
        else
        {
            if (screen_type == 2u)
            {
                uint param_8 = lcd_resolution;
                uint param_9 = crt_resolution;
                uint param_10 = lcd_subpixel_layout;
                vec2 param_11 = current_position;
                colour_mask = GetSlotMask(param_8, param_9, param_10, param_11);
            }
        }
    }
    float scanline_size = output_size.y / source_size.y;
    vec3 horizontal_convergence = vec3(LIBRA_UNIFORM_hcrt_red_horizontal_convergence, LIBRA_UNIFORM_hcrt_green_horizontal_convergence, LIBRA_UNIFORM_hcrt_blue_horizontal_convergence);
    vec3 vertical_convergence = vec3(LIBRA_UNIFORM_hcrt_red_vertical_convergence, LIBRA_UNIFORM_hcrt_green_vertical_convergence, LIBRA_UNIFORM_hcrt_blue_vertical_convergence);
    vec3 beam_sharpness = vec3(LIBRA_UNIFORM_hcrt_red_beam_sharpness, LIBRA_UNIFORM_hcrt_green_beam_sharpness, LIBRA_UNIFORM_hcrt_blue_beam_sharpness);
    vec3 beam_attack = vec3(LIBRA_UNIFORM_hcrt_red_beam_attack, LIBRA_UNIFORM_hcrt_green_beam_attack, LIBRA_UNIFORM_hcrt_blue_beam_attack);
    vec3 scanline_min = vec3(LIBRA_UNIFORM_hcrt_red_scanline_min, LIBRA_UNIFORM_hcrt_green_scanline_min, LIBRA_UNIFORM_hcrt_blue_scanline_min);
    vec3 scanline_max = vec3(LIBRA_UNIFORM_hcrt_red_scanline_max, LIBRA_UNIFORM_hcrt_green_scanline_max, LIBRA_UNIFORM_hcrt_blue_scanline_max);
    vec3 scanline_attack = vec3(LIBRA_UNIFORM_hcrt_red_scanline_attack, LIBRA_UNIFORM_hcrt_green_scanline_attack, LIBRA_UNIFORM_hcrt_blue_scanline_attack);
    vec3 scanline_colour = GenerateScanline(tex_coord, source_size, scanline_size, horizontal_convergence, vertical_convergence, beam_sharpness, beam_attack, scanline_min, scanline_max, scanline_attack);
    vec3 linear_colour = max(scanline_colour, vec3(0.0));
    vec3 mask = vec3(0.0);
    uint channel_count = colour_mask & 3u;
    if (channel_count > 0u)
    {
        uint idx = (colour_mask >> uint(2)) & 3u;
        mask += vec3(float(idx == 0u), float(idx == 1u), float(idx == 2u));
    }
    if (channel_count > 1u)
    {
        uint idx_1 = (colour_mask >> uint(4)) & 3u;
        mask += vec3(float(idx_1 == 0u), float(idx_1 == 1u), float(idx_1 == 2u));
    }
    if (channel_count > 2u)
    {
        uint idx_2 = (colour_mask >> uint(6)) & 3u;
        mask += vec3(float(idx_2 == 0u), float(idx_2 == 1u), float(idx_2 == 2u));
    }
    if (LIBRA_UNIFORM_HDRMode > 0u)
    {
        linear_colour = To2020(linear_colour);
        if (LIBRA_UNIFORM_HDRMode == 1u)
        {
            linear_colour = InverseTonemap(linear_colour, LIBRA_UNIFORM_hcrt_max_nits, LIBRA_UNIFORM_BrightnessNits);
            linear_colour *= mask;
            vec3 pq_input = linear_colour * (LIBRA_UNIFORM_BrightnessNits / 10000.0);
            FragColor = vec4(LinearToST2084(pq_input), 1.0);
        }
        else
        {
            linear_colour *= mat3(vec3(1.66049098968505859375, -0.5876410007476806640625, -0.072849996387958526611328125), vec3(-0.12454999983310699462890625, 1.1328999996185302734375, -0.00834899954497814178466796875), vec3(-0.01815100014209747314453125, -0.100579001009464263916015625, 1.11872994899749755859375));
            linear_colour *= mask;
            FragColor = vec4(linear_colour * (LIBRA_UNIFORM_hcrt_max_nits / 80.0), 1.0);
        }
    }
    else
    {
        linear_colour *= mask;
        uint output_space = uint(LIBRA_UNIFORM_hcrt_colour_space);
        if (output_space == 0u)
        {
            FragColor = vec4(LinearTo709(linear_colour), 1.0);
        }
        else
        {
            if (output_space == 1u)
            {
                FragColor = vec4(LinearTosRGB(linear_colour), 1.0);
            }
            else
            {
                if (output_space == 2u)
                {
                    FragColor = vec4(LinearToAdobe(linear_colour), 1.0);
                }
                else
                {
                    if (output_space == 3u)
                    {
                        FragColor = vec4(LinearToDCIP3(linear_colour), 1.0);
                    }
                    else
                    {
                        FragColor = vec4(LinearTo709(linear_colour), 1.0);
                    }
                }
            }
        }
    }
}
