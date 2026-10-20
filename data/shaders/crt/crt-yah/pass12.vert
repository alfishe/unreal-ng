#version 330

const float _275[17] = float[](0.111111111938953399658203125, 0.125, 0.14285714924335479736328125, 0.16666667163372039794921875, 0.20000000298023223876953125, 0.25, 0.3333333432674407958984375, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_FrameTimeDelta;
uniform float LIBRA_UNIFORM_OriginalFPS;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_ORIENTATION;
uniform float LIBRA_UNIFORM_SCREEN_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_FREQUENCY;
uniform float LIBRA_UNIFORM_SCREEN_INTERLACED;
uniform float LIBRA_UNIFORM_SHARP_AMOUNT;
uniform float LIBRA_UNIFORM_HALATION_INTENSITY;
uniform float LIBRA_UNIFORM_HALATION_DIFFUSION;
uniform float LIBRA_UNIFORM_HALATION_WEIGHT;
uniform float LIBRA_UNIFORM_HALATION_INFLUENCE;

uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS;
uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS_FLICKER;
uniform float LIBRA_UNIFORM_COLOR_COMPENSATION;
uniform float LIBRA_UNIFORM_COLOR_SATURATION;
uniform float LIBRA_UNIFORM_COLOR_OVERFLOW;
uniform float LIBRA_UNIFORM_COLOR_CONTRAST;
uniform float LIBRA_UNIFORM_COLOR_PROFILE;
uniform float LIBRA_UNIFORM_COLOR_TEMPERATUE;
uniform float LIBRA_UNIFORM_COLOR_BLACK_LIGHT;
uniform float LIBRA_UNIFORM_SCANLINES_STRENGTH;
uniform float LIBRA_UNIFORM_SCANLINES_OFFSET;
uniform float LIBRA_UNIFORM_BEAM_WIDTH_MIN;
uniform float LIBRA_UNIFORM_BEAM_WIDTH_MAX;
uniform float LIBRA_UNIFORM_BEAM_SHAPE;
uniform float LIBRA_UNIFORM_BEAM_FILTER;
uniform float LIBRA_UNIFORM_ANTI_RINGING;
uniform float LIBRA_UNIFORM_SCANLINES_COLOR_BURN;
uniform float LIBRA_UNIFORM_MASK_INTENSITY;
uniform float LIBRA_UNIFORM_MASK_BLEND;
uniform float LIBRA_UNIFORM_MASK_SCALE;
uniform float LIBRA_UNIFORM_MASK_TYPE;
uniform float LIBRA_UNIFORM_MASK_SUBPIXEL;
uniform float LIBRA_UNIFORM_MASK_SUBPIXEL_ORDER;
uniform float LIBRA_UNIFORM_MASK_SUBPIXEL_SHAPE;
uniform float LIBRA_UNIFORM_MASK_COLOR_BLEED;
uniform float LIBRA_UNIFORM_CRT_CURVATURE_AMOUNT;
uniform float LIBRA_UNIFORM_CRT_VIGNETTE_AMOUNT;
uniform float LIBRA_UNIFORM_CRT_NOISE_AMOUNT;
uniform float LIBRA_UNIFORM_CRT_CORNER_RAIDUS;
uniform float LIBRA_UNIFORM_CRT_CORNER_SMOOTHNESS;

flat out int LIBRA_VARYING_3;
out vec3 LIBRA_VARYING_4;
out vec4 LIBRA_VARYING_6;
layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 Coord;
out vec2 LIBRA_VARYING_1;
out vec4 LIBRA_VARYING_7;
out mat4 LIBRA_VARYING_11;
out float LIBRA_VARYING_5;
out float LIBRA_VARYING_8;
out vec2 LIBRA_VARYING_9;
flat out uvec2 LIBRA_VARYING_10;
out vec2 LIBRA_VARYING_2;

bool is_portrait(vec2 size)
{
    return size.y > size.x;
}

int get_orientation(vec2 size)
{
    vec2 param = size;
    return int(is_portrait(param));
}

int get_orientation(vec2 size, int orientation)
{
    int _169;
    if (orientation > 0)
    {
        _169 = orientation - 1;
    }
    else
    {
        vec2 param = size;
        _169 = get_orientation(param);
    }
    return _169;
}

float get_ratio(vec2 size, int orientation)
{
    float _183;
    if (orientation > 0)
    {
        _183 = size.x / size.y;
    }
    else
    {
        _183 = size.y / size.x;
    }
    return _183;
}

float get_multiple(vec2 source_size, int orientation)
{
    vec2 param = source_size;
    int param_1 = orientation;
    float ratio = get_ratio(param, param_1);
    vec2 param_2 = source_size;
    float portrait = float(is_portrait(param_2));
    float base_size = (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 3) ? 480.0 : 240.0;
    float _224;
    if (orientation > 0)
    {
        _224 = mix(base_size * ratio, base_size, portrait);
    }
    else
    {
        _224 = mix(base_size, base_size * ratio, portrait);
    }
    base_size = _224;
    float _243;
    if (orientation > 0)
    {
        _243 = source_size.x / base_size;
    }
    else
    {
        _243 = source_size.y / base_size;
    }
    return _243;
}

float get_multiple_factor(float index)
{
    return mix(_275[int(clamp(floor(index), 0.0, 16.0))], _275[int(clamp(ceil(index), 0.0, 16.0))], fract(index));
}

float offset_multiple(inout float multiple, float pixel_size, float multiple_offset)
{
    bool _300 = multiple < 1.0;
    bool _316;
    if (_300)
    {
        bool _306 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 3;
        bool _315;
        if (!_306)
        {
            _315 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 5;
        }
        else
        {
            _315 = _306;
        }
        _316 = _315;
    }
    else
    {
        _316 = _300;
    }
    bool upscale = _316;
    float _sign = upscale ? (-1.0) : 1.0;
    float _323;
    if (upscale)
    {
        _323 = round((1.0 / multiple) * 2.0) * 0.5;
    }
    else
    {
        _323 = round(multiple);
    }
    float index = _323;
    float _345 = ((_sign * (max(1.0, index) - 1.0)) + 8.0) + multiple_offset;
    for (float multiple_index = _345; multiple_index < 17.0; multiple_index += 0.0500000007450580596923828125)
    {
        float param = multiple_index;
        multiple = get_multiple_factor(param);
        if ((pixel_size * multiple) >= 3.0)
        {
            break;
        }
    }
    return multiple;
}

float get_auto_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    vec2 param = source_size;
    int param_1 = source_orientation;
    float multiple = get_multiple(param, param_1);
    float _379;
    if (source_orientation > 0)
    {
        _379 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _379 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _379;
    float param_2 = multiple;
    float param_3 = pixel_size;
    float param_4 = multiple_offset;
    float _401 = offset_multiple(param_2, param_3, param_4);
    return _401;
}

float get_fixed_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float multiple = 1.0;
    float _408;
    if (source_orientation > 0)
    {
        _408 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _408 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _408;
    float param = multiple;
    float param_1 = pixel_size;
    float param_2 = multiple_offset;
    float _429 = offset_multiple(param, param_1, param_2);
    return _429;
}

float get_screen_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float _436;
    if (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 1)
    {
        vec2 param = source_size;
        int param_1 = source_orientation;
        float param_2 = multiple_offset;
        _436 = get_auto_multiple(param, param_1, param_2);
    }
    else
    {
        vec2 param_3 = source_size;
        int param_4 = source_orientation;
        float param_5 = multiple_offset;
        _436 = get_fixed_multiple(param_3, param_4, param_5);
    }
    return _436;
}

