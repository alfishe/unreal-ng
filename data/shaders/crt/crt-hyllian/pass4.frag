#version 330

const vec3 _281[3] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0));
const vec3 _299[3] = vec3[](vec3(0.0), vec3(1.0), vec3(0.0));
const vec3 _322[4] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0));
const vec3 _341[4] = vec3[](vec3(0.0), vec3(0.0), vec3(1.0), vec3(1.0));
const vec3 _359[4] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0));
const vec3 _407[8] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0));
const vec3 _433[8] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0));
const vec3 _458[16] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0));
const vec3 _489[24] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0));
const vec3 _517[24] = vec3[](vec3(0.0), vec3(1.0), vec3(0.0), vec3(0.0), vec3(1.0), vec3(0.0), vec3(0.0), vec3(1.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(1.0), vec3(0.0), vec3(0.0), vec3(1.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(1.0), vec3(0.0));
const vec3 _547[32] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0));
const vec3 _577[40] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0));

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform float LIBRA_UNIFORM_GLOW_ENABLE;
uniform float LIBRA_UNIFORM_GLOW_RADIUS;
uniform float LIBRA_UNIFORM_GLOW_STRENGTH;
uniform float LIBRA_UNIFORM_PRESET_OPTION;
uniform float LIBRA_UNIFORM_DISPLAY_RES;
uniform float LIBRA_UNIFORM_PHOSPHOR_LAYOUT;
uniform float LIBRA_UNIFORM_MASK_STRENGTH;
uniform float LIBRA_UNIFORM_BRIGHTBOOST;
uniform float LIBRA_UNIFORM_MONITOR_SUBPIXELS;
uniform float LIBRA_UNIFORM_VSCANLINES;
uniform float LIBRA_UNIFORM_H_OUTPUT_GAMMA;
uniform float LIBRA_UNIFORM_H_MaskGamma;
uniform float LIBRA_UNIFORM_h_curvature;
uniform float LIBRA_UNIFORM_h_shape;
uniform float LIBRA_UNIFORM_h_radius;
uniform float LIBRA_UNIFORM_h_cornersize;
uniform float LIBRA_UNIFORM_h_cornersmooth;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_CRTPass;

in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_1;
layout(location = 0) out vec4 FragColor;
float r2;
vec2 max_size;
vec2 aspect;
float cornersize;
float cornersmooth;
vec2 overscan;

vec2 h_warp(inout vec2 uv)
{
    uv = (uv * 2.0) - vec2(1.0);
    vec2 cylinder = max_size * sqrt((r2 - (uv.x * uv.x)) / (r2 - ((2.0 * uv.x) * uv.x)));
    vec2 sphere = vec2(sqrt((r2 - 1.0) / (r2 - dot(uv, uv))));
    uv *= mix(sphere, cylinder, vec2(LIBRA_UNIFORM_h_shape));
    return (uv * 0.5) + vec2(0.5);
}

float h_corner(vec2 uv)
{
    vec2 d = abs(((uv * 2.0) - vec2(1.0)) * aspect) - (aspect - vec2(cornersize));
    float borderline = (length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0)) - cornersize;
    return smoothstep(cornersmooth, -cornersmooth, borderline);
}

vec3 get_bloom(sampler2D src, vec2 tex, vec2 dxy)
{
    vec3 bloom = vec3(0.0);
    bloom += (texture(src, tex - (dxy * 4.0)).xyz * 0.001234402996487915515899658203125);
    bloom += (texture(src, tex - (dxy * 3.0)).xyz * 0.01430468820035457611083984375);
    bloom += (texture(src, tex - (dxy * 2.0)).xyz * 0.0823177993297576904296875);
    bloom += (texture(src, tex - dxy).xyz * 0.2352355420589447021484375);
    bloom += (texture(src, tex).xyz * 0.3338151276111602783203125);
    bloom += (texture(src, tex + dxy).xyz * 0.2352355420589447021484375);
    bloom += (texture(src, tex + (dxy * 2.0)).xyz * 0.0823177993297576904296875);
    bloom += (texture(src, tex + (dxy * 3.0)).xyz * 0.01430468820035457611083984375);
    bloom += (texture(src, tex + (dxy * 4.0)).xyz * 0.001234402996487915515899658203125);
    return bloom;
}

