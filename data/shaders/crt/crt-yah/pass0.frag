#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;

uniform float LIBRA_UNIFORM_COLOR_PROFILE;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_SamplerLutNtsc;
uniform sampler2D LIBRA_TEXTURE_SamplerLutTrinitron;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
}

vec3 apply_lut(vec3 color, sampler2D lut_sampler)
{
    float lut_size = float(textureSize(lut_sampler, 0).y);
    float red = ((color.x * (lut_size - 1.0)) + 0.5) / (lut_size * lut_size);
    float green = ((color.y * (lut_size - 1.0)) + 0.5) / lut_size;
    float blue = (floor(color.z * (lut_size - 1.0)) / lut_size) + red;
    return texture(lut_sampler, vec2(blue, green)).xyz;
}

void main()
{
    float param = LIBRA_UNIFORM_COLOR_PROFILE;
    float param_1 = 0.0;
    float param_2 = -1.0;
    float param_3 = 1.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        FragColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
        return;
    }
    vec3 color = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 param_4 = color;
    vec3 ntsc_color = apply_lut(param_4, LIBRA_TEXTURE_SamplerLutNtsc);
    vec3 param_5 = color;
    vec3 trinitron_color = apply_lut(param_5, LIBRA_TEXTURE_SamplerLutTrinitron);
    float param_6 = LIBRA_UNIFORM_COLOR_PROFILE;
    float param_7 = 0.0;
    float param_8 = -1.0;
    float param_9 = 1.0;
    vec3 _151;
    if (mix_master(param_6, param_7, param_8, param_9) < 0.0)
    {
        float param_10 = LIBRA_UNIFORM_COLOR_PROFILE;
        float param_11 = 0.0;
        float param_12 = -1.0;
        float param_13 = 1.0;
        _151 = mix(color, ntsc_color, vec3(-mix_master(param_10, param_11, param_12, param_13)));
    }
    else
    {
        float param_14 = LIBRA_UNIFORM_COLOR_PROFILE;
        float param_15 = 0.0;
        float param_16 = -1.0;
        float param_17 = 1.0;
        _151 = mix(color, trinitron_color, vec3(mix_master(param_14, param_15, param_16, param_17)));
    }
    color = _151;
    FragColor = vec4(color, 1.0);
}
