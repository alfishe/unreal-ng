#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

layout(location = 0) in vec4 Position;
out float LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out float LIBRA_VARYING_1;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord.x;
    LIBRA_VARYING_1 = (TexCoord.y * LIBRA_UNIFORM_OutputSize.y) - round((LIBRA_UNIFORM_OutputSize.y - LIBRA_UNIFORM_SourceSize.y) / 2.0);
}