vec4 get_mask_profile()
{
    float _512;
    if (LIBRA_UNIFORM_OriginalSize.x < LIBRA_UNIFORM_OriginalSize.y)
    {
        _512 = LIBRA_UNIFORM_FinalViewportSize.x / LIBRA_UNIFORM_OriginalSize.x;
    }
    else
    {
        _512 = LIBRA_UNIFORM_FinalViewportSize.y / LIBRA_UNIFORM_OriginalSize.y;
    }
    float pixel_size = _512;
    float subpixel_count = (min(LIBRA_UNIFORM_OriginalSize.x, LIBRA_UNIFORM_OriginalSize.y) < 180.0) ? 3.0 : 4.0;
    float subpixel_downscale = floor(abs(LIBRA_UNIFORM_MASK_SCALE)) + 1.0;
    float subpixel_upscale = LIBRA_UNIFORM_MASK_SCALE + 1.0;
    float subpixel_size = pixel_size / subpixel_count;
    subpixel_size = floor(subpixel_size * LIBRA_VARYING_4.z);
    subpixel_size = max(1.0, subpixel_size);
    float _570;
    if (LIBRA_UNIFORM_MASK_SCALE < 0.0)
    {
        _570 = ceil(subpixel_size / subpixel_downscale);
    }
    else
    {
        _570 = floor(subpixel_size * subpixel_upscale);
    }
    subpixel_size = _570;
    subpixel_size = max(1.0, subpixel_size);
    float _591;
    if (int(LIBRA_UNIFORM_MASK_TYPE) == 1)
    {
        _591 = clamp((subpixel_size - 2.0) * 0.75, 0.0, 1.0);
    }
    else
    {
        float _605;
        if (int(LIBRA_UNIFORM_MASK_TYPE) == 2)
        {
            _605 = clamp((subpixel_size - 2.0) * 0.75, 0.0, 1.0);
        }
        else
        {
            float _617;
            if (int(LIBRA_UNIFORM_MASK_TYPE) == 3)
            {
                _617 = clamp((subpixel_size - 2.0) * 0.25, 0.0, 1.0);
            }
            else
            {
                _617 = 0.0;
            }
            _605 = _617;
        }
        _591 = _605;
    }
    float subpixel_smoothness = _591;
    subpixel_smoothness *= LIBRA_UNIFORM_MASK_SUBPIXEL_SHAPE;
    int subpixel_type = int(LIBRA_UNIFORM_MASK_SUBPIXEL);
    int subpixel_color_order = int(LIBRA_UNIFORM_MASK_SUBPIXEL_ORDER);
    return vec4(float(subpixel_type), subpixel_size, subpixel_smoothness, float(subpixel_color_order));
}

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
}

