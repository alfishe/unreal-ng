#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_acc_modulate;

uniform sampler2D LIBRA_TEXTURE_PassFeedback1;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 a = texture(LIBRA_TEXTURE_PassFeedback1, LIBRA_VARYING_0) * vec4(LIBRA_UNIFORM_acc_modulate);
    vec4 b = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
    FragColor = max(a, b * 0.959999978542327880859375);
}
