#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_PAL_FILT;
uniform float LIBRA_UNIFORM_pal_scale;
uniform float LIBRA_UNIFORM_pal_ring;
uniform float LIBRA_UNIFORM_pal_res;
uniform float LIBRA_UNIFORM_GCORR;
uniform float LIBRA_UNIFORM_RFNOISE;
uniform float LIBRA_UNIFORM_RFNOISE1;
uniform float LIBRA_UNIFORM_RFNOISE2;
uniform float LIBRA_UNIFORM_CHROMAF;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
float RFNOISE;

void main()
{
    RFNOISE = (LIBRA_UNIFORM_RFNOISE * LIBRA_UNIFORM_RFNOISE) + 9.9999997473787516355514526367188e-06;
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord + vec2(0.125 * LIBRA_UNIFORM_OriginalSize.z, 0.0);
}
