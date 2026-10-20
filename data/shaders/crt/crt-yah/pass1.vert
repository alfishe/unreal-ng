#version 330

const float _176[17] = float[](0.111111111938953399658203125, 0.125, 0.14285714924335479736328125, 0.16666667163372039794921875, 0.20000000298023223876953125, 0.25, 0.3333333432674407958984375, 0.5, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_InputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_FrameTimeDelta;
uniform float LIBRA_UNIFORM_OriginalFPS;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_ORIENTATION;
uniform float LIBRA_UNIFORM_SCREEN_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_FREQUENCY;

uniform float LIBRA_UNIFORM_NTSC_PROFILE;
uniform float LIBRA_UNIFORM_NTSC_SAMPLES;
uniform float LIBRA_UNIFORM_NTSC_PHASE;
uniform float LIBRA_UNIFORM_NTSC_SCALE;
uniform float LIBRA_UNIFORM_NTSC_SHIFT;
uniform float LIBRA_UNIFORM_NTSC_JITTER;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 Coord;
out vec2 LIBRA_VARYING_1;
out float LIBRA_VARYING_2;
out float LIBRA_VARYING_3;
flat out int LIBRA_VARYING_6;
out float LIBRA_VARYING_5;
out float LIBRA_VARYING_4;
out vec3 LIBRA_VARYING_7;
flat out uint LIBRA_VARYING_8;

float get_ratio(vec2 size, int orientation)
{
    float _76;
    if (orientation > 0)
    {
        _76 = size.x / size.y;
    }
    else
    {
        _76 = size.y / size.x;
    }
    return _76;
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
    float _124;
    if (orientation > 0)
    {
        _124 = mix(base_size * ratio, base_size, portrait);
    }
    else
    {
        _124 = mix(base_size, base_size * ratio, portrait);
    }
    base_size = _124;
    float _143;
    if (orientation > 0)
    {
        _143 = source_size.x / base_size;
    }
    else
    {
        _143 = source_size.y / base_size;
    }
    return _143;
}

float get_multiple_factor(float index)
{
    return mix(_176[int(clamp(floor(index), 0.0, 16.0))], _176[int(clamp(ceil(index), 0.0, 16.0))], fract(index));
}

float offset_multiple(inout float multiple, float pixel_size, float multiple_offset)
{
    bool _201 = multiple < 1.0;
    bool _217;
    if (_201)
    {
        bool _207 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 3;
        bool _216;
        if (!_207)
        {
            _216 = int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) == 5;
        }
        else
        {
            _216 = _207;
        }
        _217 = _216;
    }
    else
    {
        _217 = _201;
    }
    bool upscale = _217;
    float _sign = upscale ? (-1.0) : 1.0;
    float _224;
    if (upscale)
    {
        _224 = round((1.0 / multiple) * 2.0) * 0.5;
    }
    else
    {
        _224 = round(multiple);
    }
    float index = _224;
    float _246 = ((_sign * (max(1.0, index) - 1.0)) + 8.0) + multiple_offset;
    for (float multiple_index = _246; multiple_index < 17.0; multiple_index += 0.0500000007450580596923828125)
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
    float _280;
    if (source_orientation > 0)
    {
        _280 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _280 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _280;
    float param_2 = multiple;
    float param_3 = pixel_size;
    float param_4 = multiple_offset;
    float _301 = offset_multiple(param_2, param_3, param_4);
    return _301;
}

float get_fixed_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float multiple = 1.0;
    float _308;
    if (source_orientation > 0)
    {
        _308 = LIBRA_UNIFORM_FinalViewportSize.x / source_size.x;
    }
    else
    {
        _308 = LIBRA_UNIFORM_FinalViewportSize.y / source_size.y;
    }
    float pixel_size = _308;
    float param = multiple;
    float param_1 = pixel_size;
    float param_2 = multiple_offset;
    float _329 = offset_multiple(param, param_1, param_2);
    return _329;
}

float get_screen_multiple(vec2 source_size, int source_orientation, float multiple_offset)
{
    float _337;
    if (int(LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE) > 1)
    {
        vec2 param = source_size;
        int param_1 = source_orientation;
        float param_2 = multiple_offset;
        _337 = get_auto_multiple(param, param_1, param_2);
    }
    else
    {
        vec2 param_3 = source_size;
        int param_4 = source_orientation;
        float param_5 = multiple_offset;
        _337 = get_fixed_multiple(param_3, param_4, param_5);
    }
    return _337;
}

vec2 vec2o(vec2 v)
{
    return mix(v, v.yx, vec2(0.0));
}

float GetUniformFrameFactor()
{
    float factor = 1.0;
    float _361;
    if (factor > 1.0)
    {
        _361 = round(factor);
    }
    else
    {
        _361 = round(factor * 4.0) / 4.0;
    }
    return _361;
}

