#version 330

const vec3 _1147[30] = vec3[](vec3(1.0), vec3(1.0), vec3(1.0), vec3(1.0), vec3(1.0), vec3(1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0));
const vec3 _1150[30] = vec3[](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(1.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 0.0));
const vec3 _1153[30] = vec3[](vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
const vec3 _1155[30] = vec3[](vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.5), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
const int _1159[5] = int[](2, 2, 3, 3, 4);

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_FrameTimeDelta;
uniform float LIBRA_UNIFORM_OriginalFPS;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_ORIENTATION;
uniform float LIBRA_UNIFORM_SCREEN_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_FREQUENCY;
uniform float LIBRA_UNIFORM_SCREEN_INTERLACED;
uniform float LIBRA_UNIFORM_SHARP_AMOUNT;
uniform float LIBRA_UNIFORM_HALATION_INTENSITY;
uniform float LIBRA_UNIFORM_HALATION_DIFFUSION;
uniform float LIBRA_UNIFORM_HALATION_WEIGHT;
uniform float LIBRA_UNIFORM_HALATION_INFLUENCE;

uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS;
uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS_FLICKER;
uniform float LIBRA_UNIFORM_COLOR_COMPENSATION;
uniform float LIBRA_UNIFORM_COLOR_SATURATION;
uniform float LIBRA_UNIFORM_COLOR_OVERFLOW;
uniform float LIBRA_UNIFORM_COLOR_CONTRAST;
uniform float LIBRA_UNIFORM_COLOR_PROFILE;
uniform float LIBRA_UNIFORM_COLOR_TEMPERATUE;
uniform float LIBRA_UNIFORM_COLOR_BLACK_LIGHT;
uniform float LIBRA_UNIFORM_SCANLINES_STRENGTH;
uniform float LIBRA_UNIFORM_SCANLINES_OFFSET;
uniform float LIBRA_UNIFORM_BEAM_WIDTH_MIN;
uniform float LIBRA_UNIFORM_BEAM_WIDTH_MAX;
uniform float LIBRA_UNIFORM_BEAM_SHAPE;
uniform float LIBRA_UNIFORM_BEAM_FILTER;
uniform float LIBRA_UNIFORM_ANTI_RINGING;
uniform float LIBRA_UNIFORM_SCANLINES_COLOR_BURN;
uniform float LIBRA_UNIFORM_MASK_INTENSITY;
uniform float LIBRA_UNIFORM_MASK_BLEND;
uniform float LIBRA_UNIFORM_MASK_SCALE;
uniform float LIBRA_UNIFORM_MASK_TYPE;
uniform float LIBRA_UNIFORM_MASK_SUBPIXEL;
uniform float LIBRA_UNIFORM_MASK_SUBPIXEL_ORDER;
uniform float LIBRA_UNIFORM_MASK_SUBPIXEL_SHAPE;
uniform float LIBRA_UNIFORM_MASK_COLOR_BLEED;
uniform float LIBRA_UNIFORM_CRT_CURVATURE_AMOUNT;
uniform float LIBRA_UNIFORM_CRT_VIGNETTE_AMOUNT;
uniform float LIBRA_UNIFORM_CRT_NOISE_AMOUNT;
uniform float LIBRA_UNIFORM_CRT_CORNER_RAIDUS;
uniform float LIBRA_UNIFORM_CRT_CORNER_SMOOTHNESS;

uniform sampler2D LIBRA_TEXTURE_PhosphorPass;
uniform sampler2D LIBRA_TEXTURE_SharpBlurVPass;
uniform sampler2D LIBRA_TEXTURE_HalationBlurVPass;

in float LIBRA_VARYING_5;
in vec2 LIBRA_VARYING_9;
flat in int LIBRA_VARYING_3;
in vec4 LIBRA_VARYING_7;
in float LIBRA_VARYING_8;
in vec3 LIBRA_VARYING_4;
flat in uvec2 LIBRA_VARYING_10;
in mat4 LIBRA_VARYING_11;
in vec4 LIBRA_VARYING_6;
in vec2 LIBRA_VARYING_2;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_1;
layout(location = 0) out vec4 FragColor;

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
}

vec2 apply_cubic_lens_distortion(inout vec2 tex_coord)
{
    float param = LIBRA_UNIFORM_CRT_CURVATURE_AMOUNT;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return tex_coord;
    }
    float param_4 = LIBRA_UNIFORM_CRT_CURVATURE_AMOUNT;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 1.0;
    float amount = mix_master(param_4, param_5, param_6, param_7);
    tex_coord -= vec2(0.5);
    float c = (tex_coord.x * tex_coord.x) + (tex_coord.y * tex_coord.y);
    float f = 1.0 + (c * (amount * sqrt(c)));
    f /= (1.0 + (amount * 0.125));
    tex_coord *= f;
    tex_coord += vec2(0.5);
    return tex_coord;
}

vec2 sharp_bilinear(vec2 tex_coord, vec2 source_size, vec2 output_size)
{
    vec2 texel = tex_coord * source_size;
    vec2 scale = floor(output_size / source_size);
    vec2 region_range = vec2(0.5) - (vec2(0.5) / scale);
    vec2 center_distance = fract(texel) - vec2(0.5);
    vec2 fraction = ((center_distance - clamp(center_distance, -region_range, region_range)) * scale) + vec2(0.5);
    vec2 sharp_texel = floor(texel) + fraction;
    return sharp_texel / source_size;
}

