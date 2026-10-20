#version 330

const float _218[17] = float[](0.111111111938953399658203125, 0.125, 0.14285714924335479736328125, 0.16666667163372039794921875, 0.20000000298023223876953125, 0.25, 0.3333333432674407958984375, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_ORIENTATION;
uniform float LIBRA_UNIFORM_SCREEN_SCALE;

uniform float LIBRA_UNIFORM_DECONVERGE_LINEAR;
uniform float LIBRA_UNIFORM_DECONVERGE_RADIAL;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 Coord;
out vec2 LIBRA_VARYING_1;
out vec2 LIBRA_VARYING_2;
out vec2 LIBRA_VARYING_3;

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
    int _110;
    if (orientation > 0)
    {
        _110 = orientation - 1;
    }
    else
    {
        vec2 param = size;
        _110 = get_orientation(param);
    }
    return _110;
}

float get_ratio(vec2 size, int orientation)
{
    float _124;
    if (orientation > 0)
    {
        _124 = size.x / size.y;
    }
    else
    {
        _124 = size.y / size.x;
    }
    return _124;
}

float get_multiple(vec2 source_size, int orientation)
{
    vec2 param = source_size;
    int param_1 = orientation;
    float ratio = get_ratio(param, param_1);
    vec2 param_2 = source_size;
    float portrait = float(is_portrait(param_2));
    float base_size = (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 3) ? 480.0 : 240.0;
    float _166;
    if (orientation > 0)
    {
        _166 = mix(base_size * ratio, base_size, portrait);
    }
    else
    {
        _166 = mix(base_size, base_size * ratio, portrait);
    }
    base_size = _166;
    float _185;
    if (orientation > 0)
    {
        _185 = source_size.x / base_size;
    }
    else
    {
        _185 = source_size.y / base_size;
    }
    return _185;
}

float get_multiple_factor(float index)
{
    return mix(_218[int(clamp(floor(index), 0.0, 16.0))], _218[int(clamp(ceil(index), 0.0, 16.0))], fract(index));
}

