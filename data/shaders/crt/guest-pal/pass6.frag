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

uniform sampler2D LIBRA_TEXTURE_InterlacePass;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
mat3 mmat;

vec3 rgb2yuv(vec3 rgb)
{
    return rgb * mat3(vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625), vec3(-0.14712999761104583740234375, -0.2888599932193756103515625, 0.43599998950958251953125), vec3(0.6150000095367431640625, -0.51498997211456298828125, -0.100010000169277191162109375));
}

float edge_cycles(int line, uint frameCount)
{
    float cycles = float(line) * (-0.25);
    bool _84 = LIBRA_UNIFORM_PHMOD == 2.0;
    bool _100;
    if (!_84)
    {
        bool _90 = LIBRA_UNIFORM_PHMOD == 0.0;
        bool _99;
        if (_90)
        {
            _99 = LIBRA_UNIFORM_OriginalSize.y > 375.0;
        }
        else
        {
            _99 = _90;
        }
        _100 = _99;
    }
    else
    {
        _100 = _84;
    }
    if (_100)
    {
        cycles += (float(frameCount & 7u) * (-0.125));
    }
    return cycles;
}

void main()
{
    mmat = mat3(vec3(LIBRA_UNIFORM_BRIGHTNESS, LIBRA_UNIFORM_FRINGING, LIBRA_UNIFORM_FRINGING), vec3(LIBRA_UNIFORM_ARTIFACTING, 2.0 * LIBRA_UNIFORM_SATURATION, 0.0), vec3(LIBRA_UNIFORM_ARTIFACTING, 0.0, 2.0 * LIBRA_UNIFORM_SATURATION));
    vec3 rgb = texture(LIBRA_TEXTURE_InterlacePass, LIBRA_VARYING_0).xyz;
    vec3 param = rgb;
    vec3 yuv = rgb2yuv(param);
    yuv.x = pow(max(yuv.x, 0.0), LIBRA_UNIFORM_GCORR);
    vec3 res4 = yuv;
    float lum = yuv.x;
    vec3 yuvd = vec3(yuv.x, texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).zw);
    if (LIBRA_UNIFORM_DLINE < 2.5)
    {
        vec3 param_1 = texture(LIBRA_TEXTURE_InterlacePass, LIBRA_VARYING_0 - vec2(0.0, LIBRA_UNIFORM_OriginalSize.w)).xyz;
        yuvd = rgb2yuv(param_1);
        yuvd.x = lum;
    }
    int line = int(floor(LIBRA_VARYING_0.y * LIBRA_UNIFORM_OriginalSize.y));
    float xN = LIBRA_VARYING_0.x * 283.7515869140625;
    int param_2 = line;
    uint param_3 = LIBRA_UNIFORM_FrameCount;
    float cycles = xN + edge_cycles(param_2, param_3);
    float phase = 6.283185482025146484375 * cycles;
    float linesign = ((line % 2) == 0) ? 1.0 : (-1.0);
    vec2 uvmod = vec2(cos(phase), sin(phase * linesign));
    vec3 _221 = yuv;
    vec2 _224 = _221.yz * uvmod;
    yuv.y = _224.x;
    yuv.z = _224.y;
    yuv *= mmat;
    vec3 _233 = yuv;
    vec2 _236 = _233.yz * uvmod;
    yuv.y = _236.x;
    yuv.z = _236.y;
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
        vec3 _267 = yuvd;
        vec2 _270 = _267.yz * uvmod;
        yuvd.y = _270.x;
        yuvd.z = _270.y;
        yuvd *= mmat;
        vec3 _278 = yuvd;
        vec2 _281 = _278.yz * uvmod;
        yuvd.y = _281.x;
        yuvd.z = _281.y;
        bool _288 = LIBRA_UNIFORM_DLINE == 2.0;
        bool _296;
        if (!_288)
        {
            _296 = LIBRA_UNIFORM_DLINE == 4.0;
        }
        else
        {
            _296 = _288;
        }
        bool _304;
        if (!_296)
        {
            _304 = LIBRA_UNIFORM_DLINE == 6.0;
        }
        else
        {
            _304 = _296;
        }
        if (_304)
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
