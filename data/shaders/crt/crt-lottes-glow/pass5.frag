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

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

float ToLinear1(float c)
{
    if (LIBRA_UNIFORM_scaleInLinearGamma == 0.0)
    {
        return c;
    }
    float _70;
    if (c <= 0.040449999272823333740234375)
    {
        _70 = c / 12.9200000762939453125;
    }
    else
    {
        _70 = pow((c + 0.054999999701976776123046875) / 1.05499994754791259765625, 2.400000095367431640625);
    }
    return _70;
}

vec3 ToLinear(vec3 c)
{
    if (LIBRA_UNIFORM_scaleInLinearGamma == 0.0)
    {
        return c;
    }
    float param = c.x;
    float param_1 = c.y;
    float param_2 = c.z;
    return vec3(ToLinear1(param), ToLinear1(param_1), ToLinear1(param_2));
}

vec3 Fetch(inout vec2 pos, vec2 off)
{
    pos = (floor((pos * LIBRA_UNIFORM_SourceSize.xy) + off) + vec2(0.5)) / LIBRA_UNIFORM_SourceSize.xy;
    vec3 param = texture(LIBRA_TEXTURE_Source, pos).xyz;
    return ToLinear(param);
}

vec2 Dist(inout vec2 pos)
{
    pos *= LIBRA_UNIFORM_SourceSize.xy;
    return -((pos - floor(pos)) - vec2(0.5));
}

float Gaus(float pos, float scale)
{
    return exp2(scale * pow(abs(pos), LIBRA_UNIFORM_shape));
}

vec3 Horz5(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 param_1 = vec2(-2.0, off);
    vec3 _178 = Fetch(param, param_1);
    vec3 a = _178;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(-1.0, off);
    vec3 _186 = Fetch(param_2, param_3);
    vec3 b = _186;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(0.0, off);
    vec3 _193 = Fetch(param_4, param_5);
    vec3 c = _193;
    vec2 param_6 = pos;
    vec2 param_7 = vec2(1.0, off);
    vec3 _201 = Fetch(param_6, param_7);
    vec3 d = _201;
    vec2 param_8 = pos;
    vec2 param_9 = vec2(2.0, off);
    vec3 _209 = Fetch(param_8, param_9);
    vec3 e = _209;
    vec2 param_10 = pos;
    vec2 _213 = Dist(param_10);
    float dst = _213.x;
    float scale = LIBRA_UNIFORM_hardPix;
    float param_11 = dst - 2.0;
    float param_12 = scale;
    float wa = Gaus(param_11, param_12);
    float param_13 = dst - 1.0;
    float param_14 = scale;
    float wb = Gaus(param_13, param_14);
    float param_15 = dst + 0.0;
    float param_16 = scale;
    float wc = Gaus(param_15, param_16);
    float param_17 = dst + 1.0;
    float param_18 = scale;
    float wd = Gaus(param_17, param_18);
    float param_19 = dst + 2.0;
    float param_20 = scale;
    float we = Gaus(param_19, param_20);
    return (((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) + (e * we)) / vec3((((wa + wb) + wc) + wd) + we);
}

vec3 Horz7(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 param_1 = vec2(-3.0, off);
    vec3 _293 = Fetch(param, param_1);
    vec3 a = _293;
    vec2 param_2 = pos;
    vec2 param_3 = vec2(-2.0, off);
    vec3 _300 = Fetch(param_2, param_3);
    vec3 b = _300;
    vec2 param_4 = pos;
    vec2 param_5 = vec2(-1.0, off);
    vec3 _307 = Fetch(param_4, param_5);
    vec3 c = _307;
    vec2 param_6 = pos;
    vec2 param_7 = vec2(0.0, off);
    vec3 _314 = Fetch(param_6, param_7);
    vec3 d = _314;
    vec2 param_8 = pos;
    vec2 param_9 = vec2(1.0, off);
    vec3 _321 = Fetch(param_8, param_9);
    vec3 e = _321;
    vec2 param_10 = pos;
    vec2 param_11 = vec2(2.0, off);
    vec3 _328 = Fetch(param_10, param_11);
    vec3 f = _328;
    vec2 param_12 = pos;
    vec2 param_13 = vec2(3.0, off);
    vec3 _336 = Fetch(param_12, param_13);
    vec3 g = _336;
    vec2 param_14 = pos;
    vec2 _340 = Dist(param_14);
    float dst = _340.x;
    float scale = LIBRA_UNIFORM_hardBloomPix;
    float param_15 = dst - 3.0;
    float param_16 = scale;
    float wa = Gaus(param_15, param_16);
    float param_17 = dst - 2.0;
    float param_18 = scale;
    float wb = Gaus(param_17, param_18);
    float param_19 = dst - 1.0;
    float param_20 = scale;
    float wc = Gaus(param_19, param_20);
    float param_21 = dst + 0.0;
    float param_22 = scale;
    float wd = Gaus(param_21, param_22);
    float param_23 = dst + 1.0;
    float param_24 = scale;
    float we = Gaus(param_23, param_24);
    float param_25 = dst + 2.0;
    float param_26 = scale;
    float wf = Gaus(param_25, param_26);
    float param_27 = dst + 3.0;
    float param_28 = scale;
    float wg = Gaus(param_27, param_28);
    return (((((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) + (e * we)) + (f * wf)) + (g * wg)) / vec3((((((wa + wb) + wc) + wd) + we) + wf) + wg);
}

float BloomScan(vec2 pos, float off)
{
    vec2 param = pos;
    vec2 _442 = Dist(param);
    float dst = _442.y;
    float param_1 = dst + off;
    float param_2 = LIBRA_UNIFORM_hardBloomScan;
    return Gaus(param_1, param_2);
}

vec3 Bloom(vec2 pos)
{
    vec2 param = pos;
    float param_1 = -2.0;
    vec3 a = Horz5(param, param_1);
    vec2 param_2 = pos;
    float param_3 = -1.0;
    vec3 b = Horz7(param_2, param_3);
    vec2 param_4 = pos;
    float param_5 = 0.0;
    vec3 c = Horz7(param_4, param_5);
    vec2 param_6 = pos;
    float param_7 = 1.0;
    vec3 d = Horz7(param_6, param_7);
    vec2 param_8 = pos;
    float param_9 = 2.0;
    vec3 e = Horz5(param_8, param_9);
    vec2 param_10 = pos;
    float param_11 = -2.0;
    float wa = BloomScan(param_10, param_11);
    vec2 param_12 = pos;
    float param_13 = -1.0;
    float wb = BloomScan(param_12, param_13);
    vec2 param_14 = pos;
    float param_15 = 0.0;
    float wc = BloomScan(param_14, param_15);
    vec2 param_16 = pos;
    float param_17 = 1.0;
    float wd = BloomScan(param_16, param_17);
    vec2 param_18 = pos;
    float param_19 = 2.0;
    float we = BloomScan(param_18, param_19);
    return ((((a * wa) + (b * wb)) + (c * wc)) + (d * wd)) + (e * we);
}

void main()
{
    vec2 pos = LIBRA_VARYING_0;
    vec2 param = pos;
    FragColor = vec4(Bloom(param) * LIBRA_UNIFORM_bloomAmount, 1.0);
}
