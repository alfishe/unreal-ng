#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_addnoise;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_csize;
uniform float LIBRA_UNIFORM_bsize1;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_c_shape;
uniform float LIBRA_UNIFORM_barspeed;
uniform float LIBRA_UNIFORM_barintensity;
uniform float LIBRA_UNIFORM_bardir;
uniform float LIBRA_UNIFORM_sborder;
uniform float LIBRA_UNIFORM_scan_falloff;
uniform float LIBRA_UNIFORM_overscanX;
uniform float LIBRA_UNIFORM_overscanY;
uniform float LIBRA_UNIFORM_VShift;
uniform float LIBRA_UNIFORM_bloom_dist;
uniform float LIBRA_UNIFORM_bmask1;
uniform float LIBRA_UNIFORM_hmask1;
uniform float LIBRA_UNIFORM_interm;

uniform float LIBRA_UNIFORM_brightboost;
uniform float LIBRA_UNIFORM_brightboost1;
uniform float LIBRA_UNIFORM_gsl;
uniform float LIBRA_UNIFORM_scanline1;
uniform float LIBRA_UNIFORM_scanline2;
uniform float LIBRA_UNIFORM_beam_min;
uniform float LIBRA_UNIFORM_beam_max;
uniform float LIBRA_UNIFORM_beam_size;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_maskstr;
uniform float LIBRA_UNIFORM_inters;
uniform float LIBRA_UNIFORM_bloom;
uniform float LIBRA_UNIFORM_halation;
uniform float LIBRA_UNIFORM_scans;
uniform float LIBRA_UNIFORM_gamma_c;
uniform float LIBRA_UNIFORM_gamma_c2;
uniform float LIBRA_UNIFORM_gamma_out;
uniform float LIBRA_UNIFORM_IOS;
uniform float LIBRA_UNIFORM_no_scanlines;
uniform float LIBRA_UNIFORM_tds;
uniform float LIBRA_UNIFORM_clips;
uniform float LIBRA_UNIFORM_hiscan;

uniform sampler2D LIBRA_TEXTURE_LinearizePass;
uniform sampler2D LIBRA_TEXTURE_Pass1;
uniform sampler2D LIBRA_TEXTURE_BloomPass;
uniform sampler2D LIBRA_TEXTURE_PrePass;

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
    float _98;
    if (LIBRA_UNIFORM_gsl > (-0.5))
    {
        _98 = ex * ex;
    }
    else
    {
        _98 = mix(ex * ex, (ex * ex) * ex, 0.4000000059604644775390625);
    }
    ex = _98;
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
    return (c * mg) / vec3(mc + 1.0000000133514319600180897396058e-10);
}

