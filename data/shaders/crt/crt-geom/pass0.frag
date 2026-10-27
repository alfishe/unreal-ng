#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_CRTgamma;
uniform float LIBRA_UNIFORM_monitorgamma;
uniform float LIBRA_UNIFORM_d;
uniform float LIBRA_UNIFORM_R;
uniform float LIBRA_UNIFORM_cornersize;
uniform float LIBRA_UNIFORM_cornersmooth;
uniform float LIBRA_UNIFORM_x_tilt;
uniform float LIBRA_UNIFORM_y_tilt;
uniform float LIBRA_UNIFORM_overscan_x;
uniform float LIBRA_UNIFORM_overscan_y;
uniform float LIBRA_UNIFORM_mask_type;
uniform float LIBRA_UNIFORM_DOTMASK;
uniform float LIBRA_UNIFORM_DOTMASK_brightboost;
uniform float LIBRA_UNIFORM_SHARPER;
uniform float LIBRA_UNIFORM_scanline_weight;
uniform float LIBRA_UNIFORM_CURVATURE;
uniform float LIBRA_UNIFORM_interlace_detect;
uniform float LIBRA_UNIFORM_lum;
uniform float LIBRA_UNIFORM_invert_aspect;
uniform float LIBRA_UNIFORM_vertical_scanlines;
uniform float LIBRA_UNIFORM_xsize;
uniform float LIBRA_UNIFORM_ysize;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_2;
in vec3 LIBRA_VARYING_3;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_4;
in vec2 LIBRA_VARYING_7;
in vec2 LIBRA_VARYING_5;
layout(location = 0) out vec4 FragColor;
vec2 height;
vec2 width;
vec4 SourceSize;
vec2 aspect;
vec2 overscan;

float intersect(vec2 xy)
{
    float A = dot(xy, xy) + (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d);
    float B;
    float C;
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        B = 2.0 * ((LIBRA_UNIFORM_R * (dot(xy, LIBRA_VARYING_1) - ((LIBRA_UNIFORM_d * LIBRA_VARYING_2.x) * LIBRA_VARYING_2.y))) - (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d));
        C = (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d) + ((((2.0 * LIBRA_UNIFORM_R) * LIBRA_UNIFORM_d) * LIBRA_VARYING_2.x) * LIBRA_VARYING_2.y);
    }
    else
    {
        B = 2.0 * ((LIBRA_UNIFORM_R * (dot(xy, LIBRA_VARYING_1) - ((LIBRA_UNIFORM_d * LIBRA_VARYING_2.y) * LIBRA_VARYING_2.x))) - (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d));
        C = (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d) + ((((2.0 * LIBRA_UNIFORM_R) * LIBRA_UNIFORM_d) * LIBRA_VARYING_2.y) * LIBRA_VARYING_2.x);
    }
    return ((-B) - sqrt((B * B) - ((4.0 * A) * C))) / (2.0 * A);
}

vec2 bkwtrans(vec2 xy)
{
    vec2 param = xy;
    float c = intersect(param);
    vec2 point = ((vec2(c) * xy) - (vec2(-LIBRA_UNIFORM_R, -LIBRA_UNIFORM_R) * LIBRA_VARYING_1)) / vec2(LIBRA_UNIFORM_R, LIBRA_UNIFORM_R);
    vec2 poc = point / LIBRA_VARYING_2;
    vec2 tang = LIBRA_VARYING_1 / LIBRA_VARYING_2;
    float A = dot(tang, tang) + 1.0;
    float B = (-2.0) * dot(poc, tang);
    float C = dot(poc, poc) - 1.0;
    float a = ((-B) + sqrt((B * B) - ((4.0 * A) * C))) / (2.0 * A);
    vec2 uv = (point - (LIBRA_VARYING_1 * a)) / LIBRA_VARYING_2;
    float r = max(abs(LIBRA_UNIFORM_R * acos(a)), 9.9999997473787516355514526367188e-06);
    return (uv * r) / vec2(sin(r / LIBRA_UNIFORM_R));
}

