#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform float LIBRA_UNIFORM_MaxSpotSize;
uniform float LIBRA_UNIFORM_MinSpotSize;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    ivec2 pix_loc = ivec2(floor(LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy));
    vec3 sample_value = texelFetch(LIBRA_TEXTURE_Source, pix_loc, 0).xyz;
    vec3 width_rcp = vec3(1.0) / (vec3(LIBRA_UNIFORM_MinSpotSize * LIBRA_UNIFORM_MaxSpotSize) - (sqrt(sample_value) * ((LIBRA_UNIFORM_MinSpotSize * LIBRA_UNIFORM_MaxSpotSize) - LIBRA_UNIFORM_MaxSpotSize)));
    FragColor.x = width_rcp.x;
    FragColor.y = width_rcp.y;
    FragColor.z = width_rcp.z;
}
