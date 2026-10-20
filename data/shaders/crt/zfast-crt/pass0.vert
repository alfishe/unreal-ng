#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_BLURSCALEX;
uniform float LIBRA_UNIFORM_LOWLUMSCAN;
uniform float LIBRA_UNIFORM_HILUMSCAN;
uniform float LIBRA_UNIFORM_BRIGHTBOOST;
uniform float LIBRA_UNIFORM_MASK_DARK;
uniform float LIBRA_UNIFORM_MASK_FADE;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out float LIBRA_VARYING_1;
out vec2 LIBRA_VARYING_2;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
    LIBRA_VARYING_1 = 0.33329999446868896484375 * LIBRA_UNIFORM_MASK_FADE;
    LIBRA_VARYING_2 = vec2(1.0) / LIBRA_UNIFORM_SourceSize.xy;
}
