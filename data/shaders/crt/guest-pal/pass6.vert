#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_BRIGHTNESS;
uniform float LIBRA_UNIFORM_SATURATION;
uniform float LIBRA_UNIFORM_ARTIFACTING;
uniform float LIBRA_UNIFORM_FRINGING;
uniform float LIBRA_UNIFORM_RFNOISE;
uniform float LIBRA_UNIFORM_RFNOISE1;
uniform float LIBRA_UNIFORM_RFNOISE2;
uniform float LIBRA_UNIFORM_PHMOD;
uniform float LIBRA_UNIFORM_DLINE;
uniform float LIBRA_UNIFORM_GCORR;
uniform float LIBRA_UNIFORM_PAL_FILT;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
mat3 mmat;

void main()
{
    mmat = mat3(vec3(LIBRA_UNIFORM_BRIGHTNESS, LIBRA_UNIFORM_FRINGING, LIBRA_UNIFORM_FRINGING), vec3(LIBRA_UNIFORM_ARTIFACTING, 2.0 * LIBRA_UNIFORM_SATURATION, 0.0), vec3(LIBRA_UNIFORM_ARTIFACTING, 0.0, 2.0 * LIBRA_UNIFORM_SATURATION));
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