vec2 apply_sharp_bilinear_filtering(vec2 tex_coord, vec2 tex_size)
{
    vec2 param = tex_coord;
    vec2 param_1 = tex_size;
    vec2 param_2 = LIBRA_UNIFORM_OutputSize.xy;
    return sharp_bilinear(param, param_1, param_2);
}

float srgb_to_linear(float value, float gamma)
{
    float _328;
    if (value <= 0.0404481999576091766357421875)
    {
        _328 = value / 12.9200000762939453125;
    }
    else
    {
        _328 = pow((value + 0.054999999701976776123046875) / 1.05499994754791259765625, gamma);
    }
    return _328;
}

vec3 srgb_to_linear(vec3 color, float gamma)
{
    float param = color.x;
    float param_1 = gamma;
    float param_2 = color.y;
    float param_3 = gamma;
    float param_4 = color.z;
    float param_5 = gamma;
    return vec3(srgb_to_linear(param, param_1), srgb_to_linear(param_2, param_3), srgb_to_linear(param_4, param_5));
}

vec3 decode_gamma(vec3 color)
{
    vec3 param = color;
    float param_1 = 2.400000095367431640625;
    return srgb_to_linear(param, param_1);
}

float get_luminance(vec3 color)
{
    return dot(color, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
}

vec3 apply_floor(vec3 color, inout float _floor)
{
    if (_floor == 0.0)
    {
        return color;
    }
    vec3 param = color;
    float luminance = get_luminance(param);
    _floor *= (1.0 - luminance);
    return (color + vec3(_floor)) / vec3(1.0 + _floor);
}

vec3 INPUT(inout vec3 color)
{
    float color_floor = LIBRA_VARYING_9.x;
    vec3 param = color;
    color = decode_gamma(param);
    vec3 param_1 = color;
    float param_2 = color_floor;
    vec3 _1272 = apply_floor(param_1, param_2);
    color = _1272;
    return color;
}

vec2 vec2o(vec2 v)
{
    return mix(v, v.yx, vec2(float(LIBRA_VARYING_3)));
}

vec3 apply_interlace(vec2 pix_coord, vec3 even_color, vec3 uneven_color)
{
    if (LIBRA_UNIFORM_SCREEN_INTERLACED == 0.0)
    {
        return even_color + uneven_color;
    }
    float interlace_frame = float(LIBRA_VARYING_10.x);
    vec2 param = pix_coord;
    bool even = (int(floor(vec2o(param).y)) % 2) != 0;
    vec3 progressive = even_color + uneven_color;
    vec3 interlace = mix(mix(even_color, uneven_color, vec3(float(even))), mix(even_color, uneven_color, vec3(float(!even))), vec3(interlace_frame));
    return mix(progressive, interlace, vec3(LIBRA_UNIFORM_SCREEN_INTERLACED));
}

vec3 get_raw_color(sampler2D source, vec2 tex_coord, vec2 tex_size)
{
    vec2 pix_coord = tex_coord * tex_size;
    vec3 color0 = vec3(0.0);
    vec3 param = texture(source, tex_coord).xyz;
    vec3 _1837 = INPUT(param);
    vec3 color1 = _1837;
    vec2 param_1 = pix_coord;
    vec3 param_2 = color0;
    vec3 param_3 = color1;
    return apply_interlace(param_1, param_2, param_3);
}

vec2 get_scanlines_pixel_coordinate(vec2 tex_coord, vec2 tex_size)
{
    vec2 pix_coord = tex_coord * tex_size;
    pix_coord += vec2(0.5);
    return pix_coord;
}

vec2 get_scanlines_texel_coordinate(inout vec2 pix_coord, vec2 tex_size)
{
    vec2 multiple = vec2(1.0, LIBRA_VARYING_4.x);
    vec2 tex_offset = vec2(0.0);
    if (LIBRA_VARYING_4.x > 1.0)
    {
        tex_offset += (vec2(-0.5, 0.5) / multiple);
    }
    else
    {
        tex_offset += vec2(-0.5, 0.5);
    }
    bool _1731 = LIBRA_VARYING_4.y > 1.0;
    bool _1738;
    if (!_1731)
    {
        _1738 = LIBRA_VARYING_4.z > 1.0;
    }
    else
    {
        _1738 = _1731;
    }
    if (_1738)
    {
        tex_offset += vec2(-0.5, 0.0);
    }
    if (LIBRA_VARYING_4.x > 1.0)
    {
        tex_offset += ((vec2(0.0, 0.5) / multiple) * (LIBRA_VARYING_4.x - 1.0));
    }
    vec2 param = tex_offset;
    pix_coord = floor(pix_coord) + vec2o(param);
    return pix_coord / tex_size;
}

vec2 vec2ox(vec2 v, float f)
{
    return mix(vec2(v.x, f), vec2(f, v.y), vec2(float(LIBRA_VARYING_3)));
}

vec2 vec2oy(vec2 v, float f)
{
    return mix(vec2(f, v.y), vec2(v.x, f), vec2(float(LIBRA_VARYING_3)));
}

vec3 get_half_beam_color(sampler2D source, vec2 tex_coord, vec2 delta_x, vec2 delta_y, vec4 beam_filter)
{
    vec3 param = texture(source, (tex_coord - delta_x) - delta_y).xyz;
    vec3 _1609 = INPUT(param);
    vec3 x = _1609;
    vec3 param_1 = texture(source, tex_coord - delta_y).xyz;
    vec3 _1618 = INPUT(param_1);
    vec3 y = _1618;
    vec3 param_2 = texture(source, (tex_coord + delta_x) - delta_y).xyz;
    vec3 _1629 = INPUT(param_2);
    vec3 z = _1629;
    vec3 param_3 = texture(source, (tex_coord + (delta_x * 2.0)) - delta_y).xyz;
    vec3 _1641 = INPUT(param_3);
    vec3 w = _1641;
    vec3 color = mat4x3(vec3(x), vec3(y), vec3(z), vec3(w)) * beam_filter;
    vec3 color_step = step(vec3(0.0), abs(x - y) * abs(z - w));
    vec3 color_clamp = clamp(color, min(y, z), max(y, z));
    color = mix(color, color_clamp, color_step * LIBRA_VARYING_8);
    return color;
}

float max_color(vec3 color)
{
    return max(max(color.x, color.y), color.z);
}

vec3 get_half_scanlines_factor(vec3 color, float position)
{
    float min_width = LIBRA_VARYING_7.x;
    float max_width = LIBRA_VARYING_7.y;
    float slope = LIBRA_VARYING_7.z;
    float strength = LIBRA_VARYING_7.w;
    vec3 param = color;
    float param_1 = LIBRA_UNIFORM_SCANLINES_COLOR_BURN;
    float param_2 = 0.0;
    float param_3 = 0.0;
    float param_4 = 1.0;
    vec3 width_limit = mix(vec3(max_color(param)), color, vec3(mix_master(param_1, param_2, param_3, param_4)));
    vec3 width = mix(vec3(min_width), vec3(max_width), width_limit);
    vec3 factor = vec3(position) / (width + vec3(9.9999997473787516355514526367188e-06));
    factor = exp(pow(factor, vec3(slope)) * ((-10.0) * strength));
    return factor;
}

vec3 get_scanlines_color(sampler2D source, inout vec2 tex_coord, vec2 tex_size, out vec3 scanlines_factor)
{
    tex_coord += vec2(9.9999997473787516355514526367188e-06);
    vec2 pix_coord = vec2(0.0);
    vec2 param = tex_coord;
    vec2 param_1 = tex_size;
    pix_coord = get_scanlines_pixel_coordinate(param, param_1);
    vec2 param_2 = pix_coord;
    vec2 param_3 = tex_size;
    vec2 _1860 = get_scanlines_texel_coordinate(param_2, param_3);
    tex_coord = _1860;
    vec2 tex_offset = vec2(1.0) / tex_size;
    vec2 param_4 = tex_offset;
    float param_5 = 0.0;
    vec2 tex_offset_x = vec2ox(param_4, param_5);
    vec2 param_6 = tex_offset;
    float param_7 = 0.0;
    vec2 tex_offset_y = vec2oy(param_6, param_7);
    vec2 param_8 = pix_coord;
    vec2 pix_fract = fract(vec2o(param_8));
    vec4 beam_filter = vec4((pix_fract.x * pix_fract.x) * pix_fract.x, pix_fract.x * pix_fract.x, pix_fract.x, 1.0) * LIBRA_VARYING_11;
    vec2 param_9 = tex_coord;
    vec2 param_10 = tex_offset_x;
    vec2 param_11 = tex_offset_y;
    vec4 param_12 = beam_filter;
    vec3 color0 = get_half_beam_color(source, param_9, param_10, param_11, param_12);
    vec2 param_13 = tex_coord;
    vec2 param_14 = tex_offset_x;
    vec2 param_15 = vec2(0.0);
    vec4 param_16 = beam_filter;
    vec3 color1 = get_half_beam_color(source, param_13, param_14, param_15, param_16);
    vec3 param_17 = color0;
    float param_18 = pix_fract.y;
    vec3 factor0 = get_half_scanlines_factor(param_17, param_18);
    vec3 param_19 = color1;
    float param_20 = 1.0 - pix_fract.y;
    vec3 factor1 = get_half_scanlines_factor(param_19, param_20);
    vec2 param_21 = pix_coord;
    vec3 param_22 = factor0;
    vec3 param_23 = factor1;
    scanlines_factor = apply_interlace(param_21, param_22, param_23);
    vec2 param_24 = pix_coord;
    vec3 param_25 = color0 * factor0;
    vec3 param_26 = color1 * factor1;
    return apply_interlace(param_24, param_25, param_26);
}

vec2 vec2o(float x, float y)
{
    return mix(vec2(x, y), vec2(y, x), vec2(float(LIBRA_VARYING_3)));
}

vec3 apply_details(vec3 scanlines_color, sampler2D base_samler, inout vec2 base_coord, sampler2D blur_sampler, vec2 blur_coord)
{
    float param = LIBRA_UNIFORM_SHARP_AMOUNT;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 2.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return scanlines_color;
    }
    vec3 base_color = texture(base_samler, base_coord).xyz;
    vec3 blur_color = texture(blur_sampler, blur_coord).xyz;
    bool _1979 = LIBRA_VARYING_4.y > 1.0;
    bool _1986;
    if (!_1979)
    {
        _1986 = LIBRA_VARYING_4.z > 1.0;
    }
    else
    {
        _1986 = _1979;
    }
    if (_1986)
    {
        float param_4 = -1.0;
        float param_5 = 0.0;
        base_coord += (vec2o(param_4, param_5) / LIBRA_UNIFORM_OriginalSize.xy);
        base_color += texture(base_samler, base_coord).xyz;
        base_color *= 0.5;
    }
    vec3 param_6 = base_color;
    vec3 _2008 = INPUT(param_6);
    base_color = _2008;
    vec3 param_7 = blur_color;
    vec3 _2011 = INPUT(param_7);
    blur_color = _2011;
    vec3 difference_color = base_color - blur_color;
    vec3 normalized_color = base_color / max(blur_color, vec3(9.9999997473787516355514526367188e-06));
    float param_8 = LIBRA_UNIFORM_SHARP_AMOUNT;
    float param_9 = 0.0;
    float param_10 = 0.0;
    float param_11 = 2.0;
    vec3 brighten = clamp(difference_color * mix_master(param_8, param_9, param_10, param_11), vec3(0.0), vec3(1.0));
    float param_12 = LIBRA_UNIFORM_SHARP_AMOUNT;
    float param_13 = 0.0;
    float param_14 = 0.0;
    float param_15 = 2.0;
    vec3 darken = clamp(((normalized_color - vec3(1.0)) * mix_master(param_12, param_13, param_14, param_15)) + vec3(1.0), vec3(0.0), vec3(1.0));
    return mix(scanlines_color + brighten, scanlines_color * darken, vec3(0.5));
}

