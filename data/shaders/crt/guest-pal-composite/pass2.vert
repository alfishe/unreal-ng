#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_TNTC;
uniform float LIBRA_UNIFORM_LS;
uniform float LIBRA_UNIFORM_LUTLOW;
uniform float LIBRA_UNIFORM_LUTBR;
uniform float LIBRA_UNIFORM_CP;
uniform float LIBRA_UNIFORM_CS;
uniform float LIBRA_UNIFORM_WP;
uniform float LIBRA_UNIFORM_wp_saturation;
uniform float LIBRA_UNIFORM_AS;
uniform float LIBRA_UNIFORM_agsat;
uniform float LIBRA_UNIFORM_BP;
uniform float LIBRA_UNIFORM_GPGXmscf;
uniform float LIBRA_UNIFORM_sega_fix;
uniform float LIBRA_UNIFORM_sms_blue;
uniform float LIBRA_UNIFORM_MD_Pal;
uniform float LIBRA_UNIFORM_IREc;
uniform float LIBRA_UNIFORM_pre_bb;
uniform float LIBRA_UNIFORM_contr;
uniform float LIBRA_UNIFORM_pre_gc;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
}
