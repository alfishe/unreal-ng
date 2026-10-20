#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out vec2 LIBRA_VARYING_1;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
    vec2 dxdy_scale = LIBRA_UNIFORM_SourceSize.xy / LIBRA_UNIFORM_OutputSize.xy;
    vec2 dxdy = dxdy_scale / LIBRA_UNIFORM_SourceSize.xy;
    LIBRA_VARYING_1 = vec2(0.0, dxdy.y);
}