vec3 blend_colors(vec3 raw_color, vec3 scanlines_color)
{
    float param = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return raw_color;
    }
    float param_4 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 1.0;
    float merge_limit = min(1.0, mix_master(param_4, param_5, param_6, param_7) * 8.0);
    return mix(raw_color, scanlines_color, vec3(merge_limit));
}

vec2 shift_x_each_x(vec2 pixCoord, float amount, float size)
{
    return vec2(amount * floor(pixCoord.x / size), 0.0);
}

vec2 shift_y_every_x(vec2 pixCoord, float amount, float size)
{
    return vec2(0.0, amount * floor(mod(pixCoord.x / size, 2.0)));
}

vec2 shift_x_every_y(vec2 pixCoord, float amount, float size)
{
    return vec2(amount * floor(mod(pixCoord.y / size, 2.0)), 0.0);
}

int get_index(float pixCoord, int count)
{
    return int(floor(mod(pixCoord, float(count))));
}

vec3 get_subpixel_color(vec2 pixCoord, vec3 c1, vec3 c2, vec3 c3, vec3 c4, int count)
{
    vec3 colors[4] = vec3[](c1, c2, c3, c4);
    float param = pixCoord.x;
    int param_1 = count;
    return colors[get_index(param, param_1)];
}

float round_box(vec2 point, vec2 size, float radius)
{
    vec2 _distance = (abs(point) - size) + vec2(radius);
    return (length(max(_distance, vec2(0.0))) + min(max(_distance.x, _distance.y), 0.0)) - radius;
}

