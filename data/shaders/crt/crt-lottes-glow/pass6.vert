#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_hardScan;
uniform float LIBRA_UNIFORM_hardPix;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_maskDark;
uniform float LIBRA_UNIFORM_maskLight;
uniform float LIBRA_UNIFORM_scaleInLinearGamma;
uniform float LIBRA_UNIFORM_shadowMask;
uniform float LIBRA_UNIFORM_brightBoost;
uniform float LIBRA_UNIFORM_hardBloomScan;
uniform float LIBRA_UNIFORM_hardBloomPix;
uniform float LIBRA_UNIFORM_bloomAmount;
uniform float LIBRA_UNIFORM_shape;
uniform float LIBRA_UNIFORM_DIFFUSION;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
