#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_HDRMode;

uniform float LIBRA_UNIFORM_hcrt_colour_space;
uniform float LIBRA_UNIFORM_hcrt_lcd_resolution;
uniform float LIBRA_UNIFORM_hcrt_lcd_subpixel;
uniform float LIBRA_UNIFORM_hcrt_colour_system;
uniform float LIBRA_UNIFORM_hcrt_phosphor_set;
uniform float LIBRA_UNIFORM_hcrt_white_temperature_D65;
uniform float LIBRA_UNIFORM_hcrt_white_temperature_D93;
uniform float LIBRA_UNIFORM_hcrt_brightness;
uniform float LIBRA_UNIFORM_hcrt_contrast;
uniform float LIBRA_UNIFORM_hcrt_saturation;
uniform float LIBRA_UNIFORM_hcrt_gamma_in;
uniform float LIBRA_UNIFORM_hcrt_gamma_cutoff;
uniform float LIBRA_UNIFORM_hcrt_colour_boost_factor;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * vec2(1.000010013580322265625);
}
