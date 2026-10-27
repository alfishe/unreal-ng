#version 330

struct lines_t
{
    float upper_sample_y;
    float lower_sample_y;
    float upper_distance_y;
    float lower_distance_y;
};

const vec3 _89[2] = vec3[](vec3(1.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0));
const vec3 _108[3] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0));
const vec3 _126[4] = vec3[](vec3(1.0, 0.0, 0.0), vec3(1.0, 1.0, 0.0), vec3(0.0, 1.0, 1.0), vec3(0.0, 0.0, 1.0));
const vec3 _142[5] = vec3[](vec3(1.0, 0.0, 0.0), vec3(1.0, 0.0, 1.0), vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0));
const vec3 _166[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
const vec3 _178[4] = vec3[](vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0));
const vec3 _190[5] = vec3[](vec3(0.0, 0.0, 1.0), vec3(1.0, 0.0, 1.0), vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 1.0, 0.0));

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_MaxSpotSize;
uniform float LIBRA_UNIFORM_MinSpotSize;
uniform float LIBRA_UNIFORM_OddFieldFirst;
uniform float LIBRA_UNIFORM_OverscanHorizontal;
uniform float LIBRA_UNIFORM_OverscanVertical;
uniform float LIBRA_UNIFORM_MaskType;
uniform float LIBRA_UNIFORM_SubpixelMaskPattern;
uniform float LIBRA_UNIFORM_SubpixelPattern;
uniform float LIBRA_UNIFORM_DynamicMaskTriads;

uniform sampler2D LIBRA_TEXTURE_Filtered;
uniform sampler2D LIBRA_TEXTURE_Source;

flat in float LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_2;
layout(location = 0) out vec4 FragColor;

lines_t get_closest_lines(float v, vec4 sourceSize)
{
    float upper_sample_y = round(v * sourceSize.y) + 0.5;
    float lower_sample_y = upper_sample_y - 1.0;
    float upper_distance_y = upper_sample_y - (v * sourceSize.y);
    float lower_distance_y = upper_distance_y - 1.0;
    upper_sample_y *= sourceSize.w;
    lower_sample_y *= sourceSize.w;
    return lines_t(upper_sample_y, lower_sample_y, upper_distance_y, lower_distance_y);
}

vec3 spot(vec3 sample_value, vec3 width_rcp, float distance_x, float distance_y)
{
    vec3 x = clamp(width_rcp * abs(distance_x), vec3(0.0), vec3(1.0));
    vec3 y = clamp(width_rcp * abs(distance_y), vec3(0.0), vec3(1.0));
    return (((sample_value * width_rcp) * width_rcp) * (((x * x) * ((x * 2.0) - vec3(3.0))) + vec3(1.0))) * (((y * y) * ((y * 2.0) - vec3(3.0))) + vec3(1.0));
}

vec3 scanlines(lines_t lines, float u)
{
    vec3 value = vec3(0.0);
    float start_x = LIBRA_UNIFORM_SourceSize.z * (round((u * LIBRA_UNIFORM_SourceSize.x) - (LIBRA_UNIFORM_MaxSpotSize / LIBRA_VARYING_1)) + 0.5);
    float stop_x = LIBRA_UNIFORM_SourceSize.z * round((u * LIBRA_UNIFORM_SourceSize.x) + (LIBRA_UNIFORM_MaxSpotSize / LIBRA_VARYING_1));
    float distance_x = (LIBRA_VARYING_1 * LIBRA_UNIFORM_SourceSize.x) * (start_x - u);
    for (float sample_x = start_x; sample_x < stop_x; sample_x += LIBRA_UNIFORM_SourceSize.z)
    {
        vec3 upper_sample = textureLod(LIBRA_TEXTURE_Filtered, vec2(sample_x, lines.upper_sample_y), 0.0).xyz;
        vec3 upper_width_rcp = textureLod(LIBRA_TEXTURE_Source, vec2(sample_x, lines.upper_sample_y), 0.0).xyz;
        vec3 lower_sample = textureLod(LIBRA_TEXTURE_Filtered, vec2(sample_x, lines.lower_sample_y), 0.0).xyz;
        vec3 lower_width_rcp = textureLod(LIBRA_TEXTURE_Source, vec2(sample_x, lines.lower_sample_y), 0.0).xyz;
        vec3 param = upper_sample;
        vec3 param_1 = upper_width_rcp;
        float param_2 = distance_x;
        float param_3 = lines.upper_distance_y;
        value += spot(param, param_1, param_2, param_3);
        vec3 param_4 = lower_sample;
        vec3 param_5 = lower_width_rcp;
        float param_6 = distance_x;
        float param_7 = lines.lower_distance_y;
        value += spot(param_4, param_5, param_6, param_7);
        distance_x += LIBRA_VARYING_1;
    }
    return value * (LIBRA_VARYING_1 * LIBRA_UNIFORM_MaxSpotSize);
}

