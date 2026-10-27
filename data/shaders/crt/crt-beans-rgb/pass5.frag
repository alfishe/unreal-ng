#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 pos = LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy;
    vec2 center = floor(pos - vec2(0.5)) + vec2(0.5);
    vec2 x = (pos - center) + vec2(1.0);
    vec2 weight0 = ((x * x) * ((abs(x) * 0.25) - vec2(0.75))) + vec2(1.0);
    x = pos - center;
    vec2 weight1 = ((x * x) * ((abs(x) * 0.25) - vec2(0.75))) + vec2(1.0);
    x = (pos - center) - vec2(1.0);
    vec2 weight2 = ((x * x) * ((abs(x) * 0.25) - vec2(0.75))) + vec2(1.0);
    x = (pos - center) - vec2(2.0);
    vec2 weight3 = ((x * x) * ((abs(x) * 0.25) - vec2(0.75))) + vec2(1.0);
    vec2 scale0 = weight0 + weight1;
    vec2 scale1 = weight2 + weight3;
    vec2 coord0 = ((center - vec2(1.0)) + (weight1 / scale0)) * LIBRA_UNIFORM_SourceSize.zw;
    vec2 coord1 = ((center + vec2(1.0)) + (weight3 / scale1)) * LIBRA_UNIFORM_SourceSize.zw;
    vec3 value = (((textureLod(LIBRA_TEXTURE_Source, vec2(coord0.x, coord0.y), 0.0).xyz * scale0.x) + (textureLod(LIBRA_TEXTURE_Source, vec2(coord1.x, coord0.y), 0.0).xyz * scale1.x)) * scale0.y) + (((textureLod(LIBRA_TEXTURE_Source, vec2(coord0.x, coord1.y), 0.0).xyz * scale0.x) + (textureLod(LIBRA_TEXTURE_Source, vec2(coord1.x, coord1.y), 0.0).xyz * scale1.x)) * scale1.y);
    vec2 weight_sum = scale0 + scale1;
    value /= vec3(weight_sum.x * weight_sum.y);
    FragColor = vec4(value, 1.0);
}