void main()
{
    vec2 prescalex = vec2(textureSize(LIBRA_TEXTURE_LinearizePass, 0)) / LIBRA_UNIFORM_OriginalSize.xy;
    vec4 SourceSize = vec4(LIBRA_UNIFORM_SourceSize.x, LIBRA_UNIFORM_OriginalSize.y, LIBRA_UNIFORM_SourceSize.z, LIBRA_UNIFORM_OriginalSize.w);
    float gamma_in = 1.0 / texture(LIBRA_TEXTURE_LinearizePass, vec2(0.25)).w;
    float intera = texture(LIBRA_TEXTURE_LinearizePass, vec2(0.75, 0.25)).w;
    bool hscan = LIBRA_UNIFORM_hiscan > 0.5;
    bool _386 = intera < 0.3499999940395355224609375;
    bool _394;
    if (_386)
    {
        _394 = LIBRA_UNIFORM_interm == 6.0;
    }
    else
    {
        _394 = _386;
    }
    bool inter6 = _394;
    bool _400 = (intera < 0.3499999940395355224609375) && (!inter6);
    bool _409;
    if (!_400)
    {
        _409 = LIBRA_UNIFORM_no_scanlines > 0.02500000037252902984619140625;
    }
    else
    {
        _409 = _400;
    }
    bool interb = _409 && (!hscan);
    bool _418 = abs(intera - 0.5) < 0.0500000007450580596923828125;
    bool _424;
    if (_418)
    {
        _424 = LIBRA_UNIFORM_no_scanlines == 0.0;
    }
    else
    {
        _424 = _418;
    }
    bool vgascan = _424;
    float SourceY = SourceSize.y;
    float sy = 1.0;
    if (LIBRA_UNIFORM_intres == 1.0)
    {
        sy = max(round(SourceY / 224.0), 1.0);
    }
    bool _442 = LIBRA_UNIFORM_intres > 0.25;
    bool _448;
    if (_442)
    {
        _448 = LIBRA_UNIFORM_intres != 1.0;
    }
    else
    {
        _448 = _442;
    }
    if (_448)
    {
        sy = LIBRA_UNIFORM_intres;
    }
    if (inter6)
    {
        sy *= 2.0;
    }
    if (vgascan)
    {
        sy = 0.5;
    }
    else
    {
        if (abs(intera - 0.75) < 0.0500000007450580596923828125)
        {
            sy = 1.0;
        }
    }
    SourceSize *= vec4(1.0, 1.0 / sy, 1.0, sy);
    vec2 texcoord = LIBRA_VARYING_0;
    if ((LIBRA_UNIFORM_IOS > 0.0) && (!interb))
    {
        vec2 ofactor = LIBRA_UNIFORM_OutputSize.xy / LIBRA_UNIFORM_OriginalSize.xy;
        vec2 _500;
        if (LIBRA_UNIFORM_IOS < 2.5)
        {
            _500 = floor(ofactor);
        }
        else
        {
            _500 = ceil(ofactor);
        }
        vec2 intfactor = _500;
        vec2 diff = ofactor / intfactor;
        float scan = diff.y;
        vec2 param = texcoord;
        float param_1 = scan;
        float param_2 = scan;
        vec2 _522 = Overscan(param, param_1, param_2);
        texcoord = _522;
        bool _525 = LIBRA_UNIFORM_IOS == 1.0;
        bool _533;
        if (!_525)
        {
            _533 = LIBRA_UNIFORM_IOS == 3.0;
        }
        else
        {
            _533 = _525;
        }
        if (_533)
        {
            texcoord = vec2(LIBRA_VARYING_0.x, texcoord.y);
        }
    }
    texcoord.y -= (LIBRA_UNIFORM_VShift * LIBRA_UNIFORM_OriginalSize.w);
    vec2 param_3 = texcoord;
    float param_4 = (LIBRA_UNIFORM_OriginalSize.x - LIBRA_UNIFORM_overscanX) / LIBRA_UNIFORM_OriginalSize.x;
    float param_5 = (LIBRA_UNIFORM_OriginalSize.y - LIBRA_UNIFORM_overscanY) / LIBRA_UNIFORM_OriginalSize.y;
    vec2 _572 = Overscan(param_3, param_4, param_5);
    texcoord = _572;
    vec2 param_6 = texcoord;
    vec2 _576 = Warp(param_6);
    vec2 pos = _576;
    float ii = float(inter6) * floor(mod(float(LIBRA_UNIFORM_FrameCount), 2.0));
    float coffset = 0.5 + (0.5 * ii);
    vec2 ps = SourceSize.zw;
    float OGL2Pos = (pos.y * SourceSize.y) - coffset;
    float f = fract(OGL2Pos);
    vec2 dx = vec2(ps.x, 0.0);
    vec2 dy = vec2(0.0, ps.y);
    vec2 pC4;
    pC4.y = (floor(OGL2Pos) * ps.y) + (0.5 * ps.y);
    pC4.y += ((float(inter6) * 0.25) * dy.y);
    pC4.x = pos.x;
    bool _640 = LIBRA_UNIFORM_intres == 0.5;
    bool _646;
    if (_640)
    {
        _646 = prescalex.y < 1.5;
    }
    else
    {
        _646 = _640;
    }
    if (_646 || vgascan)
    {
        pC4.y = (floor(pC4.y * LIBRA_UNIFORM_OriginalSize.y) * LIBRA_UNIFORM_OriginalSize.w) + (0.5 * LIBRA_UNIFORM_OriginalSize.w);
    }
    bool _671;
    if (interb)
    {
        _671 = LIBRA_UNIFORM_no_scanlines < 0.02500000037252902984619140625;
    }
    else
    {
        _671 = interb;
    }
    if (_671 || hscan)
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
    pC4 += dy;
    bool _717 = LIBRA_UNIFORM_intres == 0.5;
    bool _723;
    if (_717)
    {
        _723 = prescalex.y < 1.5;
    }
    else
    {
        _723 = _717;
    }
    if (_723 || vgascan)
    {
        pC4.y = (floor((pos.y + (0.3300000131130218505859375 * dy.y)) * LIBRA_UNIFORM_OriginalSize.y) * LIBRA_UNIFORM_OriginalSize.w) + (0.5 * LIBRA_UNIFORM_OriginalSize.w);
    }
    vec3 color2 = texture(LIBRA_TEXTURE_Pass1, pC4).xyz;
    vec3 scolor2 = texture(LIBRA_TEXTURE_Pass1, pC4).www;
    vec3 ctmp = color1;
    float w3 = 1.0;
    vec3 color = color1;
    vec3 one = vec3(1.0);
    if (hscan)
    {
        color2 = color1;
        scolor2 = scolor1;
    }
    if (!interb)
    {
        float shape1 = mix(LIBRA_UNIFORM_scanline1, LIBRA_UNIFORM_scanline2, f);
        float shape2 = mix(LIBRA_UNIFORM_scanline1, LIBRA_UNIFORM_scanline2, 1.0 - f);
        float param_7 = f;
        float wt1 = st(param_7);
        float param_8 = 1.0 - f;
        float wt2 = st(param_8);
        vec3 color00 = (color1 * wt1) + (color2 * wt2);
        vec3 scolor0 = (scolor1 * wt1) + (scolor2 * wt2);
        ctmp = color00 / vec3(wt1 + wt2);
        vec3 sctmp = max(scolor0 / vec3(wt1 + wt2), ctmp);
        vec3 cref1 = mix(sctmp, scolor1, vec3(LIBRA_UNIFORM_beam_size));
        float creff1 = pow(max(max(cref1.x, cref1.y), cref1.z), LIBRA_UNIFORM_scan_falloff);
        vec3 cref2 = mix(sctmp, scolor2, vec3(LIBRA_UNIFORM_beam_size));
        float creff2 = pow(max(max(cref2.x, cref2.y), cref2.z), LIBRA_UNIFORM_scan_falloff);
        if (LIBRA_UNIFORM_tds > 0.5)
        {
            shape1 = mix(LIBRA_UNIFORM_scanline2, shape1, creff1);
            shape2 = mix(LIBRA_UNIFORM_scanline2, shape2, creff2);
        }
        float f1 = f;
        float f2 = 1.0 - f;
        float mc1 = max(max(color1.x, color1.y), color1.z) + 1.0000000133514319600180897396058e-10;
        float mc2 = max(max(color2.x, color2.y), color2.z) + 1.0000000133514319600180897396058e-10;
        cref1 = color1 / vec3(mc1);
        cref2 = color2 / vec3(mc2);
        vec3 w1;
        vec3 w2;
        if (LIBRA_UNIFORM_gsl < 0.5)
        {
            float param_9 = f1;
            float param_10 = creff1;
            float param_11 = shape1;
            vec3 param_12 = cref1;
            w1 = sw0(param_9, param_10, param_11, param_12);
            float param_13 = f2;
            float param_14 = creff2;
            float param_15 = shape2;
            vec3 param_16 = cref2;
            w2 = sw0(param_13, param_14, param_15, param_16);
        }
        else
        {
            if (LIBRA_UNIFORM_gsl == 1.0)
            {
                float param_17 = f1;
                float param_18 = creff1;
                float param_19 = shape1;
                vec3 param_20 = cref1;
                vec3 _953 = sw1(param_17, param_18, param_19, param_20);
                w1 = _953;
                float param_21 = f2;
                float param_22 = creff2;
                float param_23 = shape2;
                vec3 param_24 = cref2;
                vec3 _962 = sw1(param_21, param_22, param_23, param_24);
                w2 = _962;
            }
            else
            {
                float param_25 = f1;
                float param_26 = creff1;
                float param_27 = shape1;
                vec3 param_28 = cref1;
                w1 = sw2(param_25, param_26, param_27, param_28);
                float param_29 = f2;
                float param_30 = creff2;
                float param_31 = shape2;
                vec3 param_32 = cref2;
                w2 = sw2(param_29, param_30, param_31, param_32);
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
            bvec3 _1021 = bvec3(LIBRA_UNIFORM_clips > 0.0);
            one = vec3(_1021.x ? w1.x : vec3(1.0).x, _1021.y ? w1.y : vec3(1.0).y, _1021.z ? w1.z : vec3(1.0).z);
            float sat = 1.00010001659393310546875 - min(min(cref1.x, cref1.y), cref1.z);
            vec3 param_33 = pow(color1, vec3(0.699999988079071044921875) - vec3(0.324999988079071044921875 * sat));
            float param_34 = sy;
            color1 = mix(color1, plant(param_33, param_34), (one * pow(sat, 0.33329999446868896484375)) * abs(LIBRA_UNIFORM_clips));
            sy = mc2;
            bvec3 _1063 = bvec3(LIBRA_UNIFORM_clips > 0.0);
            one = vec3(_1063.x ? w2.x : vec3(1.0).x, _1063.y ? w2.y : vec3(1.0).y, _1063.z ? w2.z : vec3(1.0).z);
            sat = 1.00010001659393310546875 - min(min(cref2.x, cref2.y), cref2.z);
            vec3 param_35 = pow(color2, vec3(0.699999988079071044921875) - vec3(0.324999988079071044921875 * sat));
            float param_36 = sy;
            color2 = mix(color2, plant(param_35, param_36), (one * pow(sat, 0.33329999446868896484375)) * abs(LIBRA_UNIFORM_clips));
        }
        vec3 param_37 = color1;
        vec3 param_38 = color2;
        color = (gc(param_37) * w1) + (gc(param_38) * w2);
        color = min(color, vec3(1.0));
    }
    if (interb)
    {
        vec3 param_39 = color1;
        color = gc(param_39);
    }
    float colmx = max(max(ctmp.x, ctmp.y), ctmp.z);
    FragColor = vec4(color, colmx);
}