vec2 transform(inout vec2 coord)
{
    coord = (((coord - vec2(0.5)) * aspect) * LIBRA_VARYING_3.z) + LIBRA_VARYING_3.xy;
    vec2 param = coord;
    return ((bkwtrans(param) / vec2(LIBRA_UNIFORM_overscan_x / 100.0, LIBRA_UNIFORM_overscan_y / 100.0)) / aspect) + vec2(0.5);
}

float corner(inout vec2 coord)
{
    coord = ((coord - vec2(0.5)) * vec2(LIBRA_UNIFORM_overscan_x / 100.0, LIBRA_UNIFORM_overscan_y / 100.0)) + vec2(0.5);
    coord = min(coord, vec2(1.0) - coord) * aspect;
    vec2 cdist = vec2(LIBRA_UNIFORM_cornersize);
    coord = cdist - min(coord, cdist);
    float dist = sqrt(dot(coord, coord));
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        return clamp((cdist.x - dist) * LIBRA_UNIFORM_cornersmooth, 0.0, 1.0);
    }
    else
    {
        return clamp((cdist.y - dist) * LIBRA_UNIFORM_cornersmooth, 0.0, 1.0);
    }
}

vec4 scanlineWeights(float _distance, vec4 color)
{
    vec4 wid = vec4(2.0) + (pow(color, vec4(4.0)) * 2.0);
    vec4 weights = vec4(_distance / LIBRA_UNIFORM_scanline_weight);
    return (exp(-pow(weights * inversesqrt(wid * 0.5), wid)) * (LIBRA_UNIFORM_lum + 1.39999997615814208984375)) / (vec4(0.60000002384185791015625) + (wid * 0.20000000298023223876953125));
}

