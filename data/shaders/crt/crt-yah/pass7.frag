#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_FrameTimeDelta;
uniform float LIBRA_UNIFORM_OriginalFPS;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;

uniform float LIBRA_UNIFORM_PHOSPHOR_AMOUNT;
uniform float LIBRA_UNIFORM_PHOSPHOR_DECAY;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_PhosphorPassFeedback;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
}

float srgb_to_linear(float value, float gamma)
{
    float _73;
    if (value <= 0.0404481999576091766357421875)
    {
        _73 = value / 12.9200000762939453125;
    }
    else
    {
        _73 = pow((value + 0.054999999701976776123046875) / 1.05499994754791259765625, gamma);
    }
    return _73;
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

float GetUniformFrameFactor()
{
    float factor = 1.0;
    float _170;
    if (factor > 1.0)
    {
        _170 = round(factor);
    }
    else
    {
        _170 = round(factor * 4.0) / 4.0;
    }
    return _170;
}

float linear_to_srgb(float value, float gamma)
{
    float _117;
    if (value <= 0.0031306999735534191131591796875)
    {
        _117 = value * 12.9200000762939453125;
    }
    else
    {
        _117 = (1.05499994754791259765625 * pow(value, 1.0 / gamma)) - 0.054999999701976776123046875;
    }
    return _117;
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
    float param = LIBRA_UNIFORM_PHOSPHOR_AMOUNT;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        FragColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
        return;
    }
    vec3 param_4 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 current = decode_gamma(param_4);
    vec3 param_5 = texture(LIBRA_TEXTURE_PhosphorPassFeedback, LIBRA_VARYING_0).xyz;
    vec3 feedback = decode_gamma(param_5);
    float r = feedback.x;
    float g = feedback.y;
    float b = feedback.z;
    float param_6 = LIBRA_UNIFORM_PHOSPHOR_AMOUNT;
    float param_7 = 0.0;
    float param_8 = 0.0;
    float param_9 = 1.0;
    float amount = 1.0 / (mix_master(param_6, param_7, param_8, param_9) * 0.5);
    float decay = LIBRA_UNIFORM_PHOSPHOR_DECAY + 1.0;
    decay *= GetUniformFrameFactor();
    float _258;
    if (r > 0.0)
    {
        _258 = pow(amount + pow(1.0 / r, 1.0 / decay), -decay);
    }
    else
    {
        _258 = r;
    }
    r = _258;
    float _276;
    if (g > 0.0)
    {
        _276 = pow(amount + pow(1.0 / g, 1.0 / decay), -decay);
    }
    else
    {
        _276 = g;
    }
    g = _276;
    float _294;
    if (b > 0.0)
    {
        _294 = pow(amount + pow(1.0 / b, 1.0 / decay), -decay);
    }
    else
    {
        _294 = b;
    }
    b = _294;
    r = max(current.x, r);
    g = max(current.y, g);
    b = max(current.z, b);
    vec3 param_10 = vec3(r, g, b);
    FragColor = vec4(encode_gamma(param_10), 1.0);
}
