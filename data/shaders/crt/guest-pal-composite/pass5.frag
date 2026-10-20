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
uniform float LIBRA_UNIFORM_ntsc_pseudo;

uniform sampler2D LIBRA_TEXTURE_InterlacePass;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
mat3 mmat;
float auto_rez;

vec3 rgb2yuv(vec3 rgb)
{
    return rgb * mat3(vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625), vec3(-0.14712999761104583740234375, -0.2888599932193756103515625, 0.43599998950958251953125), vec3(0.6150000095367431640625, -0.51498997211456298828125, -0.100010000169277191162109375));
}

float edge_cycles(int line, uint frameCount)
{
    float cycles = float(line) * (-0.25);
    bool _126 = LIBRA_UNIFORM_PHMOD == 2.0;
    bool _142;
    if (!_126)
    {
        bool _132 = LIBRA_UNIFORM_PHMOD == 0.0;
        bool _141;
        if (_132)
        {
            _141 = (LIBRA_UNIFORM_OriginalSize.y / auto_rez) > 375.0;
        }
        else
        {
            _141 = _132;
        }
        _142 = _141;
    }
    else
    {
        _142 = _126;
    }
    if (_142)
    {
        cycles += (float(frameCount & 7u) * (-0.125));
    }
    return cycles;
}

void main()
{
    mmat = mat3(vec3(LIBRA_UNIFORM_BRIGHTNESS, LIBRA_UNIFORM_FRINGING, LIBRA_UNIFORM_FRINGING), vec3(LIBRA_UNIFORM_ARTIFACTING, 2.0 * LIBRA_UNIFORM_SATURATION, 0.0), vec3(LIBRA_UNIFORM_ARTIFACTING, 0.0, 2.0 * LIBRA_UNIFORM_SATURATION));
    float _65;
    if (LIBRA_UNIFORM_ntsc_pseudo == 1.0)
    {
        _65 = 1.0;
    }
    else
    {
        bool _71 = LIBRA_UNIFORM_ntsc_pseudo == 2.0;
        bool _83;
        if (_71)
        {
            _83 = (LIBRA_UNIFORM_OriginalSize.x / LIBRA_UNIFORM_OriginalSize.y) >= 2.0;
        }
        else
        {
            _83 = _71;
        }
        bool _98;
        if (!_83)
        {
            bool _90 = LIBRA_UNIFORM_ntsc_pseudo == 3.0;
            bool _97;
            if (_90)
            {
                _97 = LIBRA_UNIFORM_OriginalSize.x >= 480.0;
            }
            else
            {
                _97 = _90;
            }
            _98 = _97;
        }
        else
        {
            _98 = _83;
        }
        _65 = _98 ? 2.0 : 1.0;
    }
    auto_rez = _65;
    vec3 rgb = texture(LIBRA_TEXTURE_InterlacePass, LIBRA_VARYING_0).xyz;
    vec3 param = rgb;
    vec3 yuv = rgb2yuv(param);
    yuv.x = pow(max(yuv.x, 0.0), LIBRA_UNIFORM_GCORR);
    vec3 res4 = yuv;
    float lum = yuv.x;
    vec3 yuvd = vec3(yuv.x, texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).zw);
    if (LIBRA_UNIFORM_DLINE < 2.5)
    {
        vec3 param_1 = texture(LIBRA_TEXTURE_InterlacePass, LIBRA_VARYING_0 - vec2(0.0, LIBRA_UNIFORM_OriginalSize.w * auto_rez)).xyz;
        yuvd = rgb2yuv(param_1);
        yuvd.x = lum;
    }
    int line = int(floor((LIBRA_VARYING_0.y * LIBRA_UNIFORM_OriginalSize.y) / auto_rez));
    float xN = LIBRA_VARYING_0.x * 283.7515869140625;
    int param_2 = line;
    uint param_3 = LIBRA_UNIFORM_FrameCount;
    float cycles = xN + edge_cycles(param_2, param_3);
    float phase = 6.283185482025146484375 * cycles;
    float linesign = ((line % 2) == 0) ? 1.0 : (-1.0);
    vec2 uvmod = vec2(cos(phase), sin(phase * linesign));
    vec3 _266 = yuv;
    vec2 _269 = _266.yz * uvmod;
    yuv.y = _269.x;
    yuv.z = _269.y;
    yuv *= mmat;
    vec3 _278 = yuv;
    vec2 _281 = _278.yz * uvmod;
    yuv.y = _281.x;
    yuv.z = _281.y;
    if (LIBRA_UNIFORM_DLINE > 0.5)
    {
        if (LIBRA_UNIFORM_DLINE > 4.5)
        {
            uvmod = vec2(-uvmod.x, -uvmod.y);
        }
        else
        {
            uvmod = vec2(uvmod.x, -uvmod.y);
        }
        vec3 _312 = yuvd;
        vec2 _315 = _312.yz * uvmod;
        yuvd.y = _315.x;
        yuvd.z = _315.y;
        yuvd *= mmat;
        vec3 _323 = yuvd;
        vec2 _326 = _323.yz * uvmod;
        yuvd.y = _326.x;
        yuvd.z = _326.y;
        bool _333 = LIBRA_UNIFORM_DLINE == 2.0;
        bool _341;
        if (!_333)
        {
            _341 = LIBRA_UNIFORM_DLINE == 4.0;
        }
        else
        {
            _341 = _333;
        }
        bool _349;
        if (!_341)
        {
            _349 = LIBRA_UNIFORM_DLINE == 6.0;
        }
        else
        {
            _349 = _341;
        }
        if (_349)
        {
            yuv = mix(yuv, yuvd, vec3(0.5));
        }
        else
        {
            yuv.x = mix(yuv.x, yuvd.x, 0.5);
        }
    }
    if (LIBRA_UNIFORM_PAL_FILT == 4.0)
    {
        yuv = res4;
    }
    FragColor = vec4(yuv, lum);
}
