#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_SIZEV;
uniform float LIBRA_UNIFORM_SIGMA_V;
uniform float LIBRA_UNIFORM_FINE_GLOW;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
float FINE_GLOW;

void main()
{
    float _23;
    if (LIBRA_UNIFORM_FINE_GLOW > 0.5)
    {
        _23 = LIBRA_UNIFORM_FINE_GLOW;
    }
    else
    {
        _23 = mix(0.75, 0.5, -LIBRA_UNIFORM_FINE_GLOW);
    }
    FINE_GLOW = _23;
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
}
