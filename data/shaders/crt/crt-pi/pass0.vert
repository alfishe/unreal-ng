#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_CURVATURE_X;
uniform float LIBRA_UNIFORM_CURVATURE_Y;
uniform float LIBRA_UNIFORM_MASK_BRIGHTNESS;
uniform float LIBRA_UNIFORM_SCANLINE_WEIGHT;
uniform float LIBRA_UNIFORM_SCANLINE_GAP_BRIGHTNESS;
uniform float LIBRA_UNIFORM_BLOOM_FACTOR;
uniform float LIBRA_UNIFORM_INPUT_GAMMA;
uniform float LIBRA_UNIFORM_OUTPUT_GAMMA;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out float LIBRA_VARYING_1;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
    LIBRA_VARYING_1 = (LIBRA_UNIFORM_SourceSize.y * LIBRA_UNIFORM_OutputSize.w) * 0.3333333432674407958984375;
}
