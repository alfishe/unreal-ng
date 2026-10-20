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
uniform float LIBRA_UNIFORM_ntsc_pseudo;
uniform float LIBRA_UNIFORM_auto_res;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
float auto_rez;

void main()
{
    float _23;
    if (LIBRA_UNIFORM_ntsc_pseudo == 1.0)
    {
        _23 = mix(1.0, 1.85000002384185791015625, clamp((0.0 * round(LIBRA_UNIFORM_OriginalSize.x / 300.0)) - 1.0, 0.0, 1.0));
    }
    else
    {
        bool _43 = LIBRA_UNIFORM_ntsc_pseudo == 2.0;
        bool _53;
        if (_43)
        {
            _53 = (LIBRA_UNIFORM_OriginalSize.x / LIBRA_UNIFORM_OriginalSize.y) >= 2.0;
        }
        else
        {
            _53 = _43;
        }
        bool _68;
        if (!_53)
        {
            bool _60 = LIBRA_UNIFORM_ntsc_pseudo == 3.0;
            bool _67;
            if (_60)
            {
                _67 = LIBRA_UNIFORM_OriginalSize.x >= 480.0;
            }
            else
            {
                _67 = _60;
            }
            _68 = _67;
        }
        else
        {
            _68 = _53;
        }
        float _69;
        if (_68)
        {
            _69 = mix(1.0, 1.85000002384185791015625, clamp((1.0 * round(LIBRA_UNIFORM_OriginalSize.x / 300.0)) - 1.0, 0.0, 1.0));
        }
        else
        {
            _69 = 1.0;
        }
        _23 = _69;
    }
    auto_rez = _23;
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * 1.000010013580322265625;
}
