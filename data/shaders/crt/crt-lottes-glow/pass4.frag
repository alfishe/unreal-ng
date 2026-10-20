#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_ORIG_LINEARIZED;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec3 diff = clamp(texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz - texture(LIBRA_TEXTURE_ORIG_LINEARIZED, LIBRA_VARYING_0).xyz, vec3(0.0), vec3(1.0));
    FragColor = vec4(diff, 1.0);
}
