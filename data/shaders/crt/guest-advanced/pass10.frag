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
uniform float LIBRA_UNIFORM_smart_ei;
uniform float LIBRA_UNIFORM_ei_limit;
uniform float LIBRA_UNIFORM_sth;
uniform float LIBRA_UNIFORM_overscanX;
uniform float LIBRA_UNIFORM_overscanY;
uniform float LIBRA_UNIFORM_VShift;
uniform float LIBRA_UNIFORM_c_shape;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_prescalex;
uniform float LIBRA_UNIFORM_scan_falloff;
uniform float LIBRA_UNIFORM_bloom_dist;
uniform float LIBRA_UNIFORM_scangamma;
uniform float LIBRA_UNIFORM_bmask1;
uniform float LIBRA_UNIFORM_hmask1;
uniform float LIBRA_UNIFORM_interm;

uniform float LIBRA_UNIFORM_TATE;
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
uniform float LIBRA_UNIFORM_csize;
uniform float LIBRA_UNIFORM_bsize1;
uniform float LIBRA_UNIFORM_warpX;
uniform float LIBRA_UNIFORM_warpY;
uniform float LIBRA_UNIFORM_glow;
uniform float LIBRA_UNIFORM_spike;
uniform float LIBRA_UNIFORM_ring;
uniform float LIBRA_UNIFORM_no_scanlines;
uniform float LIBRA_UNIFORM_tds;
uniform float LIBRA_UNIFORM_clips;
uniform float LIBRA_UNIFORM_hiscan;
uniform float LIBRA_UNIFORM_rolling_scan;

uniform sampler2D LIBRA_TEXTURE_LinearizePass;
uniform sampler2D LIBRA_TEXTURE_AvgLumPass;
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
    return (c * mg) / vec3(mc + 1.0000000133514319600180897396058e-10);
}

