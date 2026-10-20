#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SHARP_AMOUNT;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 Coord;
flat out int LIBRA_VARYING_3;
flat out float LIBRA_VARYING_1;
flat out float LIBRA_VARYING_2;

bool is_portrait(vec2 size)
{
    return size.y > size.x;
}

int get_orientation(vec2 size)
{
    vec2 param = size;
    return int(is_portrait(param));
}

float get_ratio(vec2 size, int orientation)
{
    float _59;
    if (orientation > 0)
    {
        _59 = size.x / size.y;
    }
    else
    {
        _59 = size.y / size.x;
    }
    return _59;
}

float get_multiple(vec2 source_size, int orientation)
{
    vec2 param = source_size;
    int param_1 = orientation;
    float ratio = get_ratio(param, param_1);
    vec2 param_2 = source_size;
    float portrait = float(is_portrait(param_2));
    float base_size = 240.0;
    float _93;
    if (orientation > 0)
    {
        _93 = mix(base_size * ratio, base_size, portrait);
    }
    else
    {
        _93 = mix(base_size, base_size * ratio, portrait);
    }
    base_size = _93;
    float _112;
    if (orientation > 0)
    {
        _112 = source_size.x / base_size;
    }
    else
    {
        _112 = source_size.y / base_size;
    }
    return _112;
}

float get_multiple(vec2 size)
{
    vec2 param = size;
    vec2 param_1 = size;
    int param_2 = get_orientation(param);
    return get_multiple(param_1, param_2);
}

float normalized_sigmoid(float value, float slope)
{
    float numerator = (1.0 - slope) * value;
    float denominator = (1.0 + slope) - ((abs(value) * slope) * 2.0);
    return numerator / denominator;
}

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = Coord;
    vec2 param = LIBRA_UNIFORM_OriginalSize.xy;
    float multiple = get_multiple(param);
    multiple = max(1.0, round(multiple));
    float blur_radius = 0.0;
    float param_1 = blur_radius;
    float param_2 = -0.75;
    float diffusion = normalized_sigmoid(param_1, param_2);
    diffusion *= 0.75;
    diffusion = max(0.25, 1.0 - diffusion);
    diffusion /= (multiple * multiple);
    float _209;
    if (false)
    {
        float param_3 = diffusion;
        float param_4 = 0.0;
        _209 = normalized_sigmoid(param_3, param_4);
    }
    else
    {
        _209 = diffusion;
    }
    diffusion = _209;
    float param_5 = blur_radius;
    float param_6 = 0.75;
    float tabs = normalized_sigmoid(param_5, param_6);
    tabs = max(1.0, round(tabs * 4.0));
    tabs = (tabs * multiple) * 2.0;
    LIBRA_VARYING_3 = int(ceil(min(tabs, 12.0) / 2.0));
    float _245;
    if (tabs > 12.0)
    {
        _245 = tabs / 12.0;
    }
    else
    {
        _245 = 1.0;
    }
    LIBRA_VARYING_1 = _245;
    LIBRA_VARYING_2 = exp(((-diffusion) * LIBRA_VARYING_1) * LIBRA_VARYING_1);
}