float smooth_round_box(inout vec2 coord, vec2 bounds, vec2 scale, float radius, float smoothness)
{
    coord = (coord - vec2(0.5)) * (vec2(2.0) / scale);
    float boxMinimum = min(bounds.x, bounds.y);
    float boxRadius = max(radius * boxMinimum, 1.0);
    vec2 param = coord * bounds;
    vec2 param_1 = bounds;
    float param_2 = boxRadius;
    float box = round_box(param, param_1, param_2);
    float boxSmoothness = 1.0 / (boxRadius * smoothness);
    float boxEdgeOffset = 1.0 - sqrt(boxSmoothness * 0.5);
    return smoothstep(1.0, 0.0, (box * boxSmoothness) + boxEdgeOffset);
}

float sharp_box(inout vec2 coord, vec2 scale)
{
    coord = abs((coord - vec2(0.5)) * 2.0) - scale;
    return step(max(coord.x, coord.y), 0.0);
}

vec3 get_subpixel_color(inout vec2 pixCoord, int size, int mask_type, inout int subpixel_type, inout int color_order, float radius, float smoothness)
{
    if (mask_type == 0)
    {
        return vec3(1.0);
    }
    subpixel_type--;
    color_order--;
    pixCoord /= vec2(float(size));
    if ((mask_type == 1) || (mask_type == 2))
    {
        float gap = floor(0.5 * float(size)) / float(size);
        if (subpixel_type == 2)
        {
            vec2 param = pixCoord;
            float param_1 = gap;
            float param_2 = 3.0 - gap;
            pixCoord += (shift_x_each_x(param, param_1, param_2) * float(size > 1));
        }
        else
        {
            if (subpixel_type == 4)
            {
                vec2 param_3 = pixCoord;
                float param_4 = gap;
                float param_5 = 4.0 - gap;
                pixCoord += (shift_x_each_x(param_3, param_4, param_5) * float(size > 1));
            }
        }
    }
    vec2 bounds = vec2(1.0);
    vec2 scale = vec2(1.0);
    if (mask_type == 1)
    {
        bounds = vec2(1.0, 8640.0);
    }
    else
    {
        if (mask_type == 2)
        {
            float height = (size == 1) ? 4.0 : 3.0;
            float _976;
            if (size == 1)
            {
                _976 = 0.5;
            }
            else
            {
                _976 = (size == 2) ? 0.25 : 0.16666667163372039794921875;
            }
            float offset = _976;
            float shift = (size == 3) ? 0.16666667163372039794921875 : 0.0;
            if ((subpixel_type == 0) || (subpixel_type == 1))
            {
                vec2 param_6 = pixCoord;
                float param_7 = 1.5 + shift;
                float param_8 = 2.0;
                pixCoord += shift_y_every_x(param_6, param_7, param_8);
                pixCoord.y *= 1.000010013580322265625;
                pixCoord.y += offset;
            }
            else
            {
                if ((subpixel_type == 2) || (subpixel_type == 3))
                {
                    vec2 param_9 = pixCoord;
                    float param_10 = 1.5 + shift;
                    float param_11 = 3.0;
                    pixCoord += shift_y_every_x(param_9, param_10, param_11);
                    pixCoord.y *= 1.000010013580322265625;
                    pixCoord.y += offset;
                }
                else
                {
                    if (subpixel_type == 4)
                    {
                        vec2 param_12 = pixCoord;
                        float param_13 = 1.5 + shift;
                        float param_14 = 4.0;
                        pixCoord += shift_y_every_x(param_12, param_13, param_14);
                        pixCoord.y *= 1.000010013580322265625;
                        pixCoord.y += offset;
                    }
                }
            }
            bounds = vec2(1.0, height);
            scale = vec2(1.0, (height - (offset * 2.0)) / height);
        }
        else
        {
            if (mask_type == 3)
            {
                if ((subpixel_type == 0) || (subpixel_type == 1))
                {
                    vec2 param_15 = pixCoord;
                    float param_16 = 1.0;
                    float param_17 = 1.0;
                    pixCoord += shift_x_every_y(param_15, param_16, param_17);
                }
                else
                {
                    if ((subpixel_type == 2) || (subpixel_type == 3))
                    {
                        float shift_1 = (size == 3) ? 0.16666667163372039794921875 : 0.0;
                        vec2 param_18 = pixCoord;
                        float param_19 = 1.5 + shift_1;
                        float param_20 = 1.0;
                        pixCoord += shift_x_every_y(param_18, param_19, param_20);
                        pixCoord.x *= 1.000010013580322265625;
                    }
                    else
                    {
                        if (subpixel_type == 4)
                        {
                            vec2 param_21 = pixCoord;
                            float param_22 = 2.0;
                            float param_23 = 1.0;
                            pixCoord += shift_x_every_y(param_21, param_22, param_23);
                        }
                    }
                }
            }
        }
    }
    int lutIndex = (subpixel_type * 6) + color_order;
    vec2 param_24 = pixCoord;
    vec3 param_25 = _1147[lutIndex];
    vec3 param_26 = _1150[lutIndex];
    vec3 param_27 = _1153[lutIndex];
    vec3 param_28 = _1155[lutIndex];
    int param_29 = _1159[subpixel_type];
    vec3 color = get_subpixel_color(param_24, param_25, param_26, param_27, param_28, param_29);
    if (size > 2)
    {
        vec2 param_30 = fract(pixCoord / bounds);
        vec2 param_31 = bounds * 1024.0;
        vec2 param_32 = scale;
        float param_33 = radius;
        float param_34 = smoothness;
        float _1205 = smooth_round_box(param_30, param_31, param_32, param_33, param_34);
        color *= _1205;
    }
    else
    {
        vec2 param_35 = fract(pixCoord / bounds);
        vec2 param_36 = scale;
        float _1216 = sharp_box(param_35, param_36);
        color *= _1216;
    }
    return color;
}

