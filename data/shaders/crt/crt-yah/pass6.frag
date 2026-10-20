#version 330

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

uniform sampler2D LIBRA_TEXTURE_Source;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_2;
in vec2 LIBRA_VARYING_3;

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
}

float srgb_to_linear(float value, float gamma)
{
    float _69;
    if (value <= 0.0404481999576091766357421875)
    {
        _69 = value / 12.9200000762939453125;
    }
    else
    {
        _69 = pow((value + 0.054999999701976776123046875) / 1.05499994754791259765625, gamma);
    }
    return _69;
}

vec3 srgb_to_linear(vec3 color, float gamma)
{
    float param = color.x;
    float param_1 = gamma;
    float param_2 = color.y;
    float param_3 = gamma;
    float param_4 = color.z;
    float param_5 = gamma;
    return vec3(srgb_to_linear(param, param_1), srgb_to_linear(param_2, param_3), srgb_to_linear(param_4, param_5));
}

vec3 decode_gamma(vec3 color)
{
    vec3 param = color;
    float param_1 = 2.400000095367431640625;
    return srgb_to_linear(param, param_1);
}

float linear_to_srgb(float value, float gamma)
{
    float _114;
    if (value <= 0.0031306999735534191131591796875)
    {
        _114 = value * 12.9200000762939453125;
    }
    else
    {
        _114 = (1.05499994754791259765625 * pow(value, 1.0 / gamma)) - 0.054999999701976776123046875;
    }
    return _114;
}

vec3 linear_to_srgb(vec3 color, float gamma)
{
    float param = color.x;
    float param_1 = gamma;
    float param_2 = color.y;
    float param_3 = gamma;
    float param_4 = color.z;
    float param_5 = gamma;
    return vec3(linear_to_srgb(param, param_1), linear_to_srgb(param_2, param_3), linear_to_srgb(param_4, param_5));
}

vec3 encode_gamma(vec3 color)
{
    vec3 param = color;
    float param_1 = 2.400000095367431640625;
    return linear_to_srgb(param, param_1);
}

void main()
{
    float param = LIBRA_UNIFORM_DECONVERGE_LINEAR;
    float param_1 = 0.0;
    float param_2 = -2.0;
    float param_3 = 2.0;
    bool _179 = mix_master(param, param_1, param_2, param_3) == 0.0;
    bool _191;
    if (_179)
    {
        float param_4 = LIBRA_UNIFORM_DECONVERGE_RADIAL;
        float param_5 = 0.0;
        float param_6 = -2.0;
        float param_7 = 2.0;
        _191 = mix_master(param_4, param_5, param_6, param_7) == 0.0;
    }
    else
    {
        _191 = _179;
    }
    if (_191)
    {
        FragColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
        return;
    }
    vec3 param_8 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 color = decode_gamma(param_8);
    vec3 param_9 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_1).xyz;
    vec3 param_10 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_2).xyz;
    vec3 param_11 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_3).xyz;
    vec3 deconverge_color = vec3(decode_gamma(param_9).x, decode_gamma(param_10).y, decode_gamma(param_11).z);
    float param_12 = LIBRA_UNIFORM_DECONVERGE_RADIAL;
    float param_13 = 0.0;
    float param_14 = -2.0;
    float param_15 = 2.0;
    float param_16 = LIBRA_UNIFORM_DECONVERGE_LINEAR;
    float param_17 = 0.0;
    float param_18 = -2.0;
    float param_19 = 2.0;
    float limit = max(abs(mix_master(param_12, param_13, param_14, param_15)), abs(mix_master(param_16, param_17, param_18, param_19)));
    float merge = max(0.0, limit - 1.0);
    deconverge_color = mix((color + deconverge_color) / vec3(2.0), deconverge_color, vec3(merge));
    vec3 param_20 = deconverge_color;
    FragColor = vec4(encode_gamma(param_20), 1.0);
}
