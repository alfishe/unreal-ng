#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_GLOW_WHITEPOINT;
uniform float LIBRA_UNIFORM_GLOW_ROLLOFF;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec3 color = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 factor = clamp(color / vec3(LIBRA_UNIFORM_GLOW_WHITEPOINT), vec3(0.0), vec3(1.0));
    FragColor = vec4(pow(factor, vec3(LIBRA_UNIFORM_GLOW_ROLLOFF)), 1.0);
}