float offset_multiple(inout float multiple, float pixel_size, float multiple_offset)
{
    bool _243 = multiple < 1.0;
    bool _258;
    if (_243)
    {
        bool _249 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 3;
        bool _257;
        if (!_249)
        {
            _257 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 5;
        }
        else
        {
            _257 = _249;
        }
        _258 = _257;
    }
    else
    {
        _258 = _243;
    }
    bool upscale = _258;
    float _sign = upscale ? (-1.0) : 1.0;
    float _265;
    if (upscale)
    {
        _265 = round((1.0 / multiple) * 2.0) * 0.5;
    }
    else
    {
        _265 = round(multiple);
    }
    float index = _265;
    float _287 = ((_sign * (max(1.0, index) - 1.0)) + 8.0) + multiple_offset;
    for (float multiple_index = _287; multiple_index < 17.0; multiple_index += 0.0500000007450580596923828125)
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
    float _321;
    if (source_orientation > 0)
    {
        _321 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _321 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _321;
    float param_2 = multiple;
    float param_3 = pixel_size;
    float param_4 = multiple_offset;
    float _343 = offset_multiple(param_2, param_3, param_4);
    return _343;
}

float get_fixed_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float multiple = 1.0;
    float _350;
    if (source_orientation > 0)
    {
        _350 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _350 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _350;
    float param = multiple;
    float param_1 = pixel_size;
    float param_2 = multiple_offset;
    float _371 = offset_multiple(param, param_1, param_2);
    return _371;
}

float get_screen_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float _378;
    if (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 1)
    {
        vec2 param = source_size;
        int param_1 = source_orientation;
        float param_2 = multiple_offset;
        _378 = get_auto_multiple(param, param_1, param_2);
    }
    else
    {
        vec2 param_3 = source_size;
        int param_4 = source_orientation;
        float param_5 = multiple_offset;
        _378 = get_fixed_multiple(param_3, param_4, param_5);
    }
    return _378;
}

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
}

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    vec2 param = LIBRA_UNIFORM_OutputSize.xy;
    int param_1 = int(LIBRA_UNIFORM_SCREEN_ORIENTATION);
    int screen_orientation = get_orientation(param, param_1);
    vec2 param_2 = LIBRA_UNIFORM_OriginalSize.xy;
    int param_3 = screen_orientation;
    float param_4 = -LIBRA_UNIFORM_SCREEN_SCALE;
    float screen_scale = get_screen_multiple(param_2, param_3, param_4);
    float param_5 = LIBRA_UNIFORM_DECONVERGE_LINEAR;
    float param_6 = 0.0;
    float param_7 = -2.0;
    float param_8 = 2.0;
    float _451;
    if (mix_master(param_5, param_6, param_7, param_8) > 0.0)
    {
        float param_9 = LIBRA_UNIFORM_DECONVERGE_LINEAR;
        float param_10 = 0.0;
        float param_11 = -2.0;
        float param_12 = 2.0;
        _451 = min(1.0, mix_master(param_9, param_10, param_11, param_12));
    }
    else
    {
        float param_13 = LIBRA_UNIFORM_DECONVERGE_LINEAR;
        float param_14 = 0.0;
        float param_15 = -2.0;
        float param_16 = 2.0;
        _451 = max(-1.0, mix_master(param_13, param_14, param_15, param_16));
    }
    float linear_limit = _451;
    float param_17 = LIBRA_UNIFORM_DECONVERGE_RADIAL;
    float param_18 = 0.0;
    float param_19 = -2.0;
    float param_20 = 2.0;
    float _481;
    if (mix_master(param_17, param_18, param_19, param_20) > 0.0)
    {
        float param_21 = LIBRA_UNIFORM_DECONVERGE_RADIAL;
        float param_22 = 0.0;
        float param_23 = -2.0;
        float param_24 = 2.0;
        _481 = min(1.0, mix_master(param_21, param_22, param_23, param_24));
    }
    else
    {
        float param_25 = LIBRA_UNIFORM_DECONVERGE_RADIAL;
        float param_26 = 0.0;
        float param_27 = -2.0;
        float param_28 = 2.0;
        _481 = max(-1.0, mix_master(param_25, param_26, param_27, param_28));
    }
    float radial_limit = _481;
    vec2 linear_offset = mix(vec2(linear_limit, 0.0), vec2(0.0, linear_limit), vec2(float(screen_orientation)));
    vec2 radial_offset = vec2(radial_limit);
    float r = -1.0;
    float g = 0.0;
    float b = 1.0;
    vec2 TexelSize = LIBRA_UNIFORM_SourceSize.zw * screen_scale;
    LIBRA_VARYING_0 = Coord;
    LIBRA_VARYING_1 = Coord;
    LIBRA_VARYING_2 = Coord;
    LIBRA_VARYING_3 = Coord;
    LIBRA_VARYING_1 -= vec2(0.5);
    LIBRA_VARYING_2 -= vec2(0.5);
    LIBRA_VARYING_3 -= vec2(0.5);
    LIBRA_VARYING_1 *= (vec2(1.0) + (((radial_offset * r) * TexelSize) * 2.0));
    LIBRA_VARYING_2 *= (vec2(1.0) + (((radial_offset * g) * TexelSize) * 2.0));
    LIBRA_VARYING_3 *= (vec2(1.0) + (((radial_offset * b) * TexelSize) * 2.0));
    LIBRA_VARYING_1 += vec2(0.5);
    LIBRA_VARYING_2 += vec2(0.5);
    LIBRA_VARYING_3 += vec2(0.5);
    LIBRA_VARYING_1 += ((linear_offset * r) * TexelSize);
    LIBRA_VARYING_2 += ((linear_offset * g) * TexelSize);
    LIBRA_VARYING_3 += ((linear_offset * b) * TexelSize);
}
