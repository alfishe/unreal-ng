#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform float LIBRA_UNIFORM_blurx;
uniform float LIBRA_UNIFORM_blury;
uniform float LIBRA_UNIFORM_warpx;
uniform float LIBRA_UNIFORM_warpy;
uniform float LIBRA_UNIFORM_corner;
uniform float LIBRA_UNIFORM_smoothness;
uniform float LIBRA_UNIFORM_scanlow;
uniform float LIBRA_UNIFORM_scanhigh;
uniform float LIBRA_UNIFORM_beamlow;
uniform float LIBRA_UNIFORM_beamhigh;
uniform float LIBRA_UNIFORM_brightboost1;
uniform float LIBRA_UNIFORM_brightboost2;
uniform float LIBRA_UNIFORM_Shadowmask;
uniform float LIBRA_UNIFORM_masksize;
uniform float LIBRA_UNIFORM_MaskDark;
uniform float LIBRA_UNIFORM_MaskLight;
uniform float LIBRA_UNIFORM_slotmask;
uniform float LIBRA_UNIFORM_slotwidth;
uniform float LIBRA_UNIFORM_double_slot;
uniform float LIBRA_UNIFORM_slotms;
uniform float LIBRA_UNIFORM_GAMMA_OUT;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_glow_str;
uniform float LIBRA_UNIFORM_sat;
uniform float LIBRA_UNIFORM_contrast;
uniform float LIBRA_UNIFORM_nois;
uniform float LIBRA_UNIFORM_WP;
uniform float LIBRA_UNIFORM_inter;
uniform float LIBRA_UNIFORM_vignette;
uniform float LIBRA_UNIFORM_vpower;
uniform float LIBRA_UNIFORM_vstr;
uniform float LIBRA_UNIFORM_alloff;
uniform float LIBRA_UNIFORM_postbr;
uniform float LIBRA_UNIFORM_PRE_SCALE;
uniform float LIBRA_UNIFORM_preserve;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_rg;
uniform float LIBRA_UNIFORM_rb;
uniform float LIBRA_UNIFORM_gr;
uniform float LIBRA_UNIFORM_gb;
uniform float LIBRA_UNIFORM_br;
uniform float LIBRA_UNIFORM_bg;
uniform float LIBRA_UNIFORM_Downscale;
uniform float LIBRA_UNIFORM_quality;
uniform float LIBRA_UNIFORM_palette_fix;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.00010001659393310546875;
}