lines_t get_even_lines(float v, vec4 sourceSize)
{
    float upper_sample_y = (ceil(((0.5 * v) * sourceSize.y) - 0.25) * 2.0) + 0.5;
    float lower_sample_y = upper_sample_y - 2.0;
    float upper_distance_y = 0.5 * (upper_sample_y - (v * sourceSize.y));
    float lower_distance_y = upper_distance_y - 1.0;
    upper_sample_y *= sourceSize.w;
    lower_sample_y *= sourceSize.w;
    return lines_t(upper_sample_y, lower_sample_y, upper_distance_y, lower_distance_y);
}

lines_t get_odd_lines(float v, vec4 sourceSize)
{
    float upper_sample_y = (ceil(((0.5 * v) * sourceSize.y) + 0.25) * 2.0) - 0.5;
    float lower_sample_y = upper_sample_y - 2.0;
    float upper_distance_y = 0.5 * (upper_sample_y - (v * sourceSize.y));
    float lower_distance_y = upper_distance_y - 1.0;
    upper_sample_y *= sourceSize.w;
    lower_sample_y *= sourceSize.w;
    return lines_t(upper_sample_y, lower_sample_y, upper_distance_y, lower_distance_y);
}

vec4 subpixel_aperture_grille(vec2 coord, vec4 output_size, float subpixel_pattern, float pixels_per_triad)
{
    float x = coord.x * output_size.x;
    vec3 mask = vec3(1.0);
    float mask_coverage = 1.0;
    if (subpixel_pattern == 0.0)
    {
        if (pixels_per_triad == 2.0)
        {
            mask = _89[int(mod(x, 2.0))];
            mask_coverage = 2.0;
        }
        else
        {
            if (pixels_per_triad == 3.0)
            {
                mask = _108[int(mod(x, 3.0))];
                mask_coverage = 3.0;
            }
            else
            {
                if (pixels_per_triad == 4.0)
                {
                    mask = _126[int(mod(x, 4.0))];
                    mask_coverage = 2.0;
                }
                else
                {
                    if (pixels_per_triad == 5.0)
                    {
                        mask = _142[int(mod(x, 5.0))];
                        mask_coverage = 2.5;
                    }
                }
            }
        }
    }
    else
    {
        if (pixels_per_triad == 2.0)
        {
            mask = _89[int(mod(x, 2.0))];
            mask_coverage = 2.0;
        }
        else
        {
            if (pixels_per_triad == 3.0)
            {
                mask = _166[int(mod(x, 3.0))];
                mask_coverage = 3.0;
            }
            else
            {
                if (pixels_per_triad == 4.0)
                {
                    mask = _178[int(mod(x, 4.0))];
                    mask_coverage = 2.0;
                }
                else
                {
                    if (pixels_per_triad == 5.0)
                    {
                        mask = _190[int(mod(x, 5.0))];
                        mask_coverage = 2.5;
                    }
                }
            }
        }
    }
    return vec4(mask, mask_coverage);
}

vec3 blend_mask(vec3 mask, vec3 pixel_value, float mask_coverage)
{
    vec3 a = clamp((pixel_value - vec3(1.0)) / vec3(1.0 - mask_coverage), vec3(0.0), pixel_value);
    vec3 b = clamp((vec3(1.0) - (pixel_value * mask_coverage)) / vec3(1.0 - mask_coverage), vec3(0.0), pixel_value);
    return ((a * mask_coverage) * mask) + b;
}

vec4 dynamic_aperture_grille(vec2 coord, vec4 output_size, float mask_triads)
{
    float kernel_radius = (3.0 * mask_triads) * output_size.z;
    float x = (3.0 * mask_triads) * coord.x;
    vec3 mask = vec3(0.0);
    if (kernel_radius < 0.5)
    {
        float x1 = clamp((x - round(x)) / kernel_radius, -1.0, 1.0);
        vec3 p0 = _166[int(mod(round(x) - 1.0, 3.0) + 0.001000000047497451305389404296875)];
        vec3 p1 = p0.zxy;
        mask = (p0 + p1) + ((p1 - p0) * (x1 + (sin(3.1415927410125732421875 * x1) / 3.1415927410125732421875)));
        mask *= 0.5;
    }
    else
    {
        if (kernel_radius < 1.0)
        {
            float x1_1 = clamp((x - (floor(x) + 0.0)) / kernel_radius, -1.0, 1.0);
            float x2 = clamp((x - (floor(x) + 1.0)) / kernel_radius, -1.0, 1.0);
            vec3 p0_1 = _166[int(mod(floor(x) - 1.0, 3.0) + 0.001000000047497451305389404296875)];
            vec3 p1_1 = p0_1.zxy;
            vec3 p2 = p1_1.zxy;
            mask = ((p0_1 + p2) + ((p1_1 - p0_1) * (x1_1 + (sin(3.1415927410125732421875 * x1_1) / 3.1415927410125732421875)))) + ((p2 - p1_1) * (x2 + (sin(3.1415927410125732421875 * x2) / 3.1415927410125732421875)));
            mask *= 0.5;
        }
        else
        {
            float x1_2 = clamp((x - (round(x) - 1.0)) / kernel_radius, -1.0, 1.0);
            float x2_1 = clamp((x - (round(x) + 0.0)) / kernel_radius, -1.0, 1.0);
            float x3 = clamp((x - (round(x) + 1.0)) / kernel_radius, -1.0, 1.0);
            vec3 p0_2 = _166[int(mod(round(x) - 2.0, 3.0) + 0.001000000047497451305389404296875)];
            vec3 p1_2 = p0_2.zxy;
            vec3 p2_1 = p1_2.zxy;
            vec3 p3 = p2_1.zxy;
            mask = (((p0_2 + p3) + ((p1_2 - p0_2) * (x1_2 + (sin(3.1415927410125732421875 * x1_2) / 3.1415927410125732421875)))) + ((p2_1 - p1_2) * (x2_1 + (sin(3.1415927410125732421875 * x2_1) / 3.1415927410125732421875)))) + ((p3 - p2_1) * (x3 + (sin(3.1415927410125732421875 * x3) / 3.1415927410125732421875)));
            mask *= 0.5;
        }
    }
    return vec4(mask, 3.0);
}

