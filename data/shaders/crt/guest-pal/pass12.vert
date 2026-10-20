#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIGMA_HOR;
uniform float LIBRA_UNIFORM_HSHARPNESS;
uniform float LIBRA_UNIFORM_S_SHARP;
uniform float LIBRA_UNIFORM_HARNG;
uniform float LIBRA_UNIFORM_HSHARP;
uniform float LIBRA_UNIFORM_spike;
uniform float LIBRA_UNIFORM_filter_res;
uniform float LIBRA_UNIFORM_MAXS;
uniform float LIBRA_UNIFORM_auto_res;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
float auto_rez;

void main()
{
    float _24;
    if (LIBRA_UNIFORM_OriginalSize.y < 375.0)
    {
        _24 = mix(1.0, 1.85000002384185791015625, clamp((LIBRA_UNIFORM_auto_res * round(LIBRA_UNIFORM_OriginalSize.x / 300.0)) - 1.0, 0.0, 1.0));
    }
    else
    {
        _24 = 1.0;
    }
    auto_rez = _24;
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