vec3 Mask(inout vec2 pos, float shadowMask)
{
    vec3 mask = vec3(0.0);
    if (shadowMask == 15.0)
    {
        float line = 1.0;
        float odd = 0.0;
        if (fract(pos.x * 0.16666667163372039794921875) < 0.5)
        {
            odd = 1.0;
        }
        if (fract((pos.y + odd) * 0.5) < 0.5)
        {
            line = 0.0;
        }
        pos.x = fract(pos.x * 0.3333333432674407958984375);
        if (pos.x < 0.333000004291534423828125)
        {
            mask.z = 1.0;
        }
        else
        {
            if (pos.x < 0.66600000858306884765625)
            {
                mask.y = 1.0;
            }
            else
            {
                mask.x = 1.0;
            }
        }
        mask *= line;
    }
    else
    {
        if (shadowMask == 16.0)
        {
            pos.x += (pos.y * 3.0);
            pos.x = fract(pos.x * 0.16666667163372039794921875);
            if (pos.x < 0.333000004291534423828125)
            {
                mask.z = 1.0;
            }
            else
            {
                if (pos.x < 0.66600000858306884765625)
                {
                    mask.y = 1.0;
                }
                else
                {
                    mask.x = 1.0;
                }
            }
        }
        else
        {
            if (shadowMask == 17.0)
            {
                pos = floor(pos * vec2(1.0, 0.5));
                pos.x += (pos.y * 3.0);
                pos.x = fract(pos.x * 0.16666667163372039794921875);
                if (pos.x < 0.333000004291534423828125)
                {
                    mask.z = 1.0;
                }
                else
                {
                    if (pos.x < 0.66600000858306884765625)
                    {
                        mask.y = 1.0;
                    }
                    else
                    {
                        mask.x = 1.0;
                    }
                }
            }
        }
    }
    return mask;
}

