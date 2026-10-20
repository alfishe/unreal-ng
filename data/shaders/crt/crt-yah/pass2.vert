#version 330

const float _169[17] = float[](0.111111111938953399658203125, 0.125, 0.14285714924335479736328125, 0.16666667163372039794921875, 0.20000000298023223876953125, 0.25, 0.3333333432674407958984375, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_ORIENTATION;
uniform float LIBRA_UNIFORM_SCREEN_SCALE;

uniform float LIBRA_UNIFORM_NTSC_PROFILE;
uniform float LIBRA_UNIFORM_NTSC_SAMPLES;
uniform float LIBRA_UNIFORM_NTSC_PHASE;
uniform float LIBRA_UNIFORM_NTSC_SCALE;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 Coord;
out vec2 LIBRA_VARYING_1;
flat out int LIBRA_VARYING_2;
flat out int LIBRA_VARYING_3;

float get_ratio(vec2 size, int orientation)
{
    float _69;
    if (orientation > 0)
    {
        _69 = size.x / size.y;
    }
    else
    {
        _69 = size.y / size.x;
    }
    return _69;
}

bool is_portrait(vec2 size)
{
    return size.y > size.x;
}

float get_multiple(vec2 source_size, int orientation)
{
    vec2 param = source_size;
    int param_1 = orientation;
    float ratio = get_ratio(param, param_1);
    vec2 param_2 = source_size;
    float portrait = float(is_portrait(param_2));
    float base_size = (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 3) ? 480.0 : 240.0;
    float _117;
    if (orientation > 0)
    {
        _117 = mix(base_size * ratio, base_size, portrait);
    }
    else
    {
        _117 = mix(base_size, base_size * ratio, portrait);
    }
    base_size = _117;
    float _136;
    if (orientation > 0)
    {
        _136 = source_size.x / base_size;
    }
    else
    {
        _136 = source_size.y / base_size;
    }
    return _136;
}

float get_multiple_factor(float index)
{
    return mix(_169[int(clamp(floor(index), 0.0, 16.0))], _169[int(clamp(ceil(index), 0.0, 16.0))], fract(index));
}

float offset_multiple(inout float multiple, float pixel_size, float multiple_offset)
{
    bool _194 = multiple < 1.0;
    bool _210;
    if (_194)
    {
        bool _200 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 3;
        bool _209;
        if (!_200)
        {
            _209 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 5;
        }
        else
        {
            _209 = _200;
        }
        _210 = _209;
    }
    else
    {
        _210 = _194;
    }
    bool upscale = _210;
    float _sign = upscale ? (-1.0) : 1.0;
    float _217;
    if (upscale)
    {
        _217 = round((1.0 / multiple) * 2.0) * 0.5;
    }
    else
    {
        _217 = round(multiple);
    }
    float index = _217;
    float _239 = ((_sign * (max(1.0, index) - 1.0)) + 8.0) + multiple_offset;
    for (float multiple_index = _239; multiple_index < 17.0; multiple_index += 0.0500000007450580596923828125)
    {
        float param = multiple_index;
        multiple = get_multiple_factor(param);
        if ((pixel_size * multiple) >= 0.0)
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
    float _273;
    if (source_orientation > 0)
    {
        _273 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _273 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _273;
    float param_2 = multiple;
    float param_3 = pixel_size;
    float param_4 = multiple_offset;
    float _295 = offset_multiple(param_2, param_3, param_4);
    return _295;
}

float get_fixed_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float multiple = 1.0;
    float _302;
    if (source_orientation > 0)
    {
        _302 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _302 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _302;
    float param = multiple;
    float param_1 = pixel_size;
    float param_2 = multiple_offset;
    float _323 = offset_multiple(param, param_1, param_2);
    return _323;
}

float get_screen_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float _331;
    if (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 1)
    {
        vec2 param = source_size;
        int param_1 = source_orientation;
        float param_2 = multiple_offset;
        _331 = get_auto_multiple(param, param_1, param_2);
    }
    else
    {
        vec2 param_3 = source_size;
        int param_4 = source_orientation;
        float param_5 = multiple_offset;
        _331 = get_fixed_multiple(param_3, param_4, param_5);
    }
    return _331;
}

vec2 vec2o(vec2 v)
{
    return mix(v, v.yx, vec2(0.0));
}

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = Coord;
    vec2 param = LIBRA_UNIFORM_OriginalSize.xy;
    int param_1 = 0;
    float param_2 = -(LIBRA_UNIFORM_SCREEN_SCALE + LIBRA_UNIFORM_NTSC_SCALE);
    float multiple = get_screen_multiple(param, param_1, param_2);
    float screen_scale = 1.0 / multiple;
    vec2 _402;
    if (true)
    {
        _402 = vec2(LIBRA_UNIFORM_SourceSize.z * multiple, 0.0);
    }
    else
    {
        _402 = vec2(0.0, LIBRA_UNIFORM_SourceSize.w * multiple);
    }
    LIBRA_VARYING_1 = _402;
    int _426;
    if (int(LIBRA_UNIFORM_NTSC_PHASE) == 0)
    {
        vec2 param_3 = LIBRA_UNIFORM_OriginalSize.xy;
        _426 = ((vec2o(param_3).x * screen_scale) > 300.0) ? 2 : 3;
    }
    else
    {
        _426 = int(LIBRA_UNIFORM_NTSC_PHASE) + 1;
    }
    LIBRA_VARYING_2 = _426;
    int samples = int((LIBRA_UNIFORM_NTSC_SAMPLES * 4.0) - 1.0);
    if (LIBRA_VARYING_2 == 2)
    {
        int _459;
        if (samples == 0)
        {
            _459 = 8;
        }
        else
        {
            int _465;
            if (samples == 1)
            {
                _465 = 16;
            }
            else
            {
                _465 = (samples == 2) ? 24 : 32;
            }
            _459 = _465;
        }
        LIBRA_VARYING_3 = _459;
    }
    else
    {
        int _480;
        if (samples == 0)
        {
            _480 = 6;
        }
        else
        {
            int _486;
            if (samples == 1)
            {
                _486 = 12;
            }
            else
            {
                _486 = (samples == 2) ? 18 : 24;
            }
            _480 = _486;
        }
        LIBRA_VARYING_3 = _480;
    }
}
