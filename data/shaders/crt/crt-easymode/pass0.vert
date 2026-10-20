#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_BRIGHT_BOOST;
uniform float LIBRA_UNIFORM_DILATION;
uniform float LIBRA_UNIFORM_GAMMA_INPUT;
uniform float LIBRA_UNIFORM_GAMMA_OUTPUT;
uniform float LIBRA_UNIFORM_MASK_SIZE;
uniform float LIBRA_UNIFORM_MASK_STAGGER;
uniform float LIBRA_UNIFORM_MASK_STRENGTH;
uniform float LIBRA_UNIFORM_MASK_DOT_HEIGHT;
uniform float LIBRA_UNIFORM_MASK_DOT_WIDTH;
uniform float LIBRA_UNIFORM_SCANLINE_CUTOFF;
uniform float LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MAX;
uniform float LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MIN;
uniform float LIBRA_UNIFORM_SCANLINE_BRIGHT_MAX;
uniform float LIBRA_UNIFORM_SCANLINE_BRIGHT_MIN;
uniform float LIBRA_UNIFORM_SCANLINE_STRENGTH;
uniform float LIBRA_UNIFORM_SHARPNESS_H;
uniform float LIBRA_UNIFORM_SHARPNESS_V;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
}