void main()
{
    vec3 value = vec3(0.0);
    if (LIBRA_UNIFORM_SourceSize.y <= 300.0)
    {
        float param = LIBRA_VARYING_0.y;
        vec4 param_1 = LIBRA_UNIFORM_SourceSize;
        lines_t lines = get_closest_lines(param, param_1);
        lines_t param_2 = lines;
        float param_3 = LIBRA_VARYING_0.x;
        value = scanlines(param_2, param_3);
    }
    else
    {
        if (LIBRA_UNIFORM_OddFieldFirst == 2.0)
        {
            float param_4 = LIBRA_VARYING_0.y;
            vec4 param_5 = LIBRA_UNIFORM_SourceSize;
            lines_t lines_even = get_even_lines(param_4, param_5);
            lines_t param_6 = lines_even;
            float param_7 = LIBRA_VARYING_0.x;
            value = scanlines(param_6, param_7);
            float param_8 = LIBRA_VARYING_0.y;
            vec4 param_9 = LIBRA_UNIFORM_SourceSize;
            lines_t lines_odd = get_odd_lines(param_8, param_9);
            lines_t param_10 = lines_odd;
            float param_11 = LIBRA_VARYING_0.x;
            value = (value + scanlines(param_10, param_11)) * 0.5;
        }
        else
        {
            if (((LIBRA_UNIFORM_FrameCount + uint(LIBRA_UNIFORM_OddFieldFirst == 1.0)) % 2u) == 0u)
            {
                float param_12 = LIBRA_VARYING_0.y;
                vec4 param_13 = LIBRA_UNIFORM_SourceSize;
                lines_t lines_1 = get_odd_lines(param_12, param_13);
                lines_t param_14 = lines_1;
                float param_15 = LIBRA_VARYING_0.x;
                value = scanlines(param_14, param_15);
            }
            else
            {
                float param_16 = LIBRA_VARYING_0.y;
                vec4 param_17 = LIBRA_UNIFORM_SourceSize;
                lines_t lines_2 = get_even_lines(param_16, param_17);
                lines_t param_18 = lines_2;
                float param_19 = LIBRA_VARYING_0.x;
                value = scanlines(param_18, param_19);
            }
        }
    }
    if (LIBRA_UNIFORM_MaskType == 1.0)
    {
        vec2 param_20 = LIBRA_VARYING_2;
        vec4 param_21 = LIBRA_UNIFORM_OutputSize;
        float param_22 = LIBRA_UNIFORM_SubpixelPattern;
        float param_23 = LIBRA_UNIFORM_SubpixelMaskPattern;
        vec4 mask = subpixel_aperture_grille(param_20, param_21, param_22, param_23);
        vec3 param_24 = mask.xyz;
        vec3 param_25 = value;
        float param_26 = mask.w;
        value = blend_mask(param_24, param_25, param_26);
    }
    else
    {
        if (LIBRA_UNIFORM_MaskType == 2.0)
        {
            vec2 param_27 = LIBRA_VARYING_2;
            vec4 param_28 = LIBRA_UNIFORM_OutputSize;
            float param_29 = LIBRA_UNIFORM_DynamicMaskTriads;
            vec4 mask_1 = dynamic_aperture_grille(param_27, param_28, param_29);
            vec3 param_30 = mask_1.xyz;
            vec3 param_31 = value;
            float param_32 = mask_1.w;
            value = blend_mask(param_30, param_31, param_32);
        }
    }
    FragColor.x = value.x;
    FragColor.y = value.y;
    FragColor.z = value.z;
}
