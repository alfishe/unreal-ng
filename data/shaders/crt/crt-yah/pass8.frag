#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SHARP_AMOUNT;

uniform sampler2D LIBRA_TEXTURE_PhosphorPass;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;
flat in float LIBRA_VARYING_2;
flat in int LIBRA_VARYING_3;
flat in float LIBRA_VARYING_1;

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
    float param = LIBRA_UNIFORM_SHARP_AMOUNT;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 2.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        FragColor = texture(LIBRA_TEXTURE_PhosphorPass, LIBRA_VARYING_0);
        return;
    }
    float sum = 1.0;
    vec3 param_4 = texture(LIBRA_TEXTURE_PhosphorPass, LIBRA_VARYING_0).xyz;
    vec3 color = decode_gamma(param_4);
    float weight = LIBRA_VARYING_2;
    float weight_factor = weight * weight;
    float weight_step = weight * weight_factor;
    for (int i = 1; i <= LIBRA_VARYING_3; i++)
    {
        float base_offset = float(i) * LIBRA_VARYING_1;
        vec2 pos_coord = vec2(LIBRA_VARYING_0.x + (base_offset * LIBRA_UNIFORM_OriginalSize.z), LIBRA_VARYING_0.y);
        vec3 param_5 = texture(LIBRA_TEXTURE_PhosphorPass, pos_coord).xyz;
        color += (decode_gamma(param_5) * weight);
        vec2 neg_coord = vec2(LIBRA_VARYING_0.x + ((-base_offset) * LIBRA_UNIFORM_OriginalSize.z), LIBRA_VARYING_0.y);
        vec3 param_6 = texture(LIBRA_TEXTURE_PhosphorPass, neg_coord).xyz;
        color += (decode_gamma(param_6) * weight);
        sum += (2.0 * weight);
        weight *= weight_step;
        weight_step *= weight_factor;
    }
    vec3 param_7 = color / vec3(sum);
    color = encode_gamma(param_7);
    FragColor = vec4(color, 1.0);
}