vec3 get_mask(vec2 tex_coord)
{
    vec2 param = tex_coord * LIBRA_UNIFORM_OutputSize.xy;
    vec2 pix_coord = vec2o(param);
    int subpixel_mask = int(LIBRA_UNIFORM_MASK_TYPE);
    int subpixel_type = int(LIBRA_VARYING_6.x);
    int subpixel_size = int(LIBRA_VARYING_6.y);
    float subpixel_smoothness = LIBRA_VARYING_6.z;
    int subpixel_color_order = int(LIBRA_VARYING_6.w);
    vec2 param_1 = pix_coord;
    int param_2 = subpixel_size;
    int param_3 = subpixel_mask;
    int param_4 = subpixel_type;
    int param_5 = subpixel_color_order;
    float param_6 = 1.0;
    float param_7 = subpixel_smoothness;
    vec3 _2136 = get_subpixel_color(param_1, param_2, param_3, param_4, param_5, param_6, param_7);
    vec3 mask = _2136;
    return mask;
}

vec3 apply_mask(inout vec3 color, float color_luma, vec2 tex_coord, inout vec3 mask_factor)
{
    if (int(LIBRA_UNIFORM_MASK_TYPE) == 0)
    {
        return color;
    }
    vec2 param = tex_coord;
    vec3 mask = get_mask(param);
    vec3 param_1 = mask;
    float mask_luma = get_luminance(param_1);
    float param_2 = LIBRA_UNIFORM_MASK_COLOR_BLEED;
    float param_3 = 0.0;
    float param_4 = 0.0;
    float param_5 = 1.0;
    float param_6 = LIBRA_UNIFORM_MASK_COLOR_BLEED;
    float param_7 = 0.0;
    float param_8 = 0.0;
    float param_9 = 1.0;
    mask += ((max(vec3(0.0), vec3(mask_luma) - mask) * mix_master(param_2, param_3, param_4, param_5)) * mix_master(param_6, param_7, param_8, param_9));
    mask = mix(mask, mask + vec3(color_luma * 0.5), vec3(LIBRA_UNIFORM_MASK_BLEND));
    vec3 mask_add = mask;
    float param_10 = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_11 = 0.0;
    float param_12 = 0.0;
    float param_13 = 1.0;
    mask_add += vec3((1.0 - mix_master(param_10, param_11, param_12, param_13)) * 0.5);
    mask_add = clamp(mask_add, vec3(0.0), vec3(1.0));
    float param_14 = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_15 = 0.0;
    float param_16 = 0.0;
    float param_17 = 1.0;
    mask_add += vec3(mix_master(param_14, param_15, param_16, param_17) * 0.5);
    mask_factor = mix(mask, mask_add, vec3(LIBRA_UNIFORM_MASK_BLEND));
    float param_18 = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_19 = 0.0;
    float param_20 = 0.0;
    float param_21 = 1.0;
    color = mix(color, color * mask_factor, vec3(mix_master(param_18, param_19, param_20, param_21)));
    return color;
}