float normalized_sigmoid(float value, float slope)
{
    float numerator = (1.0 - slope) * value;
    float denominator = (1.0 + slope) - ((abs(value) * slope) * 2.0);
    return numerator / denominator;
}

vec4 get_beam_profile()
{
    float beam_min_width = 1.0;
    float beam_max_width = 1.0;
    float beam_slope = 2.0;
    float param_beam_shape_inverted = 1.0 - LIBRA_UNIFORM_BEAM_SHAPE;
    float param = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    float param_4 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 1.0;
    beam_slope += ((2.0 * (param_beam_shape_inverted * mix_master(param, param_1, param_2, param_3))) * ((param_beam_shape_inverted * mix_master(param_4, param_5, param_6, param_7)) + 1.0));
    float param_8 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_9 = 0.0;
    float param_10 = 0.0;
    float param_11 = 1.0;
    beam_min_width -= ((param_beam_shape_inverted * mix_master(param_8, param_9, param_10, param_11)) / 2.0);
    float param_12 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_13 = 0.0;
    float param_14 = 0.0;
    float param_15 = 1.0;
    beam_max_width -= ((param_beam_shape_inverted * mix_master(param_12, param_13, param_14, param_15)) / 2.0);
    float param_16 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_17 = 0.0;
    float param_18 = 0.0;
    float param_19 = 1.0;
    beam_min_width /= (1.0 + (((min(1.0, 1.0 - LIBRA_UNIFORM_BEAM_WIDTH_MIN) - min(0.0, LIBRA_UNIFORM_BEAM_WIDTH_MIN * 2.0)) * min(1.0, mix_master(param_16, param_17, param_18, param_19) * 2.0)) / 2.0));
    float param_20 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_21 = 0.0;
    float param_22 = 0.0;
    float param_23 = 1.0;
    beam_max_width *= (1.0 + ((max(0.0, LIBRA_UNIFORM_BEAM_WIDTH_MAX - 1.0) * min(1.0, mix_master(param_20, param_21, param_22, param_23) * 2.0)) / 2.0));
    float param_24 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_25 = 0.0;
    float param_26 = 0.0;
    float param_27 = 1.0;
    float param_28 = mix_master(param_24, param_25, param_26, param_27);
    float param_29 = 0.5;
    float scanlines_strength = normalized_sigmoid(param_28, param_29);
    scanlines_strength *= 1.25;
    scanlines_strength += 0.25;
    return vec4(beam_min_width, beam_max_width, beam_slope, scanlines_strength);
}

