#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_use_frame;
uniform float LIBRA_UNIFORM_curvature;
uniform float LIBRA_UNIFORM_wiggle_toggle;
uniform float LIBRA_UNIFORM_scanroll;
uniform float LIBRA_UNIFORM_vignette;
uniform float LIBRA_UNIFORM_ghosting;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = vec2(TexCoord.x, 1.0 - TexCoord.y);
}
