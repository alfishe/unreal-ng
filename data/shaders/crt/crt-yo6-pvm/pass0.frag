#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_Source;

in float LIBRA_VARYING_1;
in float LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    float clip0 = (sign(LIBRA_VARYING_1) + 1.0) / 2.0;
    float clipH = (sign(LIBRA_UNIFORM_SourceSize.y - LIBRA_VARYING_1) + 1.0) / 2.0;
    vec3 col = texture(LIBRA_TEXTURE_Source, vec2(LIBRA_VARYING_0, LIBRA_VARYING_1 / LIBRA_UNIFORM_SourceSize.y)).xyz;
    FragColor = vec4(col * (clip0 * clipH), 1.0);
}
