#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_BRIGHT_BOOST;
uniform float LIBRA_UNIFORM_DILATION;
uniform float LIBRA_UNIFORM_GAMMA_INPUT;
uniform float LIBRA_UNIFORM_GAMMA_OUTPUT;
uniform float LIBRA_UNIFORM_MASK_SIZE;
uniform float LIBRA_UNIFORM_MASK_STAGGER;
uniform float LIBRA_UNIFORM_MASK_STRENGTH;
uniform float LIBRA_UNIFORM_MASK_DOT_HEIGHT;
uniform float LIBRA_UNIFORM_MASK_DOT_WIDTH;
uniform float LIBRA_UNIFORM_SCANLINE_CUTOFF;
uniform float LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MAX;
uniform float LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MIN;
uniform float LIBRA_UNIFORM_SCANLINE_BRIGHT_MAX;
uniform float LIBRA_UNIFORM_SCANLINE_BRIGHT_MIN;
uniform float LIBRA_UNIFORM_SCANLINE_STRENGTH;
uniform float LIBRA_UNIFORM_SHARPNESS_H;
uniform float LIBRA_UNIFORM_SHARPNESS_V;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

float curve_distance(float x, float sharp)
{
    float x_step = step(0.5, x);
    float curve = 0.5 - (sqrt(0.25 - ((x - x_step) * (x - x_step))) * sign(0.5 - x));
    return mix(x, curve, sharp);
}

vec4 dilate(vec4 col)
{
    vec4 x = mix(vec4(1.0), col, vec4(LIBRA_UNIFORM_DILATION));
    return col * x;
}

mat4 get_color_matrix(vec2 co, vec2 dx)
{
    vec4 param = texture(LIBRA_TEXTURE_Source, co - dx);
    vec4 param_1 = texture(LIBRA_TEXTURE_Source, co);
    vec4 param_2 = texture(LIBRA_TEXTURE_Source, co + dx);
    vec4 param_3 = texture(LIBRA_TEXTURE_Source, co + (dx * 2.0));
    return mat4(vec4(dilate(param)), vec4(dilate(param_1)), vec4(dilate(param_2)), vec4(dilate(param_3)));
}

vec3 filter_lanczos(vec4 coeffs, mat4 color_matrix)
{
    vec4 col = color_matrix * coeffs;
    vec4 sample_min = min(color_matrix[1], color_matrix[2]);
    vec4 sample_max = max(color_matrix[1], color_matrix[2]);
    col = clamp(col, sample_min, sample_max);
    return col.xyz;
}

void main()
{
    vec2 dx = vec2(LIBRA_UNIFORM_SourceSize.z, 0.0);
    vec2 dy = vec2(0.0, LIBRA_UNIFORM_SourceSize.w);
    vec2 pix_co = (LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy) - vec2(0.5);
    vec2 tex_co = (floor(pix_co) + vec2(0.5)) * LIBRA_UNIFORM_SourceSize.zw;
    vec2 dist = fract(pix_co);
    float param = dist.x;
    float param_1 = LIBRA_UNIFORM_SHARPNESS_H * LIBRA_UNIFORM_SHARPNESS_H;
    float curve_x = curve_distance(param, param_1);
    vec4 coeffs = vec4(1.0 + curve_x, curve_x, 1.0 - curve_x, 2.0 - curve_x) * 3.1415927410125732421875;
    coeffs = max(abs(coeffs), vec4(9.9999997473787516355514526367188e-06));
    coeffs = ((sin(coeffs) * 2.0) * sin(coeffs * 0.5)) / (coeffs * coeffs);
    coeffs /= vec4(dot(coeffs, vec4(1.0)));
    vec2 param_2 = tex_co;
    vec2 param_3 = dx;
    vec4 param_4 = coeffs;
    mat4 param_5 = get_color_matrix(param_2, param_3);
    vec3 col = filter_lanczos(param_4, param_5);
    vec2 param_6 = tex_co + dy;
    vec2 param_7 = dx;
    vec4 param_8 = coeffs;
    mat4 param_9 = get_color_matrix(param_6, param_7);
    vec3 col2 = filter_lanczos(param_8, param_9);
    float param_10 = dist.y;
    float param_11 = LIBRA_UNIFORM_SHARPNESS_V;
    col = mix(col, col2, vec3(curve_distance(param_10, param_11)));
    col = pow(col, vec3(LIBRA_UNIFORM_GAMMA_INPUT / (LIBRA_UNIFORM_DILATION + 1.0)));
    float luma = dot(vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875), col);
    float bright = (max(col.x, max(col.y, col.z)) + luma) * 0.5;
    float scan_bright = clamp(bright, LIBRA_UNIFORM_SCANLINE_BRIGHT_MIN, LIBRA_UNIFORM_SCANLINE_BRIGHT_MAX);
    float scan_beam = clamp(bright * LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MAX, LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MIN, LIBRA_UNIFORM_SCANLINE_BEAM_WIDTH_MAX);
    float scan_weight = 1.0 - (pow((cos(((LIBRA_VARYING_0.y * 2.0) * 3.1415927410125732421875) * LIBRA_UNIFORM_SourceSize.y) * 0.5) + 0.5, scan_beam) * LIBRA_UNIFORM_SCANLINE_STRENGTH);
    float mask = 1.0 - LIBRA_UNIFORM_MASK_STRENGTH;
    vec2 mod_fac = floor(((LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy) * LIBRA_UNIFORM_SourceSize.xy) / (LIBRA_UNIFORM_SourceSize.xy * vec2(LIBRA_UNIFORM_MASK_SIZE, LIBRA_UNIFORM_MASK_DOT_HEIGHT * LIBRA_UNIFORM_MASK_SIZE)));
    int dot_no = int(mod((mod_fac.x + (mod(mod_fac.y, 2.0) * LIBRA_UNIFORM_MASK_STAGGER)) / LIBRA_UNIFORM_MASK_DOT_WIDTH, 3.0));
    vec3 mask_weight;
    if (dot_no == 0)
    {
        mask_weight = vec3(1.0, mask, mask);
    }
    else
    {
        if (dot_no == 1)
        {
            mask_weight = vec3(mask, 1.0, mask);
        }
        else
        {
            mask_weight = vec3(mask, mask, 1.0);
        }
    }
    if (LIBRA_UNIFORM_SourceSize.y >= LIBRA_UNIFORM_SCANLINE_CUTOFF)
    {
        scan_weight = 1.0;
    }
    col2 = col;
    col *= vec3(scan_weight);
    col = mix(col, col2, vec3(scan_bright));
    col *= mask_weight;
    col = pow(col, vec3(1.0 / LIBRA_UNIFORM_GAMMA_OUTPUT));
    FragColor = vec4(col * LIBRA_UNIFORM_BRIGHT_BOOST, 1.0);
}
