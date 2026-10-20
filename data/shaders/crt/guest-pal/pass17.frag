#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_bloom;
uniform float LIBRA_UNIFORM_halation;
uniform float LIBRA_UNIFORM_scans;
uniform float LIBRA_UNIFORM_gamma_c;
uniform float LIBRA_UNIFORM_gamma_c2;
uniform float LIBRA_UNIFORM_gamma_out;
uniform float LIBRA_UNIFORM_overscanX;
uniform float LIBRA_UNIFORM_overscanY;
uniform float LIBRA_UNIFORM_VShift;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_prescalex;
uniform float LIBRA_UNIFORM_c_shape;
uniform float LIBRA_UNIFORM_scangamma;
uniform float LIBRA_UNIFORM_sborder;
uniform float LIBRA_UNIFORM_scan_falloff;
uniform float LIBRA_UNIFORM_bloom_dist;
uniform float LIBRA_UNIFORM_bmask1;
uniform float LIBRA_UNIFORM_hmask1;
uniform float LIBRA_UNIFORM_interm;

uniform float LIBRA_UNIFORM_IOS;
uniform float LIBRA_UNIFORM_OS;
uniform float LIBRA_UNIFORM_BLOOM;
uniform float LIBRA_UNIFORM_brightboost;
uniform float LIBRA_UNIFORM_brightboost1;
uniform float LIBRA_UNIFORM_gsl;
uniform float LIBRA_UNIFORM_scanline1;
uniform float LIBRA_UNIFORM_scanline2;
uniform float LIBRA_UNIFORM_beam_min;
uniform float LIBRA_UNIFORM_beam_max;
uniform float LIBRA_UNIFORM_beam_size;
uniform float LIBRA_UNIFORM_h_sharp;
uniform float LIBRA_UNIFORM_s_sharp;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_shadowMask;
uniform float LIBRA_UNIFORM_masksize;
uniform float LIBRA_UNIFORM_ring;
uniform float LIBRA_UNIFORM_no_scanlines;
uniform float LIBRA_UNIFORM_tds;
uniform float LIBRA_UNIFORM_clips;
uniform float LIBRA_UNIFORM_hiscan;
uniform float LIBRA_UNIFORM_rolling_scan;

uniform sampler2D LIBRA_TEXTURE_LinearizePass;
uniform sampler2D LIBRA_TEXTURE_AvgLumPass;
uniform sampler2D LIBRA_TEXTURE_Pass1;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 Overscan(inout vec2 pos, float dx, float dy)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos *= vec2(dx, dy);
    return (pos * 0.5) + vec2(0.5);
}

vec2 Warp(inout vec2 pos)
{
    pos = (pos * 2.0) - vec2(1.0);
    pos = mix(pos, vec2(pos.x * inversesqrt(1.0 - ((LIBRA_UNIFORM_c_shape * pos.y) * pos.y)), pos.y * inversesqrt(1.0 - ((LIBRA_UNIFORM_c_shape * pos.x) * pos.x))), vec2(LIBRA_UNIFORM_warpX, LIBRA_UNIFORM_warpY) / vec2(LIBRA_UNIFORM_c_shape));
    return (pos * 0.5) + vec2(0.5);
}

float st(float x)
{
    return exp2(((-10.0) * x) * x);
}

vec3 sw0(float x, float color, float scanline, vec3 c)
{
    float tmp = mix(LIBRA_UNIFORM_beam_min, LIBRA_UNIFORM_beam_max, color);
    vec3 sat = mix(vec3(1.0) + vec3(1.5 * LIBRA_UNIFORM_scans), vec3(1.0), c);
    float ex = x * tmp;
    float _105;
    if (LIBRA_UNIFORM_gsl > (-0.5))
    {
        _105 = ex * ex;
    }
    else
    {
        _105 = mix(ex * ex, (ex * ex) * ex, 0.4000000059604644775390625);
    }
    ex = _105;
    return exp2(sat * ((-scanline) * ex));
}

vec3 sw1(inout float x, float color, float scanline, vec3 c)
{
    x = mix(x, LIBRA_UNIFORM_beam_min * x, max(x - (0.4000000059604644775390625 * color), 0.0));
    vec3 sat = mix(vec3(1.0) + vec3(1.5 * LIBRA_UNIFORM_scans), vec3(1.0), c);
    float tmp = mix(1.2000000476837158203125 * LIBRA_UNIFORM_beam_min, LIBRA_UNIFORM_beam_max, color);
    float ex = x * tmp;
    return exp2(sat * (((-scanline) * ex) * ex));
}

