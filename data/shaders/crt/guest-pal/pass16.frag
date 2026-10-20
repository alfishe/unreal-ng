#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIZEVB;
uniform float LIBRA_UNIFORM_SIGMA_VB;
uniform float LIBRA_UNIFORM_FINE_BLOOM;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
float FINE_BLOOM;
float invsqrsigma;

float gaussian(float x)
{
    return exp(((-x) * x) * invsqrsigma);
}

void main()
{
    float _27;
    if (LIBRA_UNIFORM_FINE_BLOOM > 0.5)
    {
        _27 = LIBRA_UNIFORM_FINE_BLOOM;
    }
    else
    {
        _27 = mix(0.75, 0.5, -LIBRA_UNIFORM_FINE_BLOOM);
    }
    FINE_BLOOM = _27;
    invsqrsigma = 1.0 / ((2.0 * LIBRA_UNIFORM_SIGMA_VB) * LIBRA_UNIFORM_SIGMA_VB);
    vec4 SourceSize1 = LIBRA_UNIFORM_SourceSize;
    SourceSize1.y = LIBRA_UNIFORM_OriginalSize.yw.x;
    SourceSize1.w = LIBRA_UNIFORM_OriginalSize.yw.y;
    SourceSize1 *= vec4(FINE_BLOOM, FINE_BLOOM, 1.0 / FINE_BLOOM, 1.0 / FINE_BLOOM);
    float f = fract(SourceSize1.y * LIBRA_VARYING_0.y);
    f = 0.5 - f;
    vec2 tex = (floor(SourceSize1.xy * LIBRA_VARYING_0) * SourceSize1.zw) + (SourceSize1.zw * 0.5);
    vec4 color = vec4(0.0);
    vec2 dy = vec2(0.0, SourceSize1.w);
    float wsum = 0.0;
    float _122 = -LIBRA_UNIFORM_SIZEVB;
    float n = _122;
    do
    {
        vec4 pixel = texture(LIBRA_TEXTURE_Source, tex + (dy * n));
        float param = n + f;
        float w = gaussian(param);
        pixel.w *= (pixel.w * pixel.w);
        color += (pixel * w);
        wsum += w;
        n += 1.0;
    } while (n <= LIBRA_UNIFORM_SIZEVB);
    color /= vec4(wsum);
    FragColor = vec4(color.xyz, pow(color.w, 0.17499999701976776123046875));
}