float random(vec2 seed)
{
    vec2 i = vec2(23.1406917572021484375, 2.6651442050933837890625);
    return fract(cos(dot(seed, i)) * 123456.0);
}

vec3 apply_noise(vec3 color, float color_luma, vec2 tex_coord)
{
    float param = LIBRA_UNIFORM_CRT_NOISE_AMOUNT;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return color;
    }
    int subpixel_size = int(LIBRA_VARYING_6.y);
    float noise_floor = LIBRA_VARYING_9.y;
    float noise_frame = float(LIBRA_VARYING_10.y);
    vec2 param_4 = tex_coord * LIBRA_UNIFORM_OutputSize.xy;
    vec2 screen_coord = vec2o(param_4);
    screen_coord = floor(screen_coord / vec2(float(subpixel_size))) * float(subpixel_size);
    vec2 param_5 = screen_coord * (noise_frame + 1.0);
    float _noise = random(param_5);
    float mul_noise = _noise * 2.0;
    float add_noise = (_noise * (1.0 - color_luma)) * noise_floor;
    float param_6 = LIBRA_UNIFORM_CRT_NOISE_AMOUNT;
    float param_7 = 0.0;
    float param_8 = 0.0;
    float param_9 = 1.0;
    return mix(color, (color * mul_noise) + vec3(add_noise), vec3(((1.0 - color_luma) * mix_master(param_6, param_7, param_8, param_9)) * 0.25));
}

vec3 RAWINPUT(inout vec3 color)
{
    vec3 param = color;
    color = decode_gamma(param);
    return color;
}

vec3 adjust_halation(vec3 color, vec3 halation, vec3 scanlines_factor, vec3 mask_factor)
{
    vec3 scanlines_halation = halation - color;
    float param = LIBRA_UNIFORM_MASK_INTENSITY;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    vec3 mask_halation = ((halation * scanlines_factor) * mask_factor) * mix_master(param, param_1, param_2, param_3);
    scanlines_halation = max(vec3(0.0), scanlines_halation);
    mask_halation = max(vec3(0.0), mask_halation);
    vec3 _2281;
    if (LIBRA_UNIFORM_HALATION_INFLUENCE < 0.0)
    {
        _2281 = mask_halation * 4.0;
    }
    else
    {
        _2281 = scanlines_halation;
    }
    vec3 affective_halation = _2281;
    return mix(scanlines_halation + mask_halation, affective_halation, vec3(abs(LIBRA_UNIFORM_HALATION_INFLUENCE)));
}

float normalized_sigmoid(float value, float slope)
{
    float numerator = (1.0 - slope) * value;
    float denominator = (1.0 + slope) - ((abs(value) * slope) * 2.0);
    return numerator / denominator;
}

float normalized_sigmoid(float value, float range, float slope)
{
    float param = value / range;
    float param_1 = slope;
    return normalized_sigmoid(param, param_1) * range;
}

vec3 apply_halation(vec3 color, sampler2D halation_source, vec2 tex_coord, vec3 scanlines_factor, vec3 mask_factor)
{
    float param = LIBRA_UNIFORM_HALATION_INTENSITY;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 2.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return color;
    }
    vec3 param_4 = texture(halation_source, tex_coord).xyz;
    vec3 _2319 = RAWINPUT(param_4);
    vec3 halation = _2319;
    vec3 param_5 = halation;
    float halation_luma = get_luminance(param_5);
    vec3 param_6 = color;
    vec3 param_7 = halation;
    vec3 param_8 = scanlines_factor;
    vec3 param_9 = mask_factor;
    halation = adjust_halation(param_6, param_7, param_8, param_9);
    float param_10 = LIBRA_UNIFORM_HALATION_INTENSITY;
    float param_11 = 0.0;
    float param_12 = 0.0;
    float param_13 = 2.0;
    float param_14 = mix_master(param_10, param_11, param_12, param_13);
    float param_15 = 2.0;
    float param_16 = -0.5;
    vec3 halation_ambience = (halation * normalized_sigmoid(param_14, param_15, param_16)) * mix(1.0, halation_luma, LIBRA_UNIFORM_HALATION_WEIGHT);
    float param_17 = LIBRA_UNIFORM_HALATION_INTENSITY;
    float param_18 = 0.0;
    float param_19 = 0.0;
    float param_20 = 2.0;
    float param_21 = mix_master(param_17, param_18, param_19, param_20);
    float param_22 = 2.0;
    float param_23 = 0.5;
    vec3 halation_essence = (halation * normalized_sigmoid(param_21, param_22, param_23)) * mix(halation_luma, halation_luma * halation_luma, LIBRA_UNIFORM_HALATION_WEIGHT);
    return (color + (halation_ambience * 0.125)) + (halation_essence * 0.375);
}

