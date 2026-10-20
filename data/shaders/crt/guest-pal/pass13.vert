#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_LinearizePassSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIZEH;
uniform float LIBRA_UNIFORM_SIGMA_H;
uniform float LIBRA_UNIFORM_FINE_GLOW;
uniform float LIBRA_UNIFORM_m_glow;
uniform float LIBRA_UNIFORM_m_glow_cutoff;
uniform float LIBRA_UNIFORM_m_glow_low;
uniform float LIBRA_UNIFORM_m_glow_high;
uniform float LIBRA_UNIFORM_m_glow_dist;
uniform float LIBRA_UNIFORM_m_glow_mask;
uniform float LIBRA_UNIFORM_auto_res;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
float auto_rez;
float FINE_GLOW;

void main()
{
    float _24;
    if (LIBRA_UNIFORM_OriginalSize.y < 375.0)
    {
        _24 = mix(1.0, 2.0, clamp((LIBRA_UNIFORM_auto_res * round(LIBRA_UNIFORM_OriginalSize.x / 300.0)) - 1.0, 0.0, 1.0));
    }
    else
    {
        _24 = 1.0;
    }
    auto_rez = _24;
    float _51;
    if (LIBRA_UNIFORM_FINE_GLOW > 0.5)
    {
        _51 = LIBRA_UNIFORM_FINE_GLOW;
    }
    else
    {
        _51 = mix(0.75, 0.5, -LIBRA_UNIFORM_FINE_GLOW);
    }
    FINE_GLOW = _51;
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
}
