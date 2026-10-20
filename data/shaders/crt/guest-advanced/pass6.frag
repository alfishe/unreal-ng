#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_LinearizePassSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIZEH;
uniform float LIBRA_UNIFORM_SIGMA_H;
uniform float LIBRA_UNIFORM_FINE_GLOW;
uniform float LIBRA_UNIFORM_m_glow;
uniform float LIBRA_UNIFORM_m_glow_cutoff;
uniform float LIBRA_UNIFORM_m_glow_low;
uniform float LIBRA_UNIFORM_m_glow_high;
uniform float LIBRA_UNIFORM_m_glow_dist;
uniform float LIBRA_UNIFORM_m_glow_mask;

uniform sampler2D LIBRA_TEXTURE_LinearizePass;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
float FINE_GLOW;
float invsqrsigma;

vec3 plant(vec3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06;
    return (tar * r) / vec3(t);
}

float gaussian(float x)
{
    return exp(((-x) * x) * invsqrsigma);
}

void main()
{
    float _34;
    if (LIBRA_UNIFORM_FINE_GLOW > 0.5)
    {
        _34 = LIBRA_UNIFORM_FINE_GLOW;
    }
    else
    {
        _34 = mix(0.75, 0.5, -LIBRA_UNIFORM_FINE_GLOW);
    }
    FINE_GLOW = _34;
    invsqrsigma = 1.0 / ((2.0 * LIBRA_UNIFORM_SIGMA_H) * LIBRA_UNIFORM_SIGMA_H);
    vec4 SourceSize1 = LIBRA_UNIFORM_OriginalSize * vec4(FINE_GLOW, FINE_GLOW, 1.0 / FINE_GLOW, 1.0 / FINE_GLOW);
    float f = fract(SourceSize1.x * LIBRA_VARYING_0.x);
    f = 0.5 - f;
    vec2 tex = (floor(SourceSize1.xy * LIBRA_VARYING_0) * SourceSize1.zw) + (SourceSize1.zw * 0.5);
    vec3 color = vec3(0.0);
    vec2 dx = vec2(SourceSize1.z, 0.0);
    float wsum = 0.0;
    float _140 = -LIBRA_UNIFORM_SIZEH;
    float n = _140;
    do
    {
        vec3 pixel = texture(LIBRA_TEXTURE_LinearizePass, tex + (dx * n)).xyz;
        if (LIBRA_UNIFORM_m_glow > 0.5)
        {
            pixel = max(pixel - vec3(LIBRA_UNIFORM_m_glow_cutoff), vec3(0.0));
            vec3 param = pixel;
            float param_1 = max(max(max(pixel.x, pixel.y), pixel.z) - LIBRA_UNIFORM_m_glow_cutoff, 0.0);
            pixel = plant(param, param_1);
        }
        float param_2 = n + f;
        float w = gaussian(param_2);
        color += (pixel * w);
        wsum += w;
        n += 1.0;
    } while (n <= LIBRA_UNIFORM_SIZEH);
    color /= vec3(wsum);
    FragColor = vec4(color, 1.0);
}