mat4 get_beam_filter()
{
    float param = LIBRA_UNIFORM_BEAM_FILTER;
    float param_1 = 0.0;
    float param_2 = -1.0;
    float param_3 = 1.0;
    float f = (mix_master(param, param_1, param_2, param_3) * 2.0) + 1.0;
    float b = 0.0;
    float c = 0.0;
    if (f <= 0.0)
    {
        b = f;
        c = 0.0;
    }
    else
    {
        if (f <= 1.0)
        {
            b = 0.0;
            c = f * 0.5;
        }
        else
        {
            if (f <= 2.0)
            {
                b = (f - 1.0) / 3.0;
                c = 0.5 - ((f - 1.0) / 6.0);
            }
            else
            {
                if (f <= 3.0)
                {
                    b = 0.3333333432674407958984375 + ((f - 2.0) / 3.0);
                    c = 0.3333333432674407958984375 - ((f - 2.0) / 6.0);
                }
            }
        }
    }
    return mat4(vec4(((-b) - (6.0 * c)) / 6.0, ((3.0 * b) + (12.0 * c)) / 6.0, (((-3.0) * b) - (6.0 * c)) / 6.0, b / 6.0), vec4(((12.0 - (9.0 * b)) - (6.0 * c)) / 6.0, (((-18.0) + (12.0 * b)) + (6.0 * c)) / 6.0, 0.0, (6.0 - (2.0 * b)) / 6.0), vec4((-((12.0 - (9.0 * b)) - (6.0 * c))) / 6.0, ((18.0 - (15.0 * b)) - (12.0 * c)) / 6.0, ((3.0 * b) + (6.0 * c)) / 6.0, b / 6.0), vec4((b + (6.0 * c)) / 6.0, -c, 0.0, 0.0));
}