vec3 apply_color_overflow(inout vec3 color, float overflow)
{
    if (overflow == 0.0)
    {
        return color;
    }
    vec3 color_overflow = (color * color) * (overflow * 0.5);
    float lumaRG = 0.4638999998569488525390625;
    float lumaRB = 0.1423999965190887451171875;
    float lumaGB = 0.39370000362396240234375;
    lumaRG *= lumaRG;
    lumaRB *= lumaRB;
    lumaGB *= lumaGB;
    color.x += (lumaRG * color_overflow.y);
    color.x += (lumaRB * color_overflow.z);
    color.y += (lumaRG * color_overflow.x);
    color.y += (lumaGB * color_overflow.z);
    color.z += (lumaRB * color_overflow.x);
    color.z += (lumaGB * color_overflow.y);
    return color;
}

vec3 apply_color_overflow(vec3 color)
{
    float param = LIBRA_UNIFORM_COLOR_OVERFLOW;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 2.0;
    vec3 param_4 = color;
    float param_5 = mix_master(param, param_1, param_2, param_3);
    vec3 _2252 = apply_color_overflow(param_4, param_5);
    return _2252;
}

float get_vignette_factor(inout vec2 tex_coord)
{
    float param = LIBRA_UNIFORM_CRT_VIGNETTE_AMOUNT;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 1.0;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return 1.0;
    }
    float param_4 = LIBRA_UNIFORM_CRT_VIGNETTE_AMOUNT;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 1.0;
    float param_8 = mix_master(param_4, param_5, param_6, param_7);
    float param_9 = -0.5;
    float amount = normalized_sigmoid(param_8, param_9);
    tex_coord -= vec2(0.5);
    float radius = 1.0 - (amount * 0.25);
    float softness = (amount * 0.125) + 0.375;
    float _distance = length(tex_coord);
    return smoothstep(radius, radius - softness, _distance);
}

float get_round_corner_factor(vec2 tex_coord)
{
    float param = LIBRA_UNIFORM_CRT_CORNER_RAIDUS;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 0.25;
    if (mix_master(param, param_1, param_2, param_3) == 0.0)
    {
        return 1.0;
    }
    float param_4 = LIBRA_UNIFORM_CRT_CORNER_RAIDUS;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 0.25;
    vec2 param_8 = tex_coord;
    vec2 param_9 = LIBRA_UNIFORM_OutputSize.xy;
    vec2 param_10 = vec2(1.0);
    float param_11 = mix_master(param_4, param_5, param_6, param_7);
    float param_12 = LIBRA_UNIFORM_CRT_CORNER_SMOOTHNESS;
    float _1539 = smooth_round_box(param_8, param_9, param_10, param_11, param_12);
    return _1539;
}

float get_brightness_compensation(float color_luma)
{
    if (int(LIBRA_UNIFORM_COLOR_COMPENSATION) == 0)
    {
        return 0.0;
    }
    float mask_blend = 1.0 - ((1.0 - LIBRA_UNIFORM_MASK_BLEND) * (1.0 - LIBRA_UNIFORM_MASK_BLEND));
    return mix(LIBRA_VARYING_5, LIBRA_VARYING_5 * (1.0 - color_luma), mask_blend);
}

vec3 apply_brightness(vec3 color, float brightness)
{
    return color * (1.0 + brightness);
}

vec3 apply_contrast(vec3 color, float contrast)
{
    if (contrast == 0.0)
    {
        return color;
    }
    vec3 param = color;
    float linear = clamp(max_color(param), 0.0, 1.0);
    float nonlinear = linear;
    nonlinear = (nonlinear * 2.0) - 1.0;
    nonlinear = sin((nonlinear * 3.141592502593994140625) * 0.5);
    nonlinear = (nonlinear + 1.0) * 0.5;
    float scale = mix(linear, nonlinear, contrast);
    float factor = scale / (linear + 9.9999997473787516355514526367188e-06);
    return color * factor;
}

vec3 apply_temperature(vec3 color, float white_point_relative)
{
    if (white_point_relative == 0.0)
    {
        return color;
    }
    mat3 _613;
    if (white_point_relative < 0.0)
    {
        _613 = mat3(vec3(1.04668915271759033203125, 0.056597299873828887939453125, -0.02677500061690807342529296875), vec3(-0.006178599782288074493408203125, 0.9779288768768310546875, 0.0521964989602565765380859375), vec3(-0.055953301489353179931640625, -0.0360764004290103912353515625, 0.841718494892120361328125));
    }
    else
    {
        _613 = mat3(vec3(0.969639599323272705078125, -0.0362020991742610931396484375, 0.0240234993398189544677734375), vec3(0.001201599952764809131622314453125, 1.01146495342254638671875, -0.044550500810146331787109375), vec3(0.044388599693775177001953125, 0.02780899964272975921630859375, 1.129950046539306640625));
    }
    mat3 white_point = _613;
    return mix(color, color * white_point, vec3(abs(white_point_relative)));
}

vec3 apply_saturation(vec3 color, float saturation)
{
    if (saturation == 1.0)
    {
        return color;
    }
    vec3 param = color;
    float luminance = get_luminance(param);
    return mix(vec3(luminance), color, vec3(saturation));
}

float linear_to_srgb(float value, float gamma)
{
    float _372;
    if (value <= 0.0031306999735534191131591796875)
    {
        _372 = value * 12.9200000762939453125;
    }
    else
    {
        _372 = (1.05499994754791259765625 * pow(value, 1.0 / gamma)) - 0.054999999701976776123046875;
    }
    return _372;
}

