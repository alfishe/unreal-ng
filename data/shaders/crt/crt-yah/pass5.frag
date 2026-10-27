#version 330

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

uniform float LIBRA_UNIFORM_COLOR_TEMPERATUE;
uniform float LIBRA_UNIFORM_COLOR_CONTRAST;
uniform float LIBRA_UNIFORM_COLOR_SATURATION;
uniform float LIBRA_UNIFORM_COLOR_OVERFLOW;
uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS;
uniform float LIBRA_UNIFORM_COLOR_BRIGHTNESS_FLICKER;
uniform float LIBRA_UNIFORM_SCANLINES_STRENGTH;
uniform float LIBRA_UNIFORM_SCANLINES_OFFSET;

uniform sampler2D LIBRA_TEXTURE_Source;

flat in int LIBRA_VARYING_1;
in float LIBRA_VARYING_2;
flat in uint LIBRA_VARYING_3;
layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;

vec2 vec2o(float x, float y)
{
    vec2 _456;
    if (LIBRA_VARYING_1 == 0)
    {
        _456 = vec2(x, y);
    }
    else
    {
        _456 = vec2(y, x);
    }
    return _456;
}

float srgb_to_linear(float value, float gamma)
{
    float _130;
    if (value <= 0.0404481999576091766357421875)
    {
        _130 = value / 12.9200000762939453125;
    }
    else
    {
        _130 = pow((value + 0.054999999701976776123046875) / 1.05499994754791259765625, gamma);
    }
    return _130;
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

float mix_master(float value, float off_value, float min_value, float max_value)
{
    float scaled = off_value + ((value - off_value) * LIBRA_UNIFORM_GLOBAL_MASTER);
    return clamp(scaled, min_value, max_value);
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

vec3 apply_brightness(vec3 color, float brightness)
{
    return color * (1.0 + brightness);
}

float max_color(vec3 color)
{
    return max(max(color.x, color.y), color.z);
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
    mat3 _391;
    if (white_point_relative < 0.0)
    {
        _391 = mat3(vec3(1.04668915271759033203125, 0.056597299873828887939453125, -0.02677500061690807342529296875), vec3(-0.006178599782288074493408203125, 0.9779288768768310546875, 0.0521964989602565765380859375), vec3(-0.055953301489353179931640625, -0.0360764004290103912353515625, 0.841718494892120361328125));
    }
    else
    {
        _391 = mat3(vec3(0.969639599323272705078125, -0.0362020991742610931396484375, 0.0240234993398189544677734375), vec3(0.001201599952764809131622314453125, 1.01146495342254638671875, -0.044550500810146331787109375), vec3(0.044388599693775177001953125, 0.02780899964272975921630859375, 1.129950046539306640625));
    }
    mat3 white_point = _391;
    return mix(color, color * white_point, vec3(abs(white_point_relative)));
}

float get_luminance(vec3 color)
{
    return dot(color, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
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

float get_luminance_corrected(inout vec3 color)
{
    float param = LIBRA_UNIFORM_COLOR_OVERFLOW;
    float param_1 = 0.0;
    float param_2 = 0.0;
    float param_3 = 2.0;
    vec3 param_4 = color;
    float param_5 = mix_master(param, param_1, param_2, param_3);
    vec3 _484 = apply_color_overflow(param_4, param_5);
    color = _484;
    float param_6 = LIBRA_UNIFORM_COLOR_BRIGHTNESS;
    float param_7 = 0.0;
    float param_8 = -1.0;
    float param_9 = 4.0;
    vec3 param_10 = color;
    float param_11 = mix_master(param_6, param_7, param_8, param_9);
    color = apply_brightness(param_10, param_11);
    float param_12 = LIBRA_UNIFORM_COLOR_CONTRAST;
    float param_13 = 0.0;
    float param_14 = -1.0;
    float param_15 = 2.0;
    vec3 param_16 = color;
    float param_17 = mix_master(param_12, param_13, param_14, param_15);
    color = apply_contrast(param_16, param_17);
    float param_18 = LIBRA_UNIFORM_COLOR_TEMPERATUE * (-1.0);
    float param_19 = 0.0;
    float param_20 = -1.0;
    float param_21 = 1.0;
    vec3 param_22 = color;
    float param_23 = mix_master(param_18, param_19, param_20, param_21);
    color = apply_temperature(param_22, param_23);
    float param_24 = LIBRA_UNIFORM_COLOR_SATURATION;
    float param_25 = 1.0;
    float param_26 = 0.0;
    float param_27 = 2.0;
    vec3 param_28 = color;
    float param_29 = mix_master(param_24, param_25, param_26, param_27);
    color = apply_saturation(param_28, param_29);
    color = min(vec3(1.0), color);
    vec3 param_30 = color;
    return get_luminance(param_30);
}

float normalized_sigmoid(float value, float slope)
{
    float numerator = (1.0 - slope) * value;
    float denominator = (1.0 + slope) - ((abs(value) * slope) * 2.0);
    return numerator / denominator;
}

vec3 get_color(sampler2D source, vec2 tex_coord)
{
    float param = 0.0;
    float param_1 = 1.0;
    vec2 texel_size = (LIBRA_UNIFORM_SourceSize.zw * LIBRA_VARYING_2) * vec2o(param, param_1);
    float scanlines_offset = 0.0;
    if (LIBRA_UNIFORM_SCANLINES_OFFSET > 0.0)
    {
        float offset_amount = 0.5;
        float _569;
        if (float(LIBRA_VARYING_3) > 1.0)
        {
            _569 = 1.0;
        }
        else
        {
            _569 = (float(LIBRA_VARYING_3) > 0.0) ? (-1.0) : 0.0;
        }
        scanlines_offset = _569;
        vec3 param_2 = texture(source, tex_coord + (texel_size * (scanlines_offset * offset_amount))).xyz;
        vec3 color = decode_gamma(param_2);
        vec3 param_3 = color;
        float _594 = get_luminance_corrected(param_3);
        float luma = _594;
        scanlines_offset *= ((luma * offset_amount) * LIBRA_UNIFORM_SCANLINES_OFFSET);
    }
    else
    {
        scanlines_offset = abs(LIBRA_UNIFORM_SCANLINES_OFFSET);
    }
    float param_4 = LIBRA_UNIFORM_SCANLINES_STRENGTH;
    float param_5 = 0.0;
    float param_6 = 0.0;
    float param_7 = 1.0;
    float param_8 = mix_master(param_4, param_5, param_6, param_7);
    float param_9 = -0.5;
    scanlines_offset *= normalized_sigmoid(param_8, param_9);
    vec3 param_10 = texture(source, tex_coord + (texel_size * scanlines_offset)).xyz;
    return decode_gamma(param_10);
}

float get_brightness_offset(vec3 color)
{
    vec3 param = color;
    float _636 = get_luminance_corrected(param);
    float luma = _636;
    float _641;
    if (float(LIBRA_VARYING_3) > 1.0)
    {
        _641 = luma;
    }
    else
    {
        float _649;
        if (float(LIBRA_VARYING_3) > 0.0)
        {
            _649 = -luma;
        }
        else
        {
            _649 = 0.0;
        }
        _641 = _649;
    }
    float brightness_offset = _641;
    float param_1 = LIBRA_UNIFORM_COLOR_BRIGHTNESS_FLICKER * 0.5;
    float param_2 = 0.5;
    brightness_offset *= normalized_sigmoid(param_1, param_2);
    return brightness_offset;
}

float linear_to_srgb(float value, float gamma)
{
    float _174;
    if (value <= 0.0031306999735534191131591796875)
    {
        _174 = value * 12.9200000762939453125;
    }
    else
    {
        _174 = (1.05499994754791259765625 * pow(value, 1.0 / gamma)) - 0.054999999701976776123046875;
    }
    return _174;
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

void main()
{
    bool _671 = LIBRA_UNIFORM_COLOR_BRIGHTNESS_FLICKER == 0.0;
    bool _677;
    if (_671)
    {
        _677 = LIBRA_UNIFORM_SCANLINES_OFFSET == 0.0;
    }
    else
    {
        _677 = _671;
    }
    if (_677)
    {
        FragColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
        return;
    }
    vec2 param = LIBRA_VARYING_0;
    vec3 color = get_color(LIBRA_TEXTURE_Source, param);
    vec3 param_1 = color;
    float brightness_offset = get_brightness_offset(param_1);
    vec3 param_2 = color;
    float param_3 = brightness_offset;
    vec3 param_4 = apply_brightness(param_2, param_3);
    FragColor = vec4(encode_gamma(param_4), 1.0);
}