vec3 sw2(float x, float color, float scanline, vec3 c)
{
    float tmp = mix((2.5 - (0.5 * color)) * LIBRA_UNIFORM_beam_min, LIBRA_UNIFORM_beam_max, color);
    vec3 sat = mix(vec3(1.0) + vec3(1.5 * LIBRA_UNIFORM_scans), vec3(1.0), c);
    tmp = mix(LIBRA_UNIFORM_beam_max, tmp, pow(x, color + 0.300000011920928955078125));
    float ex = x * tmp;
    return exp2(sat * (((-scanline) * ex) * ex));
}

vec3 plant(vec3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06;
    return (tar * r) / vec3(t);
}

vec3 gc(vec3 c)
{
    float mc = max(max(c.x, c.y), c.z);
    float mg = pow(mc, 1.0 / LIBRA_UNIFORM_gamma_c);
    return (c * mg) / vec3(mc + 9.9999999392252902907785028219223e-09);
}

void main()
{
    float prescalex = float(textureSize(LIBRA_TEXTURE_LinearizePass, 0).x) / (2.0 * LIBRA_UNIFORM_OriginalSize.x);
    vec4 SourceSize = LIBRA_UNIFORM_OriginalSize * vec4(prescalex, 1.0, 1.0 / prescalex, 1.0);
    SourceSize *= vec4(2.0, 1.0, 0.5, 1.0);
    float lum = texture(LIBRA_TEXTURE_AvgLumPass, vec2(0.5)).w;
    float gamma_in = 1.0 / texture(LIBRA_TEXTURE_LinearizePass, vec2(0.25)).w;
    float intera = texture(LIBRA_TEXTURE_LinearizePass, vec2(0.75, 0.25)).w;
    bool hscan = LIBRA_UNIFORM_hiscan > 0.5;
    bool _394 = intera < 0.3499999940395355224609375;
    bool _402;
    if (_394)
    {
        _402 = LIBRA_UNIFORM_interm == 6.0;
    }
    else
    {
        _402 = _394;
    }
    bool inter6 = _402;
    bool _408 = (intera < 0.3499999940395355224609375) && (!inter6);
    bool _417;
    if (!_408)
    {
        _417 = LIBRA_UNIFORM_no_scanlines > 0.02500000037252902984619140625;
    }
    else
    {
        _417 = _408;
    }
    bool interb = _417 && (!hscan);
    float SourceY = SourceSize.y;
    float sy = 1.0;
    if (LIBRA_UNIFORM_intres == 1.0)
    {
        sy = max(round(SourceY / 224.0), 1.0);
    }
    bool _437 = LIBRA_UNIFORM_intres > 0.25;
    bool _443;
    if (_437)
    {
        _443 = LIBRA_UNIFORM_intres != 1.0;
    }
    else
    {
        _443 = _437;
    }
    if (_443)
    {
        sy = LIBRA_UNIFORM_intres;
    }
    if (inter6)
    {
        sy *= 2.0;
    }
    SourceSize *= vec4(1.0, 1.0 / sy, 1.0, sy);
    vec2 texcoord = LIBRA_VARYING_0;
    if ((LIBRA_UNIFORM_IOS > 0.0) && (!interb))
    {
        vec2 ofactor = LIBRA_UNIFORM_OutputSize.xy / LIBRA_UNIFORM_OriginalSize.xy;
        vec2 _484;
        if (LIBRA_UNIFORM_IOS < 2.5)
        {
            _484 = floor(ofactor);
        }
        else
        {
            _484 = ceil(ofactor);
        }
        vec2 intfactor = _484;
        vec2 diff = ofactor / intfactor;
        float scan = diff.y;
        vec2 param = texcoord;
        float param_1 = scan;
        float param_2 = scan;
        vec2 _506 = Overscan(param, param_1, param_2);
        texcoord = _506;
        bool _509 = LIBRA_UNIFORM_IOS == 1.0;
        bool _517;
        if (!_509)
        {
            _517 = LIBRA_UNIFORM_IOS == 3.0;
        }
        else
        {
            _517 = _509;
        }
        if (_517)
        {
            texcoord = vec2(LIBRA_VARYING_0.x, texcoord.y);
        }
    }
    float factor = (1.0 + (((1.0 - (0.5 * LIBRA_UNIFORM_OS)) * LIBRA_UNIFORM_BLOOM) / 100.0)) - ((lum * LIBRA_UNIFORM_BLOOM) / 100.0);
    vec2 param_3 = texcoord;
    float param_4 = factor;
    float param_5 = factor;
    vec2 _550 = Overscan(param_3, param_4, param_5);
    texcoord = _550;
    texcoord.y -= (LIBRA_UNIFORM_VShift * LIBRA_UNIFORM_OriginalSize.w);
    vec2 param_6 = texcoord;
    float param_7 = (LIBRA_UNIFORM_OriginalSize.x - LIBRA_UNIFORM_overscanX) / LIBRA_UNIFORM_OriginalSize.x;
    float param_8 = (LIBRA_UNIFORM_OriginalSize.y - LIBRA_UNIFORM_overscanY) / LIBRA_UNIFORM_OriginalSize.y;
    vec2 _582 = Overscan(param_6, param_7, param_8);
    texcoord = _582;
    vec2 param_9 = texcoord;
    vec2 _586 = Warp(param_9);
    vec2 pos = _586;
    vec2 param_10 = LIBRA_VARYING_0;
    vec2 _590 = Warp(param_10);
    vec2 pos0 = _590;
    float ii = float(inter6) * floor(mod(float(LIBRA_UNIFORM_FrameCount), 2.0));
    vec2 coffset = vec2(0.5, 0.5 + (0.5 * ii));
    vec2 ps = SourceSize.zw;
    vec2 OGL2Pos = (pos * SourceSize.xy) - coffset;
    vec2 fp = fract(OGL2Pos);
    vec2 dx = vec2(ps.x, 0.0);
    vec2 dy = vec2(0.0, ps.y);
    float f = fp.y;
    vec2 pC4 = (floor(OGL2Pos) * ps) + (ps * 0.5);
    pC4.y += ((float(inter6) * 0.25) * dy.y);
    pC4.x = pos.x;
    if ((LIBRA_UNIFORM_intres == 0.5) && (prescalex < 1.5))
    {
        pC4.y = (floor(pC4.y * LIBRA_UNIFORM_OriginalSize.y) * LIBRA_UNIFORM_OriginalSize.w) + (0.5 * LIBRA_UNIFORM_OriginalSize.w);
    }
    bool _680;
    if (interb)
    {
        _680 = LIBRA_UNIFORM_no_scanlines < 0.02500000037252902984619140625;
    }
    else
    {
        _680 = interb;
    }
    if (_680 || hscan)
    {
        pC4.y = pos.y;
    }
    else
    {
        if (interb)
        {
            pC4.y += (smoothstep(0.4000000059604644775390625 - (0.5 * LIBRA_UNIFORM_no_scanlines), 0.60000002384185791015625 + (0.5 * LIBRA_UNIFORM_no_scanlines), f) * SourceSize.w);
        }
    }
    vec3 color1 = texture(LIBRA_TEXTURE_Pass1, pC4).xyz;
    vec3 scolor1 = texture(LIBRA_TEXTURE_Pass1, pC4).www;
    if (!interb)
    {
        color1 = pow(color1, vec3(LIBRA_UNIFORM_scangamma / gamma_in));
    }
    pC4 += dy;
    if ((LIBRA_UNIFORM_intres == 0.5) && (prescalex < 1.5))
    {
        pC4.y = (floor((pos.y + (0.3300000131130218505859375 * dy.y)) * LIBRA_UNIFORM_OriginalSize.y) * LIBRA_UNIFORM_OriginalSize.w) + (0.5 * LIBRA_UNIFORM_OriginalSize.w);
    }
    vec3 color2 = texture(LIBRA_TEXTURE_Pass1, pC4).xyz;
    vec3 scolor2 = texture(LIBRA_TEXTURE_Pass1, pC4).www;
    if (!interb)
    {
        color2 = pow(color2, vec3(LIBRA_UNIFORM_scangamma / gamma_in));
    }
    vec3 ctmp = color1;
    float w3 = 1.0;
    vec3 color = color1;
    vec3 one = vec3(1.0);
    if (hscan)
    {
        color2 = color1;
        scolor2 = scolor1;
    }
    if ((!interb) || hscan)
    {
        float shape1 = mix(LIBRA_UNIFORM_scanline1, LIBRA_UNIFORM_scanline2, f);
        float shape2 = mix(LIBRA_UNIFORM_scanline1, LIBRA_UNIFORM_scanline2, 1.0 - f);
        float param_11 = f;
        float wt1 = st(param_11);
        float param_12 = 1.0 - f;
        float wt2 = st(param_12);
        vec3 color00 = (color1 * wt1) + (color2 * wt2);
        vec3 scolor0 = (scolor1 * wt1) + (scolor2 * wt2);
        ctmp = color00 / vec3(wt1 + wt2);
        vec3 sctmp = scolor0 / vec3(wt1 + wt2);
        if (abs(LIBRA_UNIFORM_rolling_scan) > 0.004999999888241291046142578125)
        {
            color1 = ctmp;
            color2 = ctmp;
            scolor1 = sctmp;
            scolor2 = sctmp;
        }
        vec3 cref1 = mix(sctmp, scolor1, vec3(LIBRA_UNIFORM_beam_size));
        float creff1 = pow(max(max(cref1.x, cref1.y), cref1.z), LIBRA_UNIFORM_scan_falloff);
        vec3 cref2 = mix(sctmp, scolor2, vec3(LIBRA_UNIFORM_beam_size));
        float creff2 = pow(max(max(cref2.x, cref2.y), cref2.z), LIBRA_UNIFORM_scan_falloff);
        if (LIBRA_UNIFORM_tds > 0.5)
        {
            shape1 = mix(LIBRA_UNIFORM_scanline2, shape1, creff1);
            shape2 = mix(LIBRA_UNIFORM_scanline2, shape2, creff2);
        }
        float scanpix = LIBRA_UNIFORM_OriginalSize.y / LIBRA_UNIFORM_OutputSize.y;
        float f1 = fract(f - ((LIBRA_UNIFORM_rolling_scan * float(LIBRA_UNIFORM_FrameCount)) * scanpix));
        float f2 = 1.0 - f1;
        float mc1 = max(max(color1.x, color1.y), color1.z) + 9.9999999392252902907785028219223e-09;
        float mc2 = max(max(color2.x, color2.y), color2.z) + 9.9999999392252902907785028219223e-09;
        cref1 = color1 / vec3(mc1);
        cref2 = color2 / vec3(mc2);
        vec3 w1;
        vec3 w2;
        if (LIBRA_UNIFORM_gsl < 0.5)
        {
            float param_13 = f1;
            float param_14 = creff1;
            float param_15 = shape1;
            vec3 param_16 = cref1;
            w1 = sw0(param_13, param_14, param_15, param_16);
            float param_17 = f2;
            float param_18 = creff2;
            float param_19 = shape2;
            vec3 param_20 = cref2;
            w2 = sw0(param_17, param_18, param_19, param_20);
        }
        else
        {
            if (LIBRA_UNIFORM_gsl == 1.0)
            {
                float param_21 = f1;
                float param_22 = creff1;
                float param_23 = shape1;
                vec3 param_24 = cref1;
                vec3 _1009 = sw1(param_21, param_22, param_23, param_24);
                w1 = _1009;
                float param_25 = f2;
                float param_26 = creff2;
                float param_27 = shape2;
                vec3 param_28 = cref2;
                vec3 _1018 = sw1(param_25, param_26, param_27, param_28);
                w2 = _1018;
            }
            else
            {
                float param_29 = f1;
                float param_30 = creff1;
                float param_31 = shape1;
                vec3 param_32 = cref1;
                w1 = sw2(param_29, param_30, param_31, param_32);
                float param_33 = f2;
                float param_34 = creff2;
                float param_35 = shape2;
                vec3 param_36 = cref2;
                w2 = sw2(param_33, param_34, param_35, param_36);
            }
        }
        vec3 w3_1 = w1 + w2;
        float wf1 = max(max(w3_1.x, w3_1.y), w3_1.z);
        if (wf1 > 1.0)
        {
            wf1 = 1.0 / wf1;
            w1 *= wf1;
            w2 *= wf1;
        }
        if (abs(LIBRA_UNIFORM_clips) > 0.004999999888241291046142578125)
        {
            sy = mc1;
            bvec3 _1076 = bvec3(LIBRA_UNIFORM_clips > 0.0);
            one = vec3(_1076.x ? w1.x : vec3(1.0).x, _1076.y ? w1.y : vec3(1.0).y, _1076.z ? w1.z : vec3(1.0).z);
            float sat = 1.00010001659393310546875 - min(min(cref1.x, cref1.y), cref1.z);
            vec3 param_37 = pow(color1, vec3(0.699999988079071044921875) - vec3(0.324999988079071044921875 * sat));
            float param_38 = sy;
            color1 = mix(color1, plant(param_37, param_38), (one * pow(sat, 0.33329999446868896484375)) * abs(LIBRA_UNIFORM_clips));
            sy = mc2;
            bvec3 _1118 = bvec3(LIBRA_UNIFORM_clips > 0.0);
            one = vec3(_1118.x ? w2.x : vec3(1.0).x, _1118.y ? w2.y : vec3(1.0).y, _1118.z ? w2.z : vec3(1.0).z);
            sat = 1.00010001659393310546875 - min(min(cref2.x, cref2.y), cref2.z);
            vec3 param_39 = pow(color2, vec3(0.699999988079071044921875) - vec3(0.324999988079071044921875 * sat));
            float param_40 = sy;
            color2 = mix(color2, plant(param_39, param_40), (one * pow(sat, 0.33329999446868896484375)) * abs(LIBRA_UNIFORM_clips));
        }
        vec3 param_41 = color1;
        vec3 param_42 = color2;
        color = (gc(param_41) * w1) + (gc(param_42) * w2);
        color = min(color, vec3(1.0));
    }
    if (interb)
    {
        vec3 param_43 = color1;
        color = gc(param_43);
    }
    float colmx = max(max(ctmp.x, ctmp.y), ctmp.z);
    if (!interb)
    {
        color = pow(color, vec3(gamma_in / LIBRA_UNIFORM_scangamma));
    }
    FragColor = vec4(color, colmx);
}