void main()
{
    vec2 prescalex = vec2(textureSize(LIBRA_TEXTURE_LinearizePass, 0)) / LIBRA_UNIFORM_OriginalSize.xy;
    vec4 SourceSize = LIBRA_UNIFORM_OriginalSize * mix(vec4(prescalex.x, 1.0, 1.0 / prescalex.x, 1.0), vec4(1.0, prescalex.y, 1.0, 1.0 / prescalex.y), vec4(LIBRA_UNIFORM_TATE));
    float lum = texture(LIBRA_TEXTURE_AvgLumPass, vec2(0.5)).w;
    float gamma_in = 1.0 / texture(LIBRA_TEXTURE_LinearizePass, vec2(0.25)).w;
    float intera = texture(LIBRA_TEXTURE_LinearizePass, vec2(0.75, 0.25)).w;
    bool hscan = LIBRA_UNIFORM_hiscan > 0.5;
    bool _402 = intera < 0.3499999940395355224609375;
    bool _409;
    if (_402)
    {
        _409 = LIBRA_UNIFORM_interm == 6.0;
    }
    else
    {
        _409 = _402;
    }
    bool inter6 = _409;
    bool _415 = (intera < 0.3499999940395355224609375) && (!inter6);
    bool _424;
    if (!_415)
    {
        _424 = LIBRA_UNIFORM_no_scanlines > 0.02500000037252902984619140625;
    }
    else
    {
        _424 = _415;
    }
    bool interb = _424 && (!hscan);
    bool notate = LIBRA_UNIFORM_TATE < 0.5;
    bool _437 = abs(intera - 0.5) < 0.0500000007450580596923828125;
    bool _443;
    if (_437)
    {
        _443 = LIBRA_UNIFORM_no_scanlines == 0.0;
    }
    else
    {
        _443 = _437;
    }
    bool vgascan = _443;
    float SourceY = mix(SourceSize.y, SourceSize.x, LIBRA_UNIFORM_TATE);
    float sy = 1.0;
    if (LIBRA_UNIFORM_intres == 1.0)
    {
        sy = max(round(SourceY / 224.0), 1.0);
    }
    bool _465 = LIBRA_UNIFORM_intres > 0.25;
    bool _471;
    if (_465)
    {
        _471 = LIBRA_UNIFORM_intres != 1.0;
    }
    else
    {
        _471 = _465;
    }
    if (_471)
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
    if (notate)
    {
        SourceSize *= vec4(1.0, 1.0 / sy, 1.0, sy);
    }
    else
    {
        SourceSize *= vec4(1.0 / sy, 1.0, sy, 1.0);
    }
    vec2 texcoord = LIBRA_VARYING_0;
    if ((LIBRA_UNIFORM_IOS > 0.0) && (!interb))
    {
        vec2 ofactor = LIBRA_UNIFORM_OutputSize.xy / LIBRA_UNIFORM_OriginalSize.xy;
        vec2 _533;
        if (LIBRA_UNIFORM_IOS < 2.5)
        {
            _533 = floor(ofactor);
        }
        else
        {
            _533 = ceil(ofactor);
        }
        vec2 intfactor = _533;
        vec2 diff = ofactor / intfactor;
        float scan = mix(diff.y, diff.x, LIBRA_UNIFORM_TATE);
        vec2 param = texcoord;
        float param_1 = scan;
        float param_2 = scan;
        vec2 _560 = Overscan(param, param_1, param_2);
        texcoord = _560;
        bool _563 = LIBRA_UNIFORM_IOS == 1.0;
        bool _571;
        if (!_563)
        {
            _571 = LIBRA_UNIFORM_IOS == 3.0;
        }
        else
        {
            _571 = _563;
        }
        if (_571)
        {
            texcoord = mix(vec2(LIBRA_VARYING_0.x, texcoord.y), vec2(texcoord.x, LIBRA_VARYING_0.y), vec2(LIBRA_UNIFORM_TATE));
        }
    }
    float factor = (1.0 + (((1.0 - (0.5 * LIBRA_UNIFORM_OS)) * LIBRA_UNIFORM_BLOOM) / 100.0)) - ((lum * LIBRA_UNIFORM_BLOOM) / 100.0);
    vec2 param_3 = texcoord;
    float param_4 = factor;
    float param_5 = factor;
    vec2 _612 = Overscan(param_3, param_4, param_5);
    texcoord = _612;
    texcoord.y -= (LIBRA_UNIFORM_VShift * LIBRA_UNIFORM_OriginalSize.w);
    vec2 param_6 = texcoord;
    float param_7 = (LIBRA_UNIFORM_OriginalSize.x - LIBRA_UNIFORM_overscanX) / LIBRA_UNIFORM_OriginalSize.x;
    float param_8 = (LIBRA_UNIFORM_OriginalSize.y - LIBRA_UNIFORM_overscanY) / LIBRA_UNIFORM_OriginalSize.y;
    vec2 _645 = Overscan(param_6, param_7, param_8);
    texcoord = _645;
    vec2 param_9 = texcoord;
    vec2 _649 = Warp(param_9);
    vec2 pos = _649;
    bool smarte = (LIBRA_UNIFORM_smart_ei > 0.00999999977648258209228515625) && notate;
    float ii = float(inter6) * floor(mod(float(LIBRA_UNIFORM_FrameCount), 2.0));
    vec2 coffset = vec2(0.5, 0.5 + (0.5 * ii));
    vec2 ps = SourceSize.zw;
    vec2 OGL2Pos = (pos * SourceSize.xy) - coffset;
    vec2 fp = fract(OGL2Pos);
    vec2 dx = vec2(ps.x, 0.0);
    vec2 dy = vec2(0.0, ps.y);
    vec2 x2 = dx * 2.0;
    vec2 y2 = dy * 2.0;
    vec2 offx = dx;
    vec2 off2 = x2;
    vec2 offy = dy;
    float fpx = fp.x;
    if (!notate)
    {
        offx = dy;
        off2 = y2;
        offy = dx;
        fpx = fp.y;
    }
    float _720;
    if (notate)
    {
        _720 = fp.y;
    }
    else
    {
        _720 = fp.x;
    }
    float f = _720;
    vec2 pC4 = (floor(OGL2Pos) * ps) + (ps * 0.5);
    pC4.y += ((float(inter6) * 0.25) * dy.y);
    bool _751 = (LIBRA_UNIFORM_intres == 0.5) && notate;
    bool _757;
    if (_751)
    {
        _757 = prescalex.y < 1.5;
    }
    else
    {
        _757 = _751;
    }
    if (_757 || vgascan)
    {
        pC4.y = (floor(pC4.y * LIBRA_UNIFORM_OriginalSize.y) * LIBRA_UNIFORM_OriginalSize.w) + (0.5 * LIBRA_UNIFORM_OriginalSize.w);
    }
    bool _781 = (LIBRA_UNIFORM_intres == 0.5) && (!notate);
    bool _787;
    if (_781)
    {
        _787 = prescalex.x < 1.5;
    }
    else
    {
        _787 = _781;
    }
    bool _795;
    if (!_787)
    {
        _795 = vgascan && (!notate);
    }
    else
    {
        _795 = _787;
    }
    if (_795)
    {
        pC4.x = (floor(pC4.x * LIBRA_UNIFORM_OriginalSize.x) * LIBRA_UNIFORM_OriginalSize.z) + (0.5 * LIBRA_UNIFORM_OriginalSize.z);
    }
    bool _818;
    if (interb)
    {
        _818 = LIBRA_UNIFORM_no_scanlines < 0.02500000037252902984619140625;
    }
    else
    {
        _818 = interb;
    }
    if (_818 && (!hscan))
    {
        pC4.y = pos.y;
    }
    else
    {
        if (interb)
        {
            pC4.y += (smoothstep(0.4000000059604644775390625 - (0.5 * LIBRA_UNIFORM_no_scanlines), 0.60000002384185791015625 + (0.5 * LIBRA_UNIFORM_no_scanlines), f) * mix(SourceSize.w, SourceSize.z, LIBRA_UNIFORM_TATE));
        }
    }
    if (hscan)
    {
        pC4 = mix(vec2(pC4.x, pos.y), vec2(pos.x, pC4.y), vec2(LIBRA_UNIFORM_TATE));
    }
    float zero = exp2(-LIBRA_UNIFORM_h_sharp);
    float sharp1 = LIBRA_UNIFORM_s_sharp * zero;
    float idiv = clamp(mix(SourceSize.x, SourceSize.y, LIBRA_UNIFORM_TATE) / 400.0, 1.0, 2.0);
    float fdivider = max(min(mix(prescalex.x, prescalex.y, LIBRA_UNIFORM_TATE), 2.0), idiv * float(interb));
    fdivider = 1.0 / max(fdivider, 1.0);
    float wl3 = (2.0 + fpx) * fdivider;
    float wl2 = (1.0 + fpx) * fdivider;
    float wl1 = fpx * fdivider;
    float wr1 = (1.0 - fpx) * fdivider;
    float wr2 = (2.0 - fpx) * fdivider;
    float wr3 = (3.0 - fpx) * fdivider;
    wl3 *= wl3;
    wl3 = exp2((-LIBRA_UNIFORM_h_sharp) * wl3);
    wl2 *= wl2;
    wl2 = exp2((-LIBRA_UNIFORM_h_sharp) * wl2);
    wl1 *= wl1;
    wl1 = exp2((-LIBRA_UNIFORM_h_sharp) * wl1);
    wr1 *= wr1;
    wr1 = exp2((-LIBRA_UNIFORM_h_sharp) * wr1);
    wr2 *= wr2;
    wr2 = exp2((-LIBRA_UNIFORM_h_sharp) * wr2);
    wr3 *= wr3;
    wr3 = exp2((-LIBRA_UNIFORM_h_sharp) * wr3);
    float fp1 = 1.0 - fpx;
    float twl3 = max(wl3 - sharp1, 0.0);
    float twl2 = max(wl2 - sharp1, mix(-0.119999997317790985107421875, 0.0, 1.0 - (fp1 * fp1)));
    float twl1 = max(wl1 - sharp1, -0.119999997317790985107421875);
    float twr1 = max(wr1 - sharp1, -0.119999997317790985107421875);
    float twr2 = max(wr2 - sharp1, mix(-0.119999997317790985107421875, 0.0, 1.0 - (fpx * fpx)));
    float twr3 = max(wr3 - sharp1, 0.0);
    bool sharp = sharp1 > 0.0;
    vec3 c1;
    vec3 c2;
    if (smarte)
    {
        twl3 = 0.0;
        twr3 = 0.0;
        c1 = texture(LIBRA_TEXTURE_AvgLumPass, pC4).xyz;
        c2 = texture(LIBRA_TEXTURE_AvgLumPass, pC4 + offy).xyz;
        c1 = max(c1 - vec3(LIBRA_UNIFORM_sth), vec3(0.0));
        c2 = max(c2 - vec3(LIBRA_UNIFORM_sth), vec3(0.0));
    }
    vec3 l3 = texture(LIBRA_TEXTURE_LinearizePass, pC4 - off2).xyz;
    vec3 l2 = texture(LIBRA_TEXTURE_LinearizePass, pC4 - offx).xyz;
    vec3 l1 = texture(LIBRA_TEXTURE_LinearizePass, pC4).xyz;
    vec3 r1 = texture(LIBRA_TEXTURE_LinearizePass, pC4 + offx).xyz;
    vec3 r2 = texture(LIBRA_TEXTURE_LinearizePass, pC4 + off2).xyz;
    vec3 r3 = texture(LIBRA_TEXTURE_LinearizePass, (pC4 + offx) + off2).xyz;
    vec3 colmin = min(min(l1, r1), min(l2, r2));
    vec3 colmax = max(max(l1, r1), max(l2, r2));
    if (smarte)
    {
        float pc = min(LIBRA_UNIFORM_smart_ei * c1.y, LIBRA_UNIFORM_ei_limit);
        float pl = min(LIBRA_UNIFORM_smart_ei * max(c1.y, c1.x), LIBRA_UNIFORM_ei_limit);
        float pr = min(LIBRA_UNIFORM_smart_ei * max(c1.y, c1.z), LIBRA_UNIFORM_ei_limit);
        twl1 = max(wl1 - pc, 0.00999999977648258209228515625 * wl1);
        twr1 = max(wr1 - pc, 0.00999999977648258209228515625 * wr1);
        twl2 = max(wl2 - pl, 0.00999999977648258209228515625 * wl2);
        twr2 = max(wr2 - pr, 0.00999999977648258209228515625 * wr2);
    }
    vec3 color1 = ((((((l3 * twl3) + (l2 * twl2)) + (l1 * twl1)) + (r1 * twr1)) + (r2 * twr2)) + (r3 * twr3)) / vec3(((((twl3 + twl2) + twl1) + twr1) + twr2) + twr3);
    if (sharp)
    {
        color1 = clamp(mix(clamp(color1, colmin, colmax), color1, vec3(LIBRA_UNIFORM_ring)), vec3(0.0), vec3(1.0));
    }
    float ts = 0.02500000037252902984619140625;
    vec3 luma = vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875);
    float lm2 = max(max(l2.x, l2.y), l2.z);
    float lm1 = max(max(l1.x, l1.y), l1.z);
    float rm1 = max(max(r1.x, r1.y), r1.z);
    float rm2 = max(max(r2.x, r2.y), r2.z);
    float swl2 = max(twl2, 0.0) * (dot(l2, luma) + ts);
    float swl1 = max(twl1, 0.0) * (dot(l1, luma) + ts);
    float swr1 = max(twr1, 0.0) * (dot(r1, luma) + ts);
    float swr2 = max(twr2, 0.0) * (dot(r2, luma) + ts);
    float fscolor1 = ((((lm2 * swl2) + (lm1 * swl1)) + (rm1 * swr1)) + (rm2 * swr2)) / (((swl2 + swl1) + swr1) + swr2);
    vec3 scolor1 = vec3(clamp(mix(max(max(color1.x, color1.y), color1.z), fscolor1, LIBRA_UNIFORM_spike), 0.0, 1.0));
    if (!interb)
    {
        color1 = pow(color1, vec3(LIBRA_UNIFORM_scangamma / gamma_in));
    }
    vec3 color2;
    vec3 scolor2;
    if ((!interb) && (!hscan))
    {
        pC4 += offy;
        bool _1384 = (LIBRA_UNIFORM_intres == 0.5) && notate;
        bool _1390;
        if (_1384)
        {
            _1390 = prescalex.y < 1.5;
        }
        else
        {
            _1390 = _1384;
        }
        if (_1390 || vgascan)
        {
            pC4.y = (floor((pos.y + (0.3300000131130218505859375 * offy.y)) * LIBRA_UNIFORM_OriginalSize.y) * LIBRA_UNIFORM_OriginalSize.w) + (0.5 * LIBRA_UNIFORM_OriginalSize.w);
        }
        bool _1419 = (LIBRA_UNIFORM_intres == 0.5) && (!notate);
        bool _1425;
        if (_1419)
        {
            _1425 = prescalex.x < 1.5;
        }
        else
        {
            _1425 = _1419;
        }
        bool _1433;
        if (!_1425)
        {
            _1433 = vgascan && (!notate);
        }
        else
        {
            _1433 = _1425;
        }
        if (_1433)
        {
            pC4.x = (floor((pos.x + (0.3300000131130218505859375 * offy.x)) * LIBRA_UNIFORM_OriginalSize.x) * LIBRA_UNIFORM_OriginalSize.z) + (0.5 * LIBRA_UNIFORM_OriginalSize.z);
        }
        l3 = texture(LIBRA_TEXTURE_LinearizePass, pC4 - off2).xyz;
        l2 = texture(LIBRA_TEXTURE_LinearizePass, pC4 - offx).xyz;
        l1 = texture(LIBRA_TEXTURE_LinearizePass, pC4).xyz;
        r1 = texture(LIBRA_TEXTURE_LinearizePass, pC4 + offx).xyz;
        r2 = texture(LIBRA_TEXTURE_LinearizePass, pC4 + off2).xyz;
        r3 = texture(LIBRA_TEXTURE_LinearizePass, (pC4 + offx) + off2).xyz;
        colmin = min(min(l1, r1), min(l2, r2));
        colmax = max(max(l1, r1), max(l2, r2));
        if (smarte)
        {
            float pc_1 = min(LIBRA_UNIFORM_smart_ei * c2.y, LIBRA_UNIFORM_ei_limit);
            float pl_1 = min(LIBRA_UNIFORM_smart_ei * max(c2.y, c2.x), LIBRA_UNIFORM_ei_limit);
            float pr_1 = min(LIBRA_UNIFORM_smart_ei * max(c2.y, c2.z), LIBRA_UNIFORM_ei_limit);
            twl1 = max(wl1 - pc_1, 0.00999999977648258209228515625 * wl1);
            twr1 = max(wr1 - pc_1, 0.00999999977648258209228515625 * wr1);
            twl2 = max(wl2 - pl_1, 0.00999999977648258209228515625 * wl2);
            twr2 = max(wr2 - pr_1, 0.00999999977648258209228515625 * wr2);
        }
        color2 = ((((((l3 * twl3) + (l2 * twl2)) + (l1 * twl1)) + (r1 * twr1)) + (r2 * twr2)) + (r3 * twr3)) / vec3(((((twl3 + twl2) + twl1) + twr1) + twr2) + twr3);
        if (sharp)
        {
            color2 = clamp(mix(clamp(color2, colmin, colmax), color2, vec3(LIBRA_UNIFORM_ring)), vec3(0.0), vec3(1.0));
        }
        lm2 = max(max(l2.x, l2.y), l2.z);
        lm1 = max(max(l1.x, l1.y), l1.z);
        rm1 = max(max(r1.x, r1.y), r1.z);
        rm2 = max(max(r2.x, r2.y), r2.z);
        swl2 = max(twl2, 0.0) * (dot(l2, luma) + ts);
        swl1 = max(twl1, 0.0) * (dot(l1, luma) + ts);
        swr1 = max(twr1, 0.0) * (dot(r1, luma) + ts);
        swr2 = max(twr2, 0.0) * (dot(r2, luma) + ts);
        float fscolor2 = ((((lm2 * swl2) + (lm1 * swl1)) + (rm1 * swr1)) + (rm2 * swr2)) / (((swl2 + swl1) + swr1) + swr2);
        scolor2 = vec3(clamp(mix(max(max(color2.x, color2.y), color2.z), fscolor2, LIBRA_UNIFORM_spike), 0.0, 1.0));
        color2 = pow(color2, vec3(LIBRA_UNIFORM_scangamma / gamma_in));
    }
    vec3 ctmp = color1;
    vec3 sctmp = color1;
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
        float param_10 = f;
        float wt1 = st(param_10);
        float param_11 = 1.0 - f;
        float wt2 = st(param_11);
        vec3 color00 = (color1 * wt1) + (color2 * wt2);
        vec3 scolor0 = (scolor1 * wt1) + (scolor2 * wt2);
        ctmp = color00 / vec3(wt1 + wt2);
        sctmp = scolor0 / vec3(wt1 + wt2);
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
        float scanpix = mix(LIBRA_UNIFORM_OriginalSize.x / LIBRA_UNIFORM_OutputSize.x, LIBRA_UNIFORM_OriginalSize.y / LIBRA_UNIFORM_OutputSize.y, float(notate));
        float f1 = fract(f - ((LIBRA_UNIFORM_rolling_scan * float(LIBRA_UNIFORM_FrameCount)) * scanpix));
        float f2 = 1.0 - f1;
        float mc1 = max(max(color1.x, color1.y), color1.z) + 1.0000000133514319600180897396058e-10;
        float mc2 = max(max(color2.x, color2.y), color2.z) + 1.0000000133514319600180897396058e-10;
        cref1 = color1 / vec3(mc1);
        cref2 = color2 / vec3(mc2);
        vec3 w1;
        vec3 w2;
        if (LIBRA_UNIFORM_gsl < 0.5)
        {
            float param_12 = f1;
            float param_13 = creff1;
            float param_14 = shape1;
            vec3 param_15 = cref1;
            w1 = sw0(param_12, param_13, param_14, param_15);
            float param_16 = f2;
            float param_17 = creff2;
            float param_18 = shape2;
            vec3 param_19 = cref2;
            w2 = sw0(param_16, param_17, param_18, param_19);
        }
        else
        {
            if (LIBRA_UNIFORM_gsl == 1.0)
            {
                float param_20 = f1;
                float param_21 = creff1;
                float param_22 = shape1;
                vec3 param_23 = cref1;
                vec3 _1957 = sw1(param_20, param_21, param_22, param_23);
                w1 = _1957;
                float param_24 = f2;
                float param_25 = creff2;
                float param_26 = shape2;
                vec3 param_27 = cref2;
                vec3 _1966 = sw1(param_24, param_25, param_26, param_27);
                w2 = _1966;
            }
            else
            {
                float param_28 = f1;
                float param_29 = creff1;
                float param_30 = shape1;
                vec3 param_31 = cref1;
                w1 = sw2(param_28, param_29, param_30, param_31);
                float param_32 = f2;
                float param_33 = creff2;
                float param_34 = shape2;
                vec3 param_35 = cref2;
                w2 = sw2(param_32, param_33, param_34, param_35);
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
            bvec3 _2024 = bvec3(LIBRA_UNIFORM_clips > 0.0);
            one = vec3(_2024.x ? w1.x : vec3(1.0).x, _2024.y ? w1.y : vec3(1.0).y, _2024.z ? w1.z : vec3(1.0).z);
            float sat = 1.00010001659393310546875 - min(min(cref1.x, cref1.y), cref1.z);
            vec3 param_36 = pow(color1, vec3(0.699999988079071044921875) - vec3(0.324999988079071044921875 * sat));
            float param_37 = sy;
            color1 = mix(color1, plant(param_36, param_37), (one * pow(sat, 0.33329999446868896484375)) * abs(LIBRA_UNIFORM_clips));
            sy = mc2;
            bvec3 _2066 = bvec3(LIBRA_UNIFORM_clips > 0.0);
            one = vec3(_2066.x ? w2.x : vec3(1.0).x, _2066.y ? w2.y : vec3(1.0).y, _2066.z ? w2.z : vec3(1.0).z);
            sat = 1.00010001659393310546875 - min(min(cref2.x, cref2.y), cref2.z);
            vec3 param_38 = pow(color2, vec3(0.699999988079071044921875) - vec3(0.324999988079071044921875 * sat));
            float param_39 = sy;
            color2 = mix(color2, plant(param_38, param_39), (one * pow(sat, 0.33329999446868896484375)) * abs(LIBRA_UNIFORM_clips));
        }
        vec3 param_40 = color1;
        vec3 param_41 = color2;
        color = (gc(param_40) * w1) + (gc(param_41) * w2);
        color = min(color, vec3(1.0));
    }
    if (interb)
    {
        vec3 param_42 = color1;
        color = gc(param_42);
    }
    float colmx = max(max(ctmp.x, ctmp.y), ctmp.z);
    if (!interb)
    {
        color = pow(color, vec3(gamma_in / LIBRA_UNIFORM_scangamma));
    }
    FragColor = vec4(color, colmx);
}
