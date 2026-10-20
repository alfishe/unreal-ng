#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform uint LIBRA_UNIFORM_FrameCount;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform float LIBRA_UNIFORM_lsmooth;
uniform float LIBRA_UNIFORM_lsdev;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
