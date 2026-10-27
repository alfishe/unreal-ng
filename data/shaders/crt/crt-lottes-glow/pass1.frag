#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_GAMMA_INPUT;

uniform sampler2D LIBRA_TEXTURE_Source;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;

void main()
{
    FragColor = pow(texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0), vec4(LIBRA_UNIFORM_GAMMA_INPUT));
}
