#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_LinearizePassSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIZEHB;
uniform float LIBRA_UNIFORM_SIGMA_HB;
uniform float LIBRA_UNIFORM_FINE_BLOOM;
uniform float LIBRA_UNIFORM_auto_res;
uniform float LIBRA_UNIFORM_GLOW_MAX;

uniform sampler2D LIBRA_TEXTURE_LinearizePass;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
float auto_rez;
float FINE_BLOOM;
float invsqrsigma;

float gaussian(float x)
{
    return exp(((-x) * x) * invsqrsigma);
}

void main()
{
    float _28;
    if (LIBRA_UNIFORM_OriginalSize.y < 375.0)
    {
        _28 = mix(1.0, 2.0, clamp((LIBRA_UNIFORM_auto_res * round(LIBRA_UNIFORM_OriginalSize.x / 300.0)) - 1.0, 0.0, 1.0));
    }
    else
    {
        _28 = 1.0;
    }
    auto_rez = _28;
    float _55;
    if (LIBRA_UNIFORM_FINE_BLOOM > 0.5)
    {
        _55 = LIBRA_UNIFORM_FINE_BLOOM;
    }
    else
    {
        _55 = mix(0.75, 0.5, -LIBRA_UNIFORM_FINE_BLOOM);
    }
    FINE_BLOOM = _55;
    invsqrsigma = 1.0 / ((((2.0 * LIBRA_UNIFORM_SIGMA_HB) * LIBRA_UNIFORM_SIGMA_HB) * auto_rez) * auto_rez);
    vec4 SourceSize1 = LIBRA_UNIFORM_OriginalSize * vec4(FINE_BLOOM, FINE_BLOOM, 1.0 / FINE_BLOOM, 1.0 / FINE_BLOOM);
    float f = fract(SourceSize1.x * LIBRA_VARYING_0.x);
    f = 0.5 - f;
    vec2 tex = (floor(SourceSize1.xy * LIBRA_VARYING_0) * SourceSize1.zw) + (SourceSize1.zw * 0.5);
    vec4 color = vec4(0.0);
    vec2 dx = vec2(SourceSize1.z, 0.0);
    float wsum = 0.0;
    float _142 = (-LIBRA_UNIFORM_SIZEHB) * auto_rez;
    float n = _142;
    do
    {
        vec4 pixel = texture(LIBRA_TEXTURE_LinearizePass, tex + (dx * n));
        float param = n + f;
        float w = gaussian(param);
        pixel.w = max(max(pixel.x, pixel.y), pixel.z);
        pixel.w *= (pixel.w * pixel.w);
        color += (pixel * w);
        wsum += w;
        n += 1.0;
    } while (n <= (LIBRA_UNIFORM_SIZEHB * auto_rez));
    color /= vec4(wsum);
    FragColor = vec4(color.xyz, pow(color.w, 0.3333329856395721435546875));
}
