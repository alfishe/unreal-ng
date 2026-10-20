#version 330

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_inter;
uniform float LIBRA_UNIFORM_interm;
uniform float LIBRA_UNIFORM_iscan;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_iscans;
uniform float LIBRA_UNIFORM_hiscan;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
