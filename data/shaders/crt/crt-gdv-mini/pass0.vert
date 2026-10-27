#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform float LIBRA_UNIFORM_brightboost;
uniform float LIBRA_UNIFORM_sat;
uniform float LIBRA_UNIFORM_scanline;
uniform float LIBRA_UNIFORM_beam_min;
uniform float LIBRA_UNIFORM_beam_max;
uniform float LIBRA_UNIFORM_h_sharp;
uniform float LIBRA_UNIFORM_shadowMask;
uniform float LIBRA_UNIFORM_masksize;
uniform float LIBRA_UNIFORM_mcut;
uniform float LIBRA_UNIFORM_maskDark;
uniform float LIBRA_UNIFORM_maskLight;
uniform float LIBRA_UNIFORM_CGWG;
uniform float LIBRA_UNIFORM_csize;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_gamma_out;
uniform float LIBRA_UNIFORM_vignette;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.00010001659393310546875;
}