uint GetUniformFrameCount(float frame_rate)
{
    float frame_rate_factor = frame_rate / 60.0;
    return uint(round((GetUniformFrameFactor() * float(LIBRA_UNIFORM_FrameCount)) * frame_rate_factor));
}

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    vec2 param = LIBRA_UNIFORM_OriginalSize.xy;
    int param_1 = 0;
    float param_2 = -(LIBRA_UNIFORM_SCREEN_SCALE + LIBRA_UNIFORM_NTSC_SCALE);
    float multiple = get_screen_multiple(param, param_1, param_2);
    float screen_scale = 1.0 / multiple;
    LIBRA_VARYING_0 = Coord;
    LIBRA_VARYING_1 = LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy;
    vec2 _447;
    if (screen_scale < 1.0)
    {
        _447 = LIBRA_VARYING_1 * screen_scale;
    }
    else
    {
        _447 = LIBRA_VARYING_1;
    }
    LIBRA_VARYING_1 = _447;
    vec2 param_3 = LIBRA_VARYING_1;
    LIBRA_VARYING_1 = vec2o(param_3);
    LIBRA_VARYING_1.x *= max(screen_scale, 1.0);
    LIBRA_VARYING_2 = LIBRA_UNIFORM_NTSC_PROFILE - 1.0;
    LIBRA_VARYING_3 = ((LIBRA_UNIFORM_NTSC_PROFILE - 1.0) * 0.5) * (max(screen_scale, 1.0) + 1.0);
    int _486;
    if (int(LIBRA_UNIFORM_NTSC_PHASE) == 0)
    {
        vec2 param_4 = LIBRA_UNIFORM_OriginalSize.xy;
        _486 = ((vec2o(param_4).x * screen_scale) > 300.0) ? 2 : 3;
    }
    else
    {
        _486 = int(LIBRA_UNIFORM_NTSC_PHASE) + 1;
    }
    LIBRA_VARYING_6 = _486;
    LIBRA_VARYING_5 = 1.0;
    int samples = int((LIBRA_UNIFORM_NTSC_SAMPLES * 4.0) - 1.0);
    if (LIBRA_VARYING_6 == 2)
    {
        float _520;
        if (samples == 0)
        {
            _520 = 1.9249999523162841796875;
        }
        else
        {
            float _527;
            if (samples == 1)
            {
                _527 = 1.22500002384185791015625;
            }
            else
            {
                _527 = (samples == 2) ? 1.0499999523162841796875 : 1.0;
            }
            _520 = _527;
        }
        LIBRA_VARYING_4 = _520;
        float _540;
        if (samples == 0)
        {
            _540 = 0.97500002384185791015625;
        }
        else
        {
            float _547;
            if (samples == 1)
            {
                _547 = 1.0;
            }
            else
            {
                _547 = (samples == 2) ? 1.0 : 1.0;
            }
            _540 = _547;
        }
        LIBRA_VARYING_5 = _540;
    }
    else
    {
        float _559;
        if (samples == 0)
        {
            _559 = 1.1749999523162841796875;
        }
        else
        {
            float _566;
            if (samples == 1)
            {
                _566 = 0.949999988079071044921875;
            }
            else
            {
                _566 = (samples == 2) ? 0.98500001430511474609375 : 1.0;
            }
            _559 = _566;
        }
        LIBRA_VARYING_4 = _559;
        float _579;
        if (samples == 0)
        {
            _579 = 0.97500002384185791015625;
        }
        else
        {
            float _585;
            if (samples == 1)
            {
                _585 = 1.0;
            }
            else
            {
                _585 = (samples == 2) ? 1.0 : 1.0;
            }
            _579 = _585;
        }
        LIBRA_VARYING_5 = _579;
    }
    float chromaFrequency = (LIBRA_VARYING_6 == 2) ? 0.837758004665374755859375 : 1.0471975803375244140625;
    float chromaAmplitude = (LIBRA_VARYING_6 == 2) ? 3.141592502593994140625 : 2.094395160675048828125;
    float _609;
    if (LIBRA_VARYING_6 == 2)
    {
        _609 = LIBRA_UNIFORM_NTSC_SHIFT + 1.0;
    }
    else
    {
        _609 = (LIBRA_UNIFORM_NTSC_SHIFT * 1.5) + 1.5;
    }
    float chromaShift = _609;
    LIBRA_VARYING_7 = vec3(chromaFrequency, chromaAmplitude, chromaShift);
    int _638;
    if (int(LIBRA_UNIFORM_SCREEN_FREQUENCY) == 50)
    {
        _638 = 48;
    }
    else
    {
        _638 = int(LIBRA_UNIFORM_SCREEN_FREQUENCY);
    }
    float param_5 = float(_638);
    LIBRA_VARYING_8 = GetUniformFrameCount(param_5);
}
