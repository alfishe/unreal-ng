#version 330

const float _202[17] = float[](0.111111111938953399658203125, 0.125, 0.14285714924335479736328125, 0.16666667163372039794921875, 0.20000000298023223876953125, 0.25, 0.3333333432674407958984375, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);

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

uniform float LIBRA_UNIFORM_COLOR_TEMPERATUE;
uniform float LIBRA_UNIFORM_COLOR_CONTRAST;
uniform float LIBRA_UNIFORM_COLOR_SATURATION;
uniform float LIBRA_UNIFORM_COLOR_OVERFLOW;
uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS;
uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS_FLICKER;
uniform float LIBRA_UNIFORM_SCANLINES_STRENGTH;
uniform float LIBRA_UNIFORM_SCANLINES_OFFSET;

layout(location = 0) in vec4 Position;
flat out int LIBRA_VARYING_1;
out float LIBRA_VARYING_2;
flat out uint LIBRA_VARYING_3;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 Coord;

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
    int _88;
    if (orientation > 0)
    {
        _88 = orientation - 1;
    }
    else
    {
        vec2 param = size;
        _88 = get_orientation(param);
    }
    return _88;
}

float get_ratio(vec2 size, int orientation)
{
    float _102;
    if (orientation > 0)
    {
        _102 = size.x / size.y;
    }
    else
    {
        _102 = size.y / size.x;
    }
    return _102;
}

float get_multiple(vec2 source_size, int orientation)
{
    vec2 param = source_size;
    int param_1 = orientation;
    float ratio = get_ratio(param, param_1);
    vec2 param_2 = source_size;
    float portrait = float(is_portrait(param_2));
    float base_size = (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 3) ? 480.0 : 240.0;
    float _150;
    if (orientation > 0)
    {
        _150 = mix(base_size * ratio, base_size, portrait);
    }
    else
    {
        _150 = mix(base_size, base_size * ratio, portrait);
    }
    base_size = _150;
    float _169;
    if (orientation > 0)
    {
        _169 = source_size.x / base_size;
    }
    else
    {
        _169 = source_size.y / base_size;
    }
    return _169;
}

float get_multiple_factor(float index)
{
    return mix(_202[int(clamp(floor(index), 0.0, 16.0))], _202[int(clamp(ceil(index), 0.0, 16.0))], fract(index));
}

float offset_multiple(inout float multiple, float pixel_size, float multiple_offset)
{
    bool _227 = multiple < 1.0;
    bool _243;
    if (_227)
    {
        bool _233 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 3;
        bool _242;
        if (!_233)
        {
            _242 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 5;
        }
        else
        {
            _242 = _233;
        }
        _243 = _242;
    }
    else
    {
        _243 = _227;
    }
    bool upscale = _243;
    float _sign = upscale ? (-1.0) : 1.0;
    float _250;
    if (upscale)
    {
        _250 = round((1.0 / multiple) * 2.0) * 0.5;
    }
    else
    {
        _250 = round(multiple);
    }
    float index = _250;
    float _272 = ((_sign * (max(1.0, index) - 1.0)) + 8.0) + multiple_offset;
    for (float multiple_index = _272; multiple_index < 17.0; multiple_index += 0.0500000007450580596923828125)
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
    float _306;
    if (source_orientation > 0)
    {
        _306 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _306 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _306;
    float param_2 = multiple;
    float param_3 = pixel_size;
    float param_4 = multiple_offset;
    float _328 = offset_multiple(param_2, param_3, param_4);
    return _328;
}

float get_fixed_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float multiple = 1.0;
    float _335;
    if (source_orientation > 0)
    {
        _335 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _335 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _335;
    float param = multiple;
    float param_1 = pixel_size;
    float param_2 = multiple_offset;
    float _356 = offset_multiple(param, param_1, param_2);
    return _356;
}

float get_screen_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float _363;
    if (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 1)
    {
        vec2 param = source_size;
        int param_1 = source_orientation;
        float param_2 = multiple_offset;
        _363 = get_auto_multiple(param, param_1, param_2);
    }
    else
    {
        vec2 param_3 = source_size;
        int param_4 = source_orientation;
        float param_5 = multiple_offset;
        _363 = get_fixed_multiple(param_3, param_4, param_5);
    }
    return _363;
}

float GetUniformFrameFactor()
{
    float factor = 1.0;
    float _387;
    if (factor > 1.0)
    {
        _387 = round(factor);
    }
    else
    {
        _387 = round(factor * 4.0) / 4.0;
    }
    return _387;
}

uint GetUniformFrameCount(float frame_rate)
{
    float frame_rate_factor = frame_rate / 60.0;
    return uint(round((GetUniformFrameFactor() * float(LIBRA_UNIFORM_FrameCount)) * frame_rate_factor));
}

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    vec2 param = LIBRA_UNIFORM_OutputSize.xy;
    int param_1 = int(LIBRA_UNIFORM_SCREEN_ORIENTATION);
    LIBRA_VARYING_1 = get_orientation(param, param_1);
    vec2 param_2 = LIBRA_UNIFORM_OriginalSize.xy;
    int param_3 = LIBRA_VARYING_1;
    float param_4 = -LIBRA_UNIFORM_SCREEN_SCALE;
    LIBRA_VARYING_2 = get_screen_multiple(param_2, param_3, param_4);
    int _464;
    if (int(LIBRA_UNIFORM_SCREEN_FREQUENCY) == 50)
    {
        _464 = 48;
    }
    else
    {
        _464 = int(LIBRA_UNIFORM_SCREEN_FREQUENCY);
    }
    float param_5 = float(_464);
    LIBRA_VARYING_3 = uint(mod(float(GetUniformFrameCount(param_5)), 3.0));
    LIBRA_VARYING_0 = Coord;
}
