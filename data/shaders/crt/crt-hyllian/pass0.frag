#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_LUT_selector_param;
uniform float LIBRA_UNIFORM_H_InputGamma;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_SamplerLUT1;
uniform sampler2D LIBRA_TEXTURE_SamplerLUT2;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 mixfix(vec3 a, vec3 b, float c)
{
    vec3 _23;
    if (a.z < 1.0)
    {
        _23 = mix(a, b, vec3(c));
    }
    else
    {
        _23 = a;
    }
    return _23;
}

void main()
{
    vec3 imgColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 color;
    if (LIBRA_UNIFORM_LUT_selector_param < 0.5)
    {
        color = imgColor;
    }
    else
    {
        vec3 color2 = vec3(0.0);
        float mixer = 0.0;
        float red = ((imgColor.x * 31.0) + 0.4999000132083892822265625) / 1024.0;
        float green = ((imgColor.y * 31.0) + 0.4999000132083892822265625) / 32.0;
        float blue1 = (floor(imgColor.z * 31.0) / 32.0) + red;
        float blue2 = (ceil(imgColor.z * 31.0) / 32.0) + red;
        mixer = clamp(max((imgColor.z - blue1) / (blue2 - blue1), 0.0), 0.0, 32.0);
        vec3 color1;
        if (LIBRA_UNIFORM_LUT_selector_param < 1.5)
        {
            color1 = texture(LIBRA_TEXTURE_SamplerLUT1, vec2(blue1, green)).xyz;
            color2 = texture(LIBRA_TEXTURE_SamplerLUT1, vec2(blue2, green)).xyz;
        }
        else
        {
            color1 = texture(LIBRA_TEXTURE_SamplerLUT2, vec2(blue1, green)).xyz;
            color2 = texture(LIBRA_TEXTURE_SamplerLUT2, vec2(blue2, green)).xyz;
        }
        vec3 param = color1;
        vec3 param_1 = color2;
        float param_2 = mixer;
        color = mixfix(param, param_1, param_2);
    }
    FragColor = vec4(pow(color, vec3(LIBRA_UNIFORM_H_InputGamma, LIBRA_UNIFORM_H_InputGamma, LIBRA_UNIFORM_H_InputGamma)), 1.0);
}
