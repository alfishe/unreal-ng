#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform float LIBRA_UNIFORM_GlowSigma;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    float sigma = LIBRA_UNIFORM_GlowSigma * LIBRA_UNIFORM_SourceSize.y;
    int side_taps = 2 * int(ceil(sigma * 1.5));
    vec2 pos = LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy;
    float weight_sum = 0.0;
    vec3 value = vec3(0.0);
    weight_sum += 1.0;
    value += texelFetch(LIBRA_TEXTURE_Source, ivec2(int(floor(pos.x)), int(floor(pos.y))), 0).xyz;
    float g0 = exp((-1.0) / ((2.0 * sigma) * sigma));
    float g2 = g0 * g0;
    float g1 = g0 * g2;
    for (int distance_y = 1; distance_y <= side_taps; distance_y += 2)
    {
        float weight0 = g0;
        g0 *= g1;
        g1 *= g2;
        float weight1 = g0;
        g0 *= g1;
        g1 *= g2;
        float scale = weight0 + weight1;
        weight_sum = (2.0 * scale) + weight_sum;
        float delta = (float(distance_y) + (weight1 / scale)) * LIBRA_UNIFORM_SourceSize.w;
        value += (textureLod(LIBRA_TEXTURE_Source, vec2(LIBRA_VARYING_0.x, LIBRA_VARYING_0.y - delta), 0.0).xyz * scale);
        value += (textureLod(LIBRA_TEXTURE_Source, vec2(LIBRA_VARYING_0.x, LIBRA_VARYING_0.y + delta), 0.0).xyz * scale);
    }
    vec3 _172 = value / vec3(weight_sum);
    FragColor.x = _172.x;
    FragColor.y = _172.y;
    FragColor.z = _172.z;
}
