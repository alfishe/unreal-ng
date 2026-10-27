#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIZEV;
uniform float LIBRA_UNIFORM_SIGMA_V;
uniform float LIBRA_UNIFORM_FINE_GLOW;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
float FINE_GLOW;
float invsqrsigma;

float gaussian(float x)
{
    return exp(((-x) * x) * invsqrsigma);
}

void main()
{
    float _27;
    if (LIBRA_UNIFORM_FINE_GLOW > 0.5)
    {
        _27 = LIBRA_UNIFORM_FINE_GLOW;
    }
    else
    {
        _27 = mix(0.75, 0.5, -LIBRA_UNIFORM_FINE_GLOW);
    }
    FINE_GLOW = _27;
    invsqrsigma = 1.0 / ((2.0 * LIBRA_UNIFORM_SIGMA_V) * LIBRA_UNIFORM_SIGMA_V);
    vec4 SourceSize1 = vec4(LIBRA_UNIFORM_SourceSize.x, LIBRA_UNIFORM_OriginalSize.y, LIBRA_UNIFORM_SourceSize.z, LIBRA_UNIFORM_OriginalSize.w) * vec4(FINE_GLOW, FINE_GLOW, 1.0 / FINE_GLOW, 1.0 / FINE_GLOW);
    float f = fract(SourceSize1.y * LIBRA_VARYING_0.y);
    f = 0.5 - f;
    vec2 tex = (floor(SourceSize1.xy * LIBRA_VARYING_0) * SourceSize1.zw) + (SourceSize1.zw * 0.5);
    vec3 color = vec3(0.0);
    vec2 dy = vec2(0.0, SourceSize1.w);
    float wsum = 0.0;
    float _124 = -LIBRA_UNIFORM_SIZEV;
    float n = _124;
    do
    {
        vec3 pixel = texture(LIBRA_TEXTURE_Source, tex + (dy * n)).xyz;
        float param = n + f;
        float w = gaussian(param);
        color += (pixel * w);
        wsum += w;
        n += 1.0;
    } while (n <= LIBRA_UNIFORM_SIZEV);
    color /= vec3(wsum);
    FragColor = vec4(color, 1.0);
}