float get_brightness_compensation()
{
    float brightness_compensation = 0.0;
    float param = LIBRA_UNIFORM_SCREEN_INTERLACED;
    float param_1 = 0.5;
    float screen_interlace = normalized_sigmoid(param, param_1);
    brightness_compensation += (screen_interlace * 2.0);
    float param_2 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_3 = 0.0;
    float param_4 = 0.0;
    float param_5 = 1.0;
    float param_6 = mix_master(param_2, param_3, param_4, param_5);
    float param_7 = 0.5;
    float scanlines_strength = normalized_sigmoid(param_6, param_7);
    brightness_compensation += (scanlines_strength * mix(0.5, 1.0, LIBRA_UNIFORM_BEAM_SHAPE));
    brightness_compensation -= ((scanlines_strength * 0.25) * (1.0 - abs((LIBRA_UNIFORM_BEAM_SHAPE * 2.0) - 1.0)));
    float param_8 = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_9 = 0.0;
    float param_10 = 0.0;
    float param_11 = 1.0;
    float param_12 = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_13 = 0.0;
    float param_14 = 0.0;
    float param_15 = 1.0;
    float param_16 = mix_master(param_8, param_9, param_10, param_11) * mix_master(param_12, param_13, param_14, param_15);
    float param_17 = 0.5;
    float mask_intensity = normalized_sigmoid(param_16, param_17);
    float mask_blend = 1.0 - ((1.0 - LIBRA_UNIFORM_MASK_BLEND) * (1.0 - LIBRA_UNIFORM_MASK_BLEND));
    int mask_size = int(LIBRA_VARYING_6.y);
    float mask_smoothness = LIBRA_VARYING_6.z;
    float _737;
    if (int(LIBRA_UNIFORM_MASK_SUBPIXEL) == 1)
    {
        _737 = mix(-1.0, -0.25, mask_blend);
    }
    else
    {
        float _748;
        if (int(LIBRA_UNIFORM_MASK_SUBPIXEL) == 2)
        {
            _748 = mix(-1.0, -0.25, mask_blend);
        }
        else
        {
            float _758;
            if (int(LIBRA_UNIFORM_MASK_SUBPIXEL) == 3)
            {
                _758 = mix(-0.4000000059604644775390625, -0.100000001490116119384765625, mask_blend);
            }
            else
            {
                float _770;
                if (int(LIBRA_UNIFORM_MASK_SUBPIXEL) == 4)
                {
                    _770 = 0.0;
                }
                else
                {
                    float _778;
                    if (int(LIBRA_UNIFORM_MASK_SUBPIXEL) == 5)
                    {
                        _778 = mix(0.60000002384185791015625, 0.1500000059604644775390625, mask_blend);
                    }
                    else
                    {
                        _778 = 0.0;
                    }
                    _770 = _778;
                }
                _758 = _770;
            }
            _748 = _758;
        }
        _737 = _748;
    }
    float subpixel_offset = _737;
    float _796;
    if (int(LIBRA_UNIFORM_MASK_TYPE) == 1)
    {
        _796 = mix(0.20000000298023223876953125, 0.0500000007450580596923828125, mask_blend);
    }
    else
    {
        float _806;
        if (int(LIBRA_UNIFORM_MASK_TYPE) == 2)
        {
            _806 = mix(0.800000011920928955078125, 0.20000000298023223876953125, mask_blend);
        }
        else
        {
            float _817;
            if (int(LIBRA_UNIFORM_MASK_TYPE) == 3)
            {
                _817 = mix(0.20000000298023223876953125, 0.0500000007450580596923828125, mask_blend);
            }
            else
            {
                _817 = 0.0;
            }
            _806 = _817;
        }
        _796 = _806;
    }
    float type_offset = _796;
    float _834;
    if ((int(LIBRA_UNIFORM_MASK_TYPE) == 1) && (mask_size > 2))
    {
        _834 = mix(-0.4000000059604644775390625, -0.100000001490116119384765625, mask_blend);
    }
    else
    {
        float _847;
        if ((int(LIBRA_UNIFORM_MASK_TYPE) == 2) && (mask_size > 2))
        {
            _847 = mix(-0.4000000059604644775390625, -0.100000001490116119384765625, mask_blend);
        }
        else
        {
            float _860;
            if ((int(LIBRA_UNIFORM_MASK_TYPE) == 3) && (mask_size > 2))
            {
                _860 = mix(0.4000000059604644775390625, 0.100000001490116119384765625, mask_blend);
            }
            else
            {
                _860 = 0.0;
            }
            _847 = _860;
        }
        _834 = _847;
    }
    float size_offset = _834;
    float smoothness_offset = mask_smoothness;
    float _877;
    if (int(LIBRA_UNIFORM_MASK_TYPE) == 1)
    {
        _877 = mix(1.60000002384185791015625, 0.4000000059604644775390625, mask_blend);
    }
    else
    {
        float _888;
        if (int(LIBRA_UNIFORM_MASK_TYPE) == 2)
        {
            _888 = mix(1.60000002384185791015625, 0.4000000059604644775390625, mask_blend);
        }
        else
        {
            float _898;
            if (int(LIBRA_UNIFORM_MASK_TYPE) == 3)
            {
                _898 = mix(2.400000095367431640625, 0.60000002384185791015625, mask_blend);
            }
            else
            {
                _898 = 1.0;
            }
            _888 = _898;
        }
        _877 = _888;
    }
    smoothness_offset *= _877;
    float param_18 = LIBRA_UNIFORM_MASK_COLOR_BLEED;
    float param_19 = 0.0;
    float param_20 = 0.0;
    float param_21 = 1.0;
    float bleed_offset = mix_master(param_18, param_19, param_20, param_21) * mix(-0.5, -0.25, mask_blend);
    brightness_compensation += ((1.5 * (1.0 - mask_blend)) * mask_intensity);
    brightness_compensation += (subpixel_offset * mask_intensity);
    brightness_compensation += (type_offset * mask_intensity);
    brightness_compensation += (size_offset * mask_intensity);
    brightness_compensation += (smoothness_offset * mask_intensity);
    brightness_compensation += (bleed_offset * mask_intensity);
    return brightness_compensation;
}

float get_anti_ringing_amount()
{
    float param = LIBRA_UNIFORM_BEAM_FILTER;
    float param_1 = 0.0;
    float param_2 = -1.0;
    float param_3 = 1.0;
    float anti_ringing_auto = 1.0 - smoothstep(1.5, 2.0, mix_master(param, param_1, param_2, param_3) + 1.0);
    float anti_ringing_manual = LIBRA_UNIFORM_ANTI_RINGING;
    return anti_ringing_auto * anti_ringing_manual;
}

