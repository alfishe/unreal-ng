#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform float LIBRA_UNIFORM_GLOW_ENABLE;
uniform float LIBRA_UNIFORM_GLOW_RADIUS;
uniform float LIBRA_UNIFORM_GLOW_STRENGTH;
uniform float LIBRA_UNIFORM_PRESET_OPTION;
uniform float LIBRA_UNIFORM_DISPLAY_RES;
uniform float LIBRA_UNIFORM_PHOSPHOR_LAYOUT;
uniform float LIBRA_UNIFORM_MASK_STRENGTH;
uniform float LIBRA_UNIFORM_BRIGHTBOOST;
uniform float LIBRA_UNIFORM_MONITOR_SUBPIXELS;
uniform float LIBRA_UNIFORM_VSCANLINES;
uniform float LIBRA_UNIFORM_H_OUTPUT_GAMMA;
uniform float LIBRA_UNIFORM_H_MaskGamma;
uniform float LIBRA_UNIFORM_h_curvature;
uniform float LIBRA_UNIFORM_h_shape;
uniform float LIBRA_UNIFORM_h_radius;
uniform float LIBRA_UNIFORM_h_cornersize;
uniform float LIBRA_UNIFORM_h_cornersmooth;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out vec2 LIBRA_VARYING_1;
float r2;
vec2 max_size;
vec2 aspect;
float cornersize;
float cornersmooth;
vec2 overscan;

vec2 get_beam_profile()
{
    vec2 bp = vec2(LIBRA_UNIFORM_PHOSPHOR_LAYOUT, LIBRA_UNIFORM_MASK_STRENGTH);
    if (LIBRA_UNIFORM_DISPLAY_RES < 0.5)
    {
        if (LIBRA_UNIFORM_PRESET_OPTION == 1.0)
        {
            bp = vec2(1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 2.0)
        {
            bp = vec2(2.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 3.0)
        {
            bp = vec2(11.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 4.0)
        {
            bp = vec2(11.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 5.0)
        {
            bp = vec2(7.0, 1.0);
        }
    }
    else
    {
        if (LIBRA_UNIFORM_PRESET_OPTION == 1.0)
        {
            bp = vec2(2.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 2.0)
        {
            bp = vec2(4.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 3.0)
        {
            bp = vec2(14.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 4.0)
        {
            bp = vec2(14.0, 1.0);
        }
        if (LIBRA_UNIFORM_PRESET_OPTION == 5.0)
        {
            bp = vec2(9.0, 1.0);
        }
    }
    return bp;
}

void main()
{
    r2 = LIBRA_UNIFORM_h_radius * LIBRA_UNIFORM_h_radius;
    max_size = vec2(sqrt((r2 - 2.0) / (r2 - 1.0)), 1.0);
    aspect = vec2(1.0, LIBRA_UNIFORM_OutputSize.y / LIBRA_UNIFORM_OutputSize.x);
    cornersize = LIBRA_UNIFORM_h_cornersize * min(aspect.x, aspect.y);
    cornersmooth = LIBRA_UNIFORM_h_cornersmooth / 100.0;
    overscan = vec2(1.0099999904632568359375);
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * vec2(1.00010001659393310546875);
    LIBRA_VARYING_1 = get_beam_profile();
}