vec3 mask_weights(vec2 coord, float phosphor_layout)
{
    if (phosphor_layout > 14.0)
    {
        vec2 param = coord;
        float param_1 = phosphor_layout;
        vec3 _237 = Mask(param, param_1);
        return _237;
    }
    vec3 weights = vec3(1.0);
    int z = 0;
    vec3 aperture_classic = mix(vec3(1.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(floor(mod(coord.x, 2.0))));
    if (phosphor_layout == 0.0)
    {
        return weights;
    }
    else
    {
        if (phosphor_layout == 1.0)
        {
            weights = aperture_classic;
            return weights;
        }
        else
        {
            if (phosphor_layout == 2.0)
            {
                z = int(floor(mod(coord.x, 3.0)));
                weights = _281[z];
                return weights;
            }
            else
            {
                if (phosphor_layout == 3.0)
                {
                    z = int(floor(mod(coord.x, 3.0)));
                    weights = _299[z];
                    return weights;
                }
                else
                {
                    int w;
                    if (phosphor_layout == 4.0)
                    {
                        w = int(floor(mod(coord.x, 4.0)));
                        weights = _322[w];
                        return weights;
                    }
                    else
                    {
                        if (phosphor_layout == 5.0)
                        {
                            z = int(floor(mod(coord.x, 4.0)));
                            weights = _341[z];
                            return weights;
                        }
                        else
                        {
                            if (phosphor_layout == 6.0)
                            {
                                z = int(floor(mod(coord.x, 4.0)));
                                weights = _359[z];
                                return weights;
                            }
                            else
                            {
                                if (phosphor_layout == 7.0)
                                {
                                    vec3 inverse_aperture = mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(floor(mod(coord.x, 2.0))));
                                    weights = mix(aperture_classic, inverse_aperture, vec3(floor(mod(coord.y, 2.0))));
                                    return weights;
                                }
                                else
                                {
                                    if (phosphor_layout == 8.0)
                                    {
                                        w = int(floor(mod(coord.y, 2.0)));
                                        z = int(floor(mod(coord.x, 4.0)));
                                        weights = _407[(int(w)) * 4 + int(z)];
                                        return weights;
                                    }
                                    else
                                    {
                                        if (phosphor_layout == 9.0)
                                        {
                                            w = int(floor(mod(coord.y, 2.0)));
                                            z = int(floor(mod(coord.x, 4.0)));
                                            weights = _433[(int(w)) * 4 + int(z)];
                                            return weights;
                                        }
                                        else
                                        {
                                            if (phosphor_layout == 10.0)
                                            {
                                                w = int(floor(mod(coord.y, 4.0)));
                                                z = int(floor(mod(coord.x, 4.0)));
                                                weights = _458[(int(w)) * 4 + int(z)];
                                                return weights;
                                            }
                                            else
                                            {
                                                if (phosphor_layout == 11.0)
                                                {
                                                    w = int(floor(mod(coord.y, 4.0)));
                                                    z = int(floor(mod(coord.x, 6.0)));
                                                    weights = _489[(int(w)) * 6 + int(z)];
                                                    return weights;
                                                }
                                                else
                                                {
                                                    if (phosphor_layout == 12.0)
                                                    {
                                                        w = int(floor(mod(coord.y, 4.0)));
                                                        z = int(floor(mod(coord.x, 6.0)));
                                                        weights = _517[(int(w)) * 6 + int(z)];
                                                        return weights;
                                                    }
                                                    else
                                                    {
                                                        if (phosphor_layout == 13.0)
                                                        {
                                                            w = int(floor(mod(coord.y, 4.0)));
                                                            z = int(floor(mod(coord.x, 8.0)));
                                                            weights = _547[(int(w)) * 8 + int(z)];
                                                            return weights;
                                                        }
                                                        else
                                                        {
                                                            if (phosphor_layout == 14.0)
                                                            {
                                                                w = int(floor(mod(coord.y, 4.0)));
                                                                z = int(floor(mod(coord.x, 10.0)));
                                                                weights = _577[(int(w)) * 10 + int(z)];
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

void main()
{
    r2 = LIBRA_UNIFORM_h_radius * LIBRA_UNIFORM_h_radius;
    max_size = vec2(sqrt((r2 - 2.0) / (r2 - 1.0)), 1.0);
    aspect = vec2(1.0, LIBRA_UNIFORM_OutputSize.y / LIBRA_UNIFORM_OutputSize.x);
    cornersize = LIBRA_UNIFORM_h_cornersize * min(aspect.x, aspect.y);
    cornersmooth = LIBRA_UNIFORM_h_cornersmooth / 100.0;
    overscan = vec2(1.0099999904632568359375);
    vec2 _773;
    if (LIBRA_UNIFORM_h_curvature > 0.5)
    {
        vec2 param = LIBRA_VARYING_0;
        vec2 _780 = h_warp(param);
        _773 = _780;
    }
    else
    {
        _773 = LIBRA_VARYING_0;
    }
    vec2 uv = _773;
    float _788;
    if (LIBRA_UNIFORM_h_curvature > 0.5)
    {
        vec2 param_1 = uv;
        _788 = h_corner(param_1);
    }
    else
    {
        _788 = 1.0;
    }
    float cval = _788;
    vec2 dy = vec2(0.0, LIBRA_UNIFORM_GLOW_RADIUS * LIBRA_UNIFORM_SourceSize.w);
    vec3 _808;
    if (LIBRA_UNIFORM_GLOW_ENABLE > 0.5)
    {
        vec2 param_2 = uv;
        vec2 param_3 = dy;
        _808 = get_bloom(LIBRA_TEXTURE_Source, param_2, param_3);
    }
    else
    {
        _808 = vec3(0.0);
    }
    vec3 bloom = _808;
    vec2 mask_coords = LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy;
    mask_coords = mix(mask_coords, mask_coords.yx, vec2(LIBRA_UNIFORM_VSCANLINES));
    vec2 param_4 = mask_coords;
    float param_5 = LIBRA_VARYING_1.x;
    vec3 mask = mask_weights(param_4, param_5);
    vec3 _847;
    if (LIBRA_UNIFORM_MONITOR_SUBPIXELS > 0.5)
    {
        _847 = mask.zyx;
    }
    else
    {
        _847 = mask;
    }
    mask = _847;
    vec3 crt = texture(LIBRA_TEXTURE_CRTPass, uv).xyz * LIBRA_UNIFORM_BRIGHTBOOST;
    crt += (bloom * LIBRA_UNIFORM_GLOW_STRENGTH);
    crt = clamp(crt, vec3(0.0), vec3(1.0));
    crt = pow(crt, vec3(1.0 / LIBRA_UNIFORM_H_OUTPUT_GAMMA));
    crt = mask + ((vec3(1.0) - (mask * 2.0)) * pow(abs(mask - crt), ((mask * LIBRA_UNIFORM_MASK_STRENGTH) * (LIBRA_UNIFORM_H_MaskGamma - 1.0)) + vec3(1.0)));
    crt *= cval;
    FragColor = vec4(crt, 1.0);
}