vec2 get_floor_profile()
{
    float param = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    float param_4 = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 1.0;
    float color_floor = 0.00390625 * max(mix_master(param, param_1, param_2, param_3), mix_master(param_4, param_5, param_6, param_7));
    float noise_floor = 0.015625 - color_floor;
    return vec2(color_floor * LIBRA_UNIFORM_COLOR_BLACK_LIGHT, noise_floor * LIBRA_UNIFORM_COLOR_BLACK_LIGHT);
}

float GetUniformFrameFactor()
{
    float factor = 1.0;
    float _460;
    if (factor > 1.0)
    {
        _460 = round(factor);
    }
    else
    {
        _460 = round(factor * 4.0) / 4.0;
    }
    return _460;
}

uint GetUniformFrameCount(float frame_rate)
{
    float frame_rate_factor = frame_rate / 60.0;
    return uint(round((GetUniformFrameFactor() * float(LIBRA_UNIFORM_FrameCount)) * frame_rate_factor));
}

uvec2 get_frame_counts()
{
    int _1268;
    if (int(LIBRA_UNIFORM_SCREEN_FREQUENCY) == 50)
    {
        _1268 = 48;
    }
    else
    {
        _1268 = int(LIBRA_UNIFORM_SCREEN_FREQUENCY);
    }
    float param = float(_1268);
    float interlace_frame = mod(float(GetUniformFrameCount(param)), 2.0);
    float param_1 = 12.0;
    float noise_frame = mod(float(GetUniformFrameCount(param_1)), 20.0);
    return uvec2(uint(interlace_frame), uint(noise_frame));
}

vec2 vec2o(float x, float y)
{
    vec2 _493;
    if (LIBRA_VARYING_3 == 0)
    {
        _493 = vec2(x, y);
    }
    else
    {
        _493 = vec2(y, x);
    }
    return _493;
}

vec2 get_tex_size()
{
    vec2 tex_size = LIBRA_UNIFORM_OriginalSize.xy;
    float param = 1.0;
    float param_1 = LIBRA_VARYING_4.x;
    vec2 multiple = vec2o(param, param_1);
    tex_size /= multiple;
    return tex_size;
}

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = Coord;
    LIBRA_VARYING_1 = Coord;
    vec2 param = LIBRA_UNIFORM_OutputSize.xy;
    int param_1 = int(LIBRA_UNIFORM_SCREEN_ORIENTATION);
    LIBRA_VARYING_3 = get_orientation(param, param_1);
    vec2 param_2 = LIBRA_UNIFORM_OriginalSize.xy;
    int param_3 = LIBRA_VARYING_3;
    float param_4 = -LIBRA_UNIFORM_SCREEN_SCALE;
    LIBRA_VARYING_4.x = get_screen_multiple(param_2, param_3, param_4);
    vec2 param_5 = LIBRA_UNIFORM_OriginalSize.xy;
    int param_6 = LIBRA_VARYING_3;
    float param_7 = 0.0;
    LIBRA_VARYING_4.y = get_screen_multiple(param_5, param_6, param_7);
    vec2 param_8 = LIBRA_UNIFORM_OriginalSize.xy;
    int param_9 = LIBRA_VARYING_3;
    float param_10 = 0.0;
    LIBRA_VARYING_4.z = get_auto_multiple(param_8, param_9, param_10);
    LIBRA_VARYING_6 = get_mask_profile();
    LIBRA_VARYING_7 = get_beam_profile();
    LIBRA_VARYING_11 = get_beam_filter();
    LIBRA_VARYING_5 = get_brightness_compensation();
    LIBRA_VARYING_8 = get_anti_ringing_amount();
    LIBRA_VARYING_9 = get_floor_profile();
    LIBRA_VARYING_10 = get_frame_counts();
    LIBRA_VARYING_2 = get_tex_size();
    bool _1390 = LIBRA_VARYING_4.y > 1.0;
    bool _1397;
    if (!_1390)
    {
        _1397 = LIBRA_VARYING_4.z > 1.0;
    }
    else
    {
        _1397 = _1390;
    }
    if (_1397)
    {
        float param_11 = 0.5;
        float param_12 = 0.0;
        LIBRA_VARYING_1 += (vec2o(param_11, param_12) / LIBRA_UNIFORM_OriginalSize.xy);
    }
}
