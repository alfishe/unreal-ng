#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_MaxSpotSize;
uniform float LIBRA_UNIFORM_MinSpotSize;
uniform float LIBRA_UNIFORM_OddFieldFirst;
uniform float LIBRA_UNIFORM_OverscanHorizontal;
uniform float LIBRA_UNIFORM_OverscanVertical;
uniform float LIBRA_UNIFORM_MaskType;
uniform float LIBRA_UNIFORM_SubpixelMaskPattern;
uniform float LIBRA_UNIFORM_SubpixelPattern;
uniform float LIBRA_UNIFORM_DynamicMaskTriads;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
flat out float LIBRA_VARYING_1;
out vec2 LIBRA_VARYING_2;

void main()
{
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = ((vec2(1.0) - vec2(LIBRA_UNIFORM_OverscanHorizontal, LIBRA_UNIFORM_OverscanVertical)) * (TexCoord - vec2(0.5))) + vec2(0.5);
    LIBRA_VARYING_1 = ((((LIBRA_UNIFORM_OutputSize.x * LIBRA_UNIFORM_OutputSize.w) * LIBRA_UNIFORM_SourceSize.y) * LIBRA_UNIFORM_SourceSize.z) * (1.0 - LIBRA_UNIFORM_OverscanVertical)) / (1.0 - LIBRA_UNIFORM_OverscanHorizontal);
    float _86;
    if (LIBRA_UNIFORM_SourceSize.y > 300.0)
    {
        _86 = 0.5 * LIBRA_VARYING_1;
    }
    else
    {
        _86 = LIBRA_VARYING_1;
    }
    LIBRA_VARYING_1 = _86;
    LIBRA_VARYING_2 = TexCoord;
}
