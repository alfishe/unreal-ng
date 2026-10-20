#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_bloom;
uniform float LIBRA_UNIFORM_halation;
uniform float LIBRA_UNIFORM_scans;
uniform float LIBRA_UNIFORM_gamma_c;
uniform float LIBRA_UNIFORM_gamma_c2;
uniform float LIBRA_UNIFORM_smart_ei;
uniform float LIBRA_UNIFORM_ei_limit;
uniform float LIBRA_UNIFORM_sth;
uniform float LIBRA_UNIFORM_overscanX;
uniform float LIBRA_UNIFORM_overscanY;
uniform float LIBRA_UNIFORM_VShift;
uniform float LIBRA_UNIFORM_c_shape;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_prescalex;
uniform float LIBRA_UNIFORM_scan_falloff;
uniform float LIBRA_UNIFORM_bloom_dist;
uniform float LIBRA_UNIFORM_scangamma;
uniform float LIBRA_UNIFORM_bmask1;
uniform float LIBRA_UNIFORM_hmask1;
uniform float LIBRA_UNIFORM_interm;

uniform float LIBRA_UNIFORM_TATE;
uniform float LIBRA_UNIFORM_IOS;
uniform float LIBRA_UNIFORM_OS;
uniform float LIBRA_UNIFORM_BLOOM;
uniform float LIBRA_UNIFORM_brightboost;
uniform float LIBRA_UNIFORM_brightboost1;
uniform float LIBRA_UNIFORM_gsl;
uniform float LIBRA_UNIFORM_scanline1;
uniform float LIBRA_UNIFORM_scanline2;
uniform float LIBRA_UNIFORM_beam_min;
uniform float LIBRA_UNIFORM_beam_max;
uniform float LIBRA_UNIFORM_beam_size;
uniform float LIBRA_UNIFORM_h_sharp;
uniform float LIBRA_UNIFORM_s_sharp;
uniform float LIBRA_UNIFORM_csize;
uniform float LIBRA_UNIFORM_bsize1;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_spike;
uniform float LIBRA_UNIFORM_ring;
uniform float LIBRA_UNIFORM_no_scanlines;
uniform float LIBRA_UNIFORM_tds;
uniform float LIBRA_UNIFORM_clips;
uniform float LIBRA_UNIFORM_hiscan;
uniform float LIBRA_UNIFORM_rolling_scan;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
