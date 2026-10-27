#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_GLOW_RADIUS;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 get_bloom(sampler2D src, vec2 tex, vec2 dxy)
{
    vec3 bloom = vec3(0.0);
    bloom += (texture(src, tex - (dxy * 4.0)).xyz * 0.001234402996487915515899658203125);
    bloom += (texture(src, tex - (dxy * 3.0)).xyz * 0.01430468820035457611083984375);
    bloom += (texture(src, tex - (dxy * 2.0)).xyz * 0.0823177993297576904296875);
    bloom += (texture(src, tex - dxy).xyz * 0.2352355420589447021484375);
    bloom += (texture(src, tex).xyz * 0.3338151276111602783203125);
    bloom += (texture(src, tex + dxy).xyz * 0.2352355420589447021484375);
    bloom += (texture(src, tex + (dxy * 2.0)).xyz * 0.0823177993297576904296875);
    bloom += (texture(src, tex + (dxy * 3.0)).xyz * 0.01430468820035457611083984375);
    bloom += (texture(src, tex + (dxy * 4.0)).xyz * 0.001234402996487915515899658203125);
    return bloom;
}

void main()
{
    vec2 dx = vec2(LIBRA_UNIFORM_GLOW_RADIUS * LIBRA_UNIFORM_SourceSize.z, 0.0);
    vec2 param = LIBRA_VARYING_0;
    vec2 param_1 = dx;
    vec3 bloom = get_bloom(LIBRA_TEXTURE_Source, param, param_1);
    FragColor = vec4(bloom, 1.0);
}