vec3 linear_to_srgb(vec3 color, float gamma)
{
    float param = color.x;
    float param_1 = gamma;
    float param_2 = color.y;
    float param_3 = gamma;
    float param_4 = color.z;
    float param_5 = gamma;
    return vec3(linear_to_srgb(param, param_1), linear_to_srgb(param_2, param_3), linear_to_srgb(param_4, param_5));
}

vec3 encode_gamma(vec3 color)
{
    vec3 param = color;
    float param_1 = 2.400000095367431640625;
    return linear_to_srgb(param, param_1);
}

vec3 OUTPUT(inout vec3 color, float color_luma)
{
    float param = color_luma;
    float brightness_compensation = get_brightness_compensation(param);
    vec3 param_1 = color;
    float param_2 = brightness_compensation;
    color = apply_brightness(param_1, param_2);
    float param_3 = LIBRA_UNIFORM_COLOR_BRIGHTNESS;
    float param_4 = 0.0;
    float param_5 = -1.0;
    float param_6 = 4.0;
    vec3 param_7 = color;
    float param_8 = mix_master(param_3, param_4, param_5, param_6);
    color = apply_brightness(param_7, param_8);
    float param_9 = LIBRA_UNIFORM_COLOR_CONTRAST;
    float param_10 = 0.0;
    float param_11 = -1.0;
    float param_12 = 2.0;
    vec3 param_13 = color;
    float param_14 = mix_master(param_9, param_10, param_11, param_12);
    color = apply_contrast(param_13, param_14);
    float param_15 = LIBRA_UNIFORM_COLOR_TEMPERATUE * (-1.0);
    float param_16 = 0.0;
    float param_17 = -1.0;
    float param_18 = 1.0;
    vec3 param_19 = color;
    float param_20 = mix_master(param_15, param_16, param_17, param_18);
    color = apply_temperature(param_19, param_20);
    float param_21 = LIBRA_UNIFORM_COLOR_SATURATION;
    float param_22 = 1.0;
    float param_23 = 0.0;
    float param_24 = 2.0;
    vec3 param_25 = color;
    float param_26 = mix_master(param_21, param_22, param_23, param_24);
    color = apply_saturation(param_25, param_26);
    vec3 param_27 = color;
    color = encode_gamma(param_27);
    return color;
}

void main()
{
    vec2 tex_size = LIBRA_VARYING_2;
    vec2 tex_coord = LIBRA_VARYING_0;
    vec2 param = tex_coord;
    vec2 _2472 = apply_cubic_lens_distortion(param);
    vec2 tex_coord_curved = _2472;
    vec2 param_1 = tex_coord_curved;
    vec2 param_2 = tex_size;
    vec2 tex_coord_curved_sharpened = apply_sharp_bilinear_filtering(param_1, param_2);
    vec2 scan_coord = LIBRA_VARYING_1;
    vec2 param_3 = scan_coord;
    vec2 _2485 = apply_cubic_lens_distortion(param_3);
    vec2 scan_coord_curved = _2485;
    vec2 param_4 = scan_coord_curved;
    vec2 param_5 = tex_size;
    vec2 scan_coord_curved_sharpened = apply_sharp_bilinear_filtering(param_4, param_5);
    vec2 param_6 = tex_coord_curved_sharpened;
    vec2 param_7 = tex_size;
    vec3 raw_color = get_raw_color(LIBRA_TEXTURE_PhosphorPass, param_6, param_7);
    vec3 scanlines_factor = vec3(0.0);
    vec2 param_8 = scan_coord_curved;
    vec2 param_9 = tex_size;
    vec3 param_10;
    vec3 _2506 = get_scanlines_color(LIBRA_TEXTURE_PhosphorPass, param_8, param_9, param_10);
    scanlines_factor = param_10;
    vec3 scanlines_color = _2506;
    vec3 param_11 = scanlines_color;
    vec2 param_12 = scan_coord_curved;
    vec2 param_13 = tex_coord_curved;
    vec3 _2515 = apply_details(param_11, LIBRA_TEXTURE_PhosphorPass, param_12, LIBRA_TEXTURE_SharpBlurVPass, param_13);
    scanlines_color = _2515;
    vec3 param_14 = raw_color;
    vec3 param_15 = scanlines_color;
    vec3 color = blend_colors(param_14, param_15);
    vec3 param_16 = color;
    float color_luma = get_luminance(param_16);
    vec3 mask_factor = vec3(0.0);
    vec3 param_17 = color;
    float param_18 = color_luma;
    vec2 param_19 = tex_coord;
    vec3 param_20;
    vec3 _2534 = apply_mask(param_17, param_18, param_19, param_20);
    mask_factor = param_20;
    color = _2534;
    vec3 param_21 = color;
    float param_22 = color_luma;
    vec2 param_23 = tex_coord;
    color = apply_noise(param_21, param_22, param_23);
    vec3 param_24 = color;
    vec2 param_25 = tex_coord_curved;
    vec3 param_26 = scanlines_factor;
    vec3 param_27 = mask_factor;
    color = apply_halation(param_24, LIBRA_TEXTURE_HalationBlurVPass, param_25, param_26, param_27);
    vec3 param_28 = color;
    color = apply_color_overflow(param_28);
    vec2 param_29 = tex_coord_curved;
    float _2558 = get_vignette_factor(param_29);
    color *= _2558;
    vec2 param_30 = tex_coord_curved;
    color *= get_round_corner_factor(param_30);
    vec3 param_31 = color;
    float param_32 = color_luma;
    vec3 _2572 = OUTPUT(param_31, param_32);
    FragColor = vec4(_2572, 1.0);
}