vec3 mask_weights_alpha(vec2 coord, float mask_intensity, int phosphor_layout, out float alpha)
{
    vec3 weights = vec3(1.0);
    float on = 1.0;
    float off = 1.0 - mask_intensity;
    vec3 red = vec3(on, off, off);
    vec3 green = vec3(off, on, off);
    vec3 blue = vec3(off, off, on);
    vec3 magenta = vec3(on, off, on);
    vec3 yellow = vec3(on, on, off);
    vec3 cyan = vec3(off, on, on);
    vec3 black = vec3(off);
    vec3 white = vec3(on);
    int z = 0;
    alpha = 1.0;
    vec3 aperture_weights = mix(magenta, green, vec3(floor(mod(coord.x, 2.0))));
    if (phosphor_layout == 0)
    {
        return weights;
    }
    else
    {
        if (phosphor_layout == 1)
        {
            weights = aperture_weights;
            alpha = 0.5;
            return weights;
        }
        else
        {
            if (phosphor_layout == 2)
            {
                vec3 inverse_aperture = mix(green, magenta, vec3(floor(mod(coord.x, 2.0))));
                weights = mix(aperture_weights, inverse_aperture, vec3(floor(mod(coord.y, 2.0))));
                alpha = 0.5;
                return weights;
            }
            else
            {
                int w;
                if (phosphor_layout == 3)
                {
                    vec3 slotmask[12] = vec3[](magenta, green, black, black, magenta, green, magenta, green, black, black, magenta, green);
                    w = int(floor(mod(coord.y, 3.0)));
                    z = int(floor(mod(coord.x, 4.0)));
                    weights = slotmask[(int(w)) * 4 + int(z)];
                    alpha = 0.3333333432674407958984375;
                    return weights;
                }
                else
                {
                    if (phosphor_layout == 4)
                    {
                        weights = mix(yellow, blue, vec3(floor(mod(coord.x, 2.0))));
                        alpha = 0.5;
                        return weights;
                    }
                    else
                    {
                        if (phosphor_layout == 5)
                        {
                            vec3 inverse_aperture_1 = mix(blue, yellow, vec3(floor(mod(coord.x, 2.0))));
                            weights = mix(mix(yellow, blue, vec3(floor(mod(coord.x, 2.0)))), inverse_aperture_1, vec3(floor(mod(coord.y, 2.0))));
                            alpha = 0.5;
                            return weights;
                        }
                        else
                        {
                            if (phosphor_layout == 6)
                            {
                                vec3 ap4[4] = vec3[](red, green, blue, black);
                                z = int(floor(mod(coord.x, 4.0)));
                                weights = ap4[z];
                                alpha = 0.25;
                                return weights;
                            }
                            else
                            {
                                if (phosphor_layout == 7)
                                {
                                    vec3 ap3[5] = vec3[](red, magenta, blue, green, green);
                                    z = int(floor(mod(coord.x, 5.0)));
                                    weights = ap3[z];
                                    alpha = 0.4000000059604644775390625;
                                    return weights;
                                }
                                else
                                {
                                    if (phosphor_layout == 8)
                                    {
                                        vec3 big_ap[7] = vec3[](red, red, yellow, green, cyan, blue, blue);
                                        w = int(floor(mod(coord.x, 7.0)));
                                        weights = big_ap[w];
                                        alpha = 0.4444444477558135986328125;
                                        return weights;
                                    }
                                    else
                                    {
                                        if (phosphor_layout == 9)
                                        {
                                            vec3 big_ap_rgb[4] = vec3[](red, yellow, cyan, blue);
                                            w = int(floor(mod(coord.x, 4.0)));
                                            weights = big_ap_rgb[w];
                                            alpha = 0.5;
                                            return weights;
                                        }
                                        else
                                        {
                                            if (phosphor_layout == 10)
                                            {
                                                vec3 big_ap_rbg[4] = vec3[](red, magenta, cyan, green);
                                                w = int(floor(mod(coord.x, 4.0)));
                                                weights = big_ap_rbg[w];
                                                alpha = 0.5;
                                                return weights;
                                            }
                                            else
                                            {
                                                if (phosphor_layout == 11)
                                                {
                                                    vec3 delta1[8] = vec3[](red, green, blue, black, blue, black, red, green);
                                                    w = int(floor(mod(coord.y, 2.0)));
                                                    z = int(floor(mod(coord.x, 4.0)));
                                                    weights = delta1[(int(w)) * 4 + int(z)];
                                                    alpha = 0.25;
                                                    return weights;
                                                }
                                                else
                                                {
                                                    if (phosphor_layout == 12)
                                                    {
                                                        vec3 delta[8] = vec3[](red, yellow, cyan, blue, cyan, blue, red, yellow);
                                                        w = int(floor(mod(coord.y, 2.0)));
                                                        z = int(floor(mod(coord.x, 4.0)));
                                                        weights = delta[(int(w)) * 4 + int(z)];
                                                        alpha = 0.5;
                                                        return weights;
                                                    }
                                                    else
                                                    {
                                                        if (phosphor_layout == 13)
                                                        {
                                                            vec3 delta_1[16] = vec3[](red, yellow, cyan, blue, red, yellow, cyan, blue, cyan, blue, red, yellow, cyan, blue, red, yellow);
                                                            w = int(floor(mod(coord.y, 4.0)));
                                                            z = int(floor(mod(coord.x, 4.0)));
                                                            weights = delta_1[(int(w)) * 4 + int(z)];
                                                            alpha = 0.5;
                                                            return weights;
                                                        }
                                                        else
                                                        {
                                                            if (phosphor_layout == 14)
                                                            {
                                                                vec3 slotmask_1[18] = vec3[](magenta, green, black, black, black, black, magenta, green, black, magenta, green, black, black, black, black, magenta, green, black);
                                                                w = int(floor(mod(coord.y, 3.0)));
                                                                z = int(floor(mod(coord.x, 6.0)));
                                                                weights = slotmask_1[(int(w)) * 6 + int(z)];
                                                                alpha = 0.22222222387790679931640625;
                                                                return weights;
                                                            }
                                                            else
                                                            {
                                                                if (phosphor_layout == 15)
                                                                {
                                                                    vec3 slot2[32] = vec3[](red, yellow, cyan, blue, red, yellow, cyan, blue, red, yellow, cyan, blue, black, black, black, black, red, yellow, cyan, blue, red, yellow, cyan, blue, black, black, black, black, red, yellow, cyan, blue);
                                                                    w = int(floor(mod(coord.y, 4.0)));
                                                                    z = int(floor(mod(coord.x, 8.0)));
                                                                    weights = slot2[(int(w)) * 8 + int(z)];
                                                                    alpha = 0.375;
                                                                    return weights;
                                                                }
                                                                else
                                                                {
                                                                    if (phosphor_layout == 16)
                                                                    {
                                                                        vec3 slotmask_2[12] = vec3[](yellow, blue, black, black, yellow, blue, yellow, blue, black, black, yellow, blue);
                                                                        w = int(floor(mod(coord.y, 3.0)));
                                                                        z = int(floor(mod(coord.x, 4.0)));
                                                                        weights = slotmask_2[(int(w)) * 4 + int(z)];
                                                                        alpha = 0.388888895511627197265625;
                                                                        return weights;
                                                                    }
                                                                    else
                                                                    {
                                                                        if (phosphor_layout == 17)
                                                                        {
                                                                            vec3 slot2_1[40] = vec3[](red, magenta, blue, green, green, red, magenta, blue, green, green, black, blue, blue, green, green, red, red, black, black, black, red, magenta, blue, green, green, red, magenta, blue, green, green, red, red, black, black, black, black, blue, blue, green, green);
                                                                            w = int(floor(mod(coord.y, 4.0)));
                                                                            z = int(floor(mod(coord.x, 10.0)));
                                                                            weights = slot2_1[(int(w)) * 10 + int(z)];
                                                                            alpha = 0.300000011920928955078125;
                                                                            return weights;
                                                                        }
                                                                        else
                                                                        {
                                                                            if (phosphor_layout == 18)
                                                                            {
                                                                                vec3 slot2_2[40] = vec3[](red, yellow, green, blue, blue, red, yellow, green, blue, blue, black, green, green, blue, blue, red, red, black, black, black, red, yellow, green, blue, blue, red, yellow, green, blue, blue, red, red, black, black, black, black, green, green, blue, blue);
                                                                                w = int(floor(mod(coord.y, 4.0)));
                                                                                z = int(floor(mod(coord.x, 10.0)));
                                                                                weights = slot2_2[(int(w)) * 10 + int(z)];
                                                                                alpha = 0.300000011920928955078125;
                                                                                return weights;
                                                                            }
                                                                            else
                                                                            {
                                                                                if (phosphor_layout == 19)
                                                                                {
                                                                                    vec3 slot[84] = vec3[](red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, black, black, black, black, black, black, black, red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, red, red, yellow, green, cyan, blue, blue, black, black, black, black, black, black, black, black, red, red, yellow, green, cyan, blue);
                                                                                    w = int(floor(mod(coord.y, 6.0)));
                                                                                    z = int(floor(mod(coord.x, 14.0)));
                                                                                    weights = slot[(int(w)) * 14 + int(z)];
                                                                                    alpha = 0.3531745970249176025390625;
                                                                                    return weights;
                                                                                }
                                                                                else
                                                                                {
                                                                                    if (phosphor_layout == 20)
                                                                                    {
                                                                                        vec3 tatemask[16] = vec3[](green, magenta, green, magenta, black, blue, green, red, green, magenta, green, magenta, green, red, black, blue);
                                                                                        w = int(floor(mod(coord.y, 4.0)));
                                                                                        z = int(floor(mod(coord.x, 4.0)));
                                                                                        weights = tatemask[(int(w)) * 4 + int(z)];
                                                                                        alpha = 0.375;
                                                                                        return weights;
                                                                                    }
                                                                                    else
                                                                                    {
                                                                                        if (phosphor_layout == 21)
                                                                                        {
                                                                                            vec3 slot_1[32] = vec3[](red, green, blue, black, red, green, blue, black, red, green, blue, black, black, black, black, black, red, green, blue, black, red, green, blue, black, black, black, black, black, red, green, blue, black);
                                                                                            w = int(floor(mod(coord.y, 4.0)));
                                                                                            z = int(floor(mod(coord.x, 8.0)));
                                                                                            weights = slot_1[(int(w)) * 8 + int(z)];
                                                                                            alpha = 0.21875;
                                                                                            return weights;
                                                                                        }
                                                                                        else
                                                                                        {
                                                                                            if (phosphor_layout == 22)
                                                                                            {
                                                                                                vec3 bw3[3] = vec3[](black, white, white);
                                                                                                z = int(floor(mod(coord.x, 3.0)));
                                                                                                weights = bw3[z];
                                                                                                alpha = 0.666666686534881591796875;
                                                                                                return weights;
                                                                                            }
                                                                                            else
                                                                                            {
                                                                                                if (phosphor_layout == 23)
                                                                                                {
                                                                                                    vec3 bw4[4] = vec3[](black, black, white, white);
                                                                                                    z = int(floor(mod(coord.x, 4.0)));
                                                                                                    weights = bw4[z];
                                                                                                    alpha = 0.5;
                                                                                                    return weights;
                                                                                                }
                                                                                                else
                                                                                                {
                                                                                                    if (phosphor_layout == 24)
                                                                                                    {
                                                                                                        vec3 shadow[60] = vec3[](green, cyan, blue, blue, blue, red, red, red, yellow, green, green, cyan, blue, blue, blue, red, red, red, yellow, green, green, cyan, blue, blue, blue, red, red, red, yellow, green, red, red, red, yellow, green, green, cyan, blue, blue, blue, red, red, red, yellow, green, green, cyan, blue, blue, blue, red, red, red, yellow, green, green, cyan, blue, blue, blue);
                                                                                                        w = int(floor(mod(coord.y, 6.0)));
                                                                                                        z = int(floor(mod(coord.x, 10.0)));
                                                                                                        weights = shadow[(int(w)) * 10 + int(z)];
                                                                                                        alpha = 0.4000000059604644775390625;
                                                                                                        return weights;
                                                                                                    }
                                                                                                    else
                                                                                                    {
                                                                                                        return weights;
                                                                                                    }
                                                                                                }
                                                                                            }
                                                                                        }
                                                                                    }
                                                                                }
                                                                            }
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void main()
{
    vec2 _54;
    if (LIBRA_UNIFORM_ysize > 0.001000000047497451305389404296875)
    {
        _54 = vec2(LIBRA_UNIFORM_ysize, 1.0 / LIBRA_UNIFORM_ysize);
    }
    else
    {
        _54 = LIBRA_UNIFORM_SourceSize.yw;
    }
    height = _54;
    vec2 _80;
    if (LIBRA_UNIFORM_xsize > 0.001000000047497451305389404296875)
    {
        _80 = vec2(LIBRA_UNIFORM_xsize, 1.0 / LIBRA_UNIFORM_xsize);
    }
    else
    {
        _80 = LIBRA_UNIFORM_SourceSize.xz;
    }
    width = _80;
    SourceSize = vec4(width.x, height.x, width.y, height.y);
    float _114;
    if (LIBRA_UNIFORM_invert_aspect > 0.5)
    {
        _114 = 1.0;
    }
    else
    {
        _114 = 0.75;
    }
    aspect = vec2(_114);
    overscan = vec2(1.0099999904632568359375);
    vec2 xy;
    if (LIBRA_UNIFORM_CURVATURE > 0.5)
    {
        vec2 param = LIBRA_VARYING_0;
        vec2 _1445 = transform(param);
        xy = _1445;
    }
    else
    {
        xy = LIBRA_VARYING_0;
    }
    vec2 param_1 = xy;
    float _1451 = corner(param_1);
    float cval = _1451;
    vec2 ilvec;
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        float _1466;
        if ((LIBRA_VARYING_4.y * LIBRA_UNIFORM_interlace_detect) > 1.5)
        {
            _1466 = mod(float(LIBRA_UNIFORM_FrameCount), 2.0);
        }
        else
        {
            _1466 = 0.0;
        }
        ilvec = vec2(0.0, _1466);
    }
    else
    {
        float _1484;
        if ((LIBRA_VARYING_4.x * LIBRA_UNIFORM_interlace_detect) > 1.5)
        {
            _1484 = mod(float(LIBRA_UNIFORM_FrameCount), 2.0);
        }
        else
        {
            _1484 = 0.0;
        }
        ilvec = vec2(_1484, 0.0);
    }
    vec2 ratio_scale = (((xy * LIBRA_VARYING_7) - vec2(0.5)) + ilvec) / LIBRA_VARYING_4;
    vec2 uv_ratio = fract(ratio_scale);
    xy = (((floor(ratio_scale) * LIBRA_VARYING_4) + vec2(0.5)) - ilvec) / LIBRA_VARYING_7;
    vec4 coeffs;
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        coeffs = vec4(1.0 + uv_ratio.x, uv_ratio.x, 1.0 - uv_ratio.x, 2.0 - uv_ratio.x) * 3.1415927410125732421875;
    }
    else
    {
        coeffs = vec4(1.0 + uv_ratio.y, uv_ratio.y, 1.0 - uv_ratio.y, 2.0 - uv_ratio.y) * 3.1415927410125732421875;
    }
    coeffs = max(abs(coeffs), vec4(9.9999997473787516355514526367188e-06));
    coeffs = ((sin(coeffs) * 2.0) * sin(coeffs / vec4(2.0))) / (coeffs * coeffs);
    coeffs /= vec4(dot(coeffs, vec4(1.0)));
    vec4 col;
    vec4 col2;
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        col = clamp(mat4(vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(-LIBRA_VARYING_5.x, 0.0)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(LIBRA_VARYING_5.x, 0.0)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(2.0 * LIBRA_VARYING_5.x, 0.0)), vec4(LIBRA_UNIFORM_CRTgamma)))) * coeffs, vec4(0.0), vec4(1.0));
        vec4 _1722 = mat4(vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(-LIBRA_VARYING_5.x, LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(0.0, LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + LIBRA_VARYING_5), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(2.0 * LIBRA_VARYING_5.x, LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma)))) * coeffs;
        col2 = clamp(_1722, vec4(0.0), vec4(1.0));
    }
    else
    {
        col = clamp(mat4(vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(0.0, -LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(0.0, LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(0.0, 2.0 * LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma)))) * coeffs, vec4(0.0), vec4(1.0));
        vec4 _1865 = mat4(vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(LIBRA_VARYING_5.x, -LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(LIBRA_VARYING_5.x, 0.0)), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + LIBRA_VARYING_5), vec4(LIBRA_UNIFORM_CRTgamma))), vec4(pow(texture(LIBRA_TEXTURE_Source, xy + vec2(LIBRA_VARYING_5.x, 2.0 * LIBRA_VARYING_5.y)), vec4(LIBRA_UNIFORM_CRTgamma)))) * coeffs;
        col2 = clamp(_1865, vec4(0.0), vec4(1.0));
    }
    vec4 weights;
    vec4 weights2;
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        float param_2 = uv_ratio.y;
        vec4 param_3 = col;
        weights = scanlineWeights(param_2, param_3);
        float param_4 = 1.0 - uv_ratio.y;
        vec4 param_5 = col2;
        weights2 = scanlineWeights(param_4, param_5);
        float filter_ = fwidth(ratio_scale.y);
        uv_ratio.y += (0.3333333432674407958984375 * filter_);
        float param_6 = uv_ratio.y;
        vec4 param_7 = col;
        weights = (weights + scanlineWeights(param_6, param_7)) / vec4(3.0);
        float param_8 = abs(1.0 - uv_ratio.y);
        vec4 param_9 = col2;
        weights2 = (weights2 + scanlineWeights(param_8, param_9)) / vec4(3.0);
        uv_ratio.y -= (0.666666686534881591796875 * filter_);
        float param_10 = abs(uv_ratio.y);
        vec4 param_11 = col;
        weights += (scanlineWeights(param_10, param_11) / vec4(3.0));
        float param_12 = abs(1.0 - uv_ratio.y);
        vec4 param_13 = col2;
        weights2 += (scanlineWeights(param_12, param_13) / vec4(3.0));
    }
    else
    {
        float param_14 = uv_ratio.x;
        vec4 param_15 = col;
        weights = scanlineWeights(param_14, param_15);
        float param_16 = 1.0 - uv_ratio.x;
        vec4 param_17 = col2;
        weights2 = scanlineWeights(param_16, param_17);
        float filter_1 = fwidth(ratio_scale.x);
        uv_ratio.x += (0.3333333432674407958984375 * filter_1);
        float param_18 = uv_ratio.x;
        vec4 param_19 = col;
        weights = (weights + scanlineWeights(param_18, param_19)) / vec4(3.0);
        float param_20 = abs(1.0 - uv_ratio.x);
        vec4 param_21 = col2;
        weights2 = (weights2 + scanlineWeights(param_20, param_21)) / vec4(3.0);
        uv_ratio.x -= (0.666666686534881591796875 * filter_1);
        float param_22 = abs(uv_ratio.x);
        vec4 param_23 = col;
        weights += (scanlineWeights(param_22, param_23) / vec4(3.0));
        float param_24 = abs(1.0 - uv_ratio.x);
        vec4 param_25 = col2;
        weights2 += (scanlineWeights(param_24, param_25) / vec4(3.0));
    }
    vec3 mul_res = ((col * weights) + (col2 * weights2)).xyz * vec3(cval);
    xy = fract((LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy) / (LIBRA_UNIFORM_OutputSize.xy * LIBRA_UNIFORM_SourceSize.zw));
    vec4 mask = vec4(1.0);
    float alpha;
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        vec2 param_26 = LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy;
        float param_27 = 1.0;
        int param_28 = int(LIBRA_UNIFORM_mask_type);
        float param_29;
        vec3 _2070 = mask_weights_alpha(param_26, param_27, param_28, param_29);
        alpha = param_29;
        mask = vec4(_2070, 1.0);
    }
    else
    {
        vec2 param_30 = LIBRA_VARYING_0.yx * LIBRA_UNIFORM_OutputSize.yx;
        float param_31 = 1.0;
        int param_32 = int(LIBRA_UNIFORM_mask_type);
        float param_33;
        vec3 _2090 = mask_weights_alpha(param_30, param_31, param_32, param_33);
        alpha = param_33;
        mask = vec4(_2090, 1.0);
    }
    mask.w = alpha;
    float nbright = 255.0 - (255.0 * mask.w);
    float fbright = nbright / ((LIBRA_UNIFORM_OutputSize.xy * LIBRA_UNIFORM_SourceSize.zw).x * (LIBRA_UNIFORM_OutputSize.xy * LIBRA_UNIFORM_SourceSize.zw).y);
    float aperture_average = mix(1.0 - (LIBRA_UNIFORM_DOTMASK * (1.0 - LIBRA_UNIFORM_DOTMASK_brightboost)), 1.0, fbright);
    vec3 clow = (vec3(1.0 - LIBRA_UNIFORM_DOTMASK) * mul_res) + ((vec3(LIBRA_UNIFORM_DOTMASK * LIBRA_UNIFORM_DOTMASK_brightboost) * mul_res) * mul_res);
    float ifbright = 1.0 / fbright;
    vec3 chi = (vec3(ifbright * aperture_average) * mul_res) - (vec3(ifbright - 1.0) * clow);
    vec3 cout = mix(clow, chi, mask.xyz);
    mul_res = pow(cout, vec3(1.0 / LIBRA_UNIFORM_monitorgamma));
    FragColor = vec4(mul_res, 1.0);
}
