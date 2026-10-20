#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform float LIBRA_UNIFORM_crt_gamma;
uniform float LIBRA_UNIFORM_lcd_gamma;
uniform float LIBRA_UNIFORM_levels_contrast;
uniform float LIBRA_UNIFORM_halation_weight;
uniform float LIBRA_UNIFORM_diffusion_weight;
uniform float LIBRA_UNIFORM_bloom_underestimate_levels;
uniform float LIBRA_UNIFORM_bloom_excess;
uniform float LIBRA_UNIFORM_beam_min_sigma;
uniform float LIBRA_UNIFORM_beam_max_sigma;
uniform float LIBRA_UNIFORM_beam_spot_power;
uniform float LIBRA_UNIFORM_beam_min_shape;
uniform float LIBRA_UNIFORM_beam_max_shape;
uniform float LIBRA_UNIFORM_beam_shape_power;
uniform float LIBRA_UNIFORM_beam_horiz_filter;
uniform float LIBRA_UNIFORM_beam_horiz_sigma;
uniform float LIBRA_UNIFORM_beam_horiz_linear_rgb_weight;
uniform float LIBRA_UNIFORM_convergence_offset_x_r;
uniform float LIBRA_UNIFORM_convergence_offset_x_g;
uniform float LIBRA_UNIFORM_convergence_offset_x_b;
uniform float LIBRA_UNIFORM_convergence_offset_y_r;
uniform float LIBRA_UNIFORM_convergence_offset_y_g;
uniform float LIBRA_UNIFORM_convergence_offset_y_b;
uniform float LIBRA_UNIFORM_mask_type;
uniform float LIBRA_UNIFORM_mask_sample_mode_desired;
uniform float LIBRA_UNIFORM_mask_num_triads_desired;
uniform float LIBRA_UNIFORM_mask_triad_size_desired;
uniform float LIBRA_UNIFORM_mask_specify_num_triads;
uniform float LIBRA_UNIFORM_aa_subpixel_r_offset_x_runtime;
uniform float LIBRA_UNIFORM_aa_subpixel_r_offset_y_runtime;
uniform float LIBRA_UNIFORM_aa_cubic_c;
uniform float LIBRA_UNIFORM_aa_gauss_sigma;
uniform float LIBRA_UNIFORM_geom_mode_runtime;
uniform float LIBRA_UNIFORM_geom_radius;
uniform float LIBRA_UNIFORM_geom_view_dist;
uniform float LIBRA_UNIFORM_geom_tilt_angle_x;
uniform float LIBRA_UNIFORM_geom_tilt_angle_y;
uniform float LIBRA_UNIFORM_geom_aspect_ratio_x;
uniform float LIBRA_UNIFORM_geom_aspect_ratio_y;
uniform float LIBRA_UNIFORM_geom_overscan_x;
uniform float LIBRA_UNIFORM_geom_overscan_y;
uniform float LIBRA_UNIFORM_border_size;
uniform float LIBRA_UNIFORM_border_darkness;
uniform float LIBRA_UNIFORM_border_compress;
uniform float LIBRA_UNIFORM_interlace_bff;
uniform float LIBRA_UNIFORM_interlace_1080i;
uniform float LIBRA_UNIFORM_interlace_detect_toggle;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_Source;

in float LIBRA_VARYING_3;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_2;
in vec2 LIBRA_VARYING_1;
layout(location = 0) out vec4 FragColor;
float crt_gamma_static;
float lcd_gamma_static;
float levels_contrast_static;
float levels_autodim_temp;
float halation_weight_static;
float diffusion_weight_static;
float bloom_underestimate_levels_static;
float bloom_excess_static;
float bloom_approx_filter_static;
float beam_num_scanlines;
bool beam_generalized_gaussian;
float beam_antialias_level;
float beam_min_sigma_static;
float beam_max_sigma_static;
float beam_spot_shape_function;
float beam_spot_power_static;
float beam_min_shape_static;
float beam_max_shape_static;
float beam_shape_power_static;
float beam_horiz_filter_static;
float beam_horiz_sigma_static;
float beam_horiz_linear_rgb_weight_static;
bool beam_misconvergence;
vec2 convergence_offsets_r_static;
vec2 convergence_offsets_g_static;
vec2 convergence_offsets_b_static;
bool interlace_detect_static;
bool interlace_1080i_static;
bool interlace_bff_static;
float aa_level;
float aa_filter;
bool aa_temporal;
vec2 aa_subpixel_r_offset_static;
float aa_cubic_c_static;
float aa_gauss_sigma_static;
float mask_type_static;
float mask_sample_mode_static;
float mask_specify_num_triads_static;
float mask_triad_size_desired_static;
float mask_num_triads_desired_static;
float mask_sinc_lobes;
float mask_min_allowed_triad_size;
float geom_mode_static;
float geom_radius_static;
float geom_view_dist_static;
vec2 geom_tilt_angle_static;
float geom_aspect_ratio_static;
vec2 geom_overscan_static;
bool geom_force_correct_tangent_matrix;
float border_size_static;
float border_darkness_static;
float border_compress_static;
float bloom_approx_size_x;
float bloom_approx_size_x_for_fake;
vec2 mask_resize_viewport_scale;
float geom_max_aspect_ratio;
vec2 mask_texture_small_size;
vec2 mask_texture_large_size;
float mask_triads_per_tile;
float mask_grille14_avg_color;
float mask_grille15_avg_color;
float mask_slot_avg_color;
float mask_shadow_avg_color;
float mask_grille_avg_color;
float bloom_approx_filter;
vec2 mask_resize_src_lut_size;
float max_aa_base_pixel_border;
float max_aniso_pixel_border;
float max_tiled_pixel_border;
float max_mask_texel_border;
float max_mask_tile_border;
float mask_resize_num_tiles;
float mask_start_texels;
float mask_resize_num_triads;
vec2 min_allowed_viewport_triads;
float pi;
float under_half;
float gba_gamma;
bool interlace_detect;
float ntsc_gamma;
float pal_gamma;
float crt_reference_gamma_high;
float crt_reference_gamma_low;
float lcd_reference_gamma;
float crt_office_gamma;
float lcd_office_gamma;
bool assume_opaque_alpha;
bool linearize_input;
bool gamma_encode_output;
bool gamma_aware_bilinear;

vec2 get_last_scanline_uv(vec2 tex_uv, vec2 tex_size, vec2 texture_size_inv, vec2 il_step_multiple, float FrameCount, out float dist)
{
    float field_offset = floor(il_step_multiple.y * 0.75) * mod(FrameCount + LIBRA_UNIFORM_interlace_bff, 2.0);
    vec2 curr_texel = tex_uv * tex_size;
    vec2 prev_texel_num = floor(curr_texel - vec2(under_half));
    float wrong_field = mod(prev_texel_num.y + field_offset, il_step_multiple.y);
    vec2 scanline_texel_num = prev_texel_num - vec2(0.0, wrong_field);
    vec2 scanline_texel = scanline_texel_num + vec2(0.5);
    vec2 scanline_uv = scanline_texel * texture_size_inv;
    dist = (curr_texel.y - scanline_texel.y) / il_step_multiple.y;
    return scanline_uv;
}

float get_pass_input_gamma()
{
    return 1.0;
}

vec4 decode_input(vec4 color)
{
    if (linearize_input)
    {
        if (assume_opaque_alpha)
        {
            return vec4(pow(color.xyz, vec3(get_pass_input_gamma())), 1.0);
        }
        else
        {
            return vec4(pow(color.xyz, vec3(get_pass_input_gamma())), color.w);
        }
    }
    else
    {
        return color;
    }
}

vec4 tex2D_linearize(sampler2D tex, vec2 tex_coords)
{
    vec4 param = texture(tex, tex_coords);
    return decode_input(param);
}

vec3 get_convergence_offsets_y_vector()
{
    return vec3(LIBRA_UNIFORM_convergence_offset_y_r, LIBRA_UNIFORM_convergence_offset_y_g, LIBRA_UNIFORM_convergence_offset_y_b);
}

vec3 get_gaussian_sigma(vec3 color, float sigma_range)
{
    if (beam_spot_shape_function < 0.5)
    {
        return vec3(LIBRA_UNIFORM_beam_min_sigma) + (pow(color, vec3(LIBRA_UNIFORM_beam_spot_power)) * sigma_range);
    }
    else
    {
        vec3 color_minus_1 = color - vec3(1.0);
        return vec3(LIBRA_UNIFORM_beam_min_sigma) + (sqrt(vec3(1.0) - (color_minus_1 * color_minus_1)) * sigma_range);
    }
}

vec3 get_generalized_gaussian_beta(vec3 color, float shape_range)
{
    return vec3(LIBRA_UNIFORM_beam_min_shape) + (pow(color, vec3(LIBRA_UNIFORM_beam_shape_power)) * shape_range);
}

vec3 gamma_impl(vec3 s, vec3 s_inv)
{
    vec3 g = vec3(1.12906825542449951171875);
    vec3 c0 = vec3(0.810911953449249267578125);
    vec3 c1 = vec3(0.4808354675769805908203125);
    vec3 e = vec3(2.71828174591064453125);
    vec3 sph = s + vec3(0.5);
    vec3 lanczos_sum = c0 + (c1 / (s + vec3(1.0)));
    vec3 base = (sph + g) / e;
    return (pow(base, sph) * lanczos_sum) * s_inv;
}

vec3 uigamma_large_z_impl(vec3 s, vec3 z)
{
    vec3 numerator = pow(z, s) * exp(-z);
    vec3 denom = (vec3(7.0) + z) - s;
    denom = ((vec3(5.0) + z) - s) + (((s * 3.0) - vec3(9.0)) / denom);
    denom = ((vec3(3.0) + z) - s) + (((s * 2.0) - vec3(4.0)) / denom);
    denom = ((vec3(1.0) + z) - s) + ((s - vec3(1.0)) / denom);
    return numerator / denom;
}

vec3 ligamma_small_z_impl(vec3 s, vec3 z, vec3 s_inv)
{
    vec3 scale = pow(z, s);
    vec3 sum = s_inv;
    vec3 z_sq = z * z;
    vec3 denom1 = s + vec3(1.0);
    vec3 denom2 = (s * 2.0) + vec3(4.0);
    vec3 denom3 = (s * 6.0) + vec3(18.0);
    sum -= (z / denom1);
    sum += (z_sq / denom2);
    sum -= ((z * z_sq) / denom3);
    return scale * sum;
}

vec3 normalized_ligamma_impl(vec3 s, vec3 z, vec3 s_inv, vec3 gamma_s_inv)
{
    vec3 thresh = vec3(0.775075018405914306640625);
    bvec3 z_is_large;
    z_is_large.x = z.x > thresh.x;
    z_is_large.y = z.y > thresh.y;
    z_is_large.z = z.z > thresh.z;
    vec3 param = s;
    vec3 param_1 = z;
    vec3 large_z = vec3(1.0) - (uigamma_large_z_impl(param, param_1) * gamma_s_inv);
    vec3 param_2 = s;
    vec3 param_3 = z;
    vec3 param_4 = s_inv;
    vec3 small_z = ligamma_small_z_impl(param_2, param_3, param_4) * gamma_s_inv;
    bvec3 inverse_z_is_large = not(z_is_large);
    return (large_z * vec3(z_is_large)) + (small_z * vec3(inverse_z_is_large));
}

vec3 scanline_generalized_gaussian_integral_contrib(vec3 dist, vec3 color, float pixel_height, float sigma_range, float shape_range)
{
    vec3 param = color;
    float param_1 = sigma_range;
    vec3 alpha = get_gaussian_sigma(param, param_1) * 1.41421353816986083984375;
    vec3 param_2 = color;
    float param_3 = shape_range;
    vec3 beta = get_generalized_gaussian_beta(param_2, param_3);
    vec3 alpha_inv = vec3(1.0) / alpha;
    vec3 s = vec3(1.0) / beta;
    vec3 ph_offset = vec3(pixel_height * 0.5);
    vec3 param_4 = s;
    vec3 param_5 = beta;
    vec3 gamma_s_inv = vec3(1.0) / gamma_impl(param_4, param_5);
    vec3 dist1 = dist + ph_offset;
    vec3 dist0 = dist - ph_offset;
    vec3 param_6 = s;
    vec3 param_7 = pow(abs(dist1) * alpha_inv, beta);
    vec3 param_8 = beta;
    vec3 param_9 = gamma_s_inv;
    vec3 integral_high = sign(dist1) * normalized_ligamma_impl(param_6, param_7, param_8, param_9);
    vec3 param_10 = s;
    vec3 param_11 = pow(abs(dist0) * alpha_inv, beta);
    vec3 param_12 = beta;
    vec3 param_13 = gamma_s_inv;
    vec3 integral_low = sign(dist0) * normalized_ligamma_impl(param_10, param_11, param_12, param_13);
    return ((color * 0.5) * (integral_high - integral_low)) / vec3(pixel_height);
}

vec3 scanline_generalized_gaussian_sampled_contrib(vec3 dist, vec3 color, float pixel_height, float sigma_range, float shape_range)
{
    vec3 param = color;
    float param_1 = sigma_range;
    vec3 alpha = get_gaussian_sigma(param, param_1) * 1.41421353816986083984375;
    vec3 param_2 = color;
    float param_3 = shape_range;
    vec3 beta = get_generalized_gaussian_beta(param_2, param_3);
    vec3 alpha_inv = vec3(1.0) / alpha;
    vec3 beta_inv = vec3(1.0) / beta;
    vec3 param_4 = beta_inv;
    vec3 param_5 = beta;
    vec3 scale = (((color * beta) * 0.5) * alpha_inv) / gamma_impl(param_4, param_5);
    if (beam_antialias_level > 0.5)
    {
        vec3 sample_offset = vec3(pixel_height / 3.0);
        vec3 dist2 = dist + sample_offset;
        vec3 dist3 = abs(dist - sample_offset);
        vec3 weight1 = exp(-pow(abs(dist * alpha_inv), beta));
        vec3 weight2 = exp(-pow(abs(dist2 * alpha_inv), beta));
        vec3 weight3 = exp(-pow(abs(dist3 * alpha_inv), beta));
        return (scale / vec3(3.0)) * ((weight1 + weight2) + weight3);
    }
    else
    {
        return scale * exp(-pow(abs(dist * alpha_inv), beta));
    }
}

vec3 erf6(vec3 x)
{
    vec3 one = vec3(1.0);
    vec3 sign_x = sign(x);
    vec3 t = one / (one + (abs(x) * 0.470470011234283447265625));
    vec3 result = one - ((t * (vec3(0.348024189472198486328125) + (t * (vec3(-0.095879800617694854736328125) + (t * 0.747855603694915771484375))))) * exp(-(x * x)));
    return result * sign_x;
}

vec3 erf(vec3 x)
{
    vec3 param = x;
    return erf6(param);
}

vec3 scanline_gaussian_integral_contrib(vec3 dist, vec3 color, float pixel_height, float sigma_range)
{
    vec3 param = color;
    float param_1 = sigma_range;
    vec3 sigma = get_gaussian_sigma(param, param_1);
    vec3 ph_offset = vec3(pixel_height * 0.5);
    vec3 denom_inv = vec3(1.0) / (sigma * 1.41421353816986083984375);
    vec3 param_2 = (dist + ph_offset) * denom_inv;
    vec3 integral_high = erf(param_2);
    vec3 param_3 = (dist - ph_offset) * denom_inv;
    vec3 integral_low = erf(param_3);
    return ((color * 0.5) * (integral_high - integral_low)) / vec3(pixel_height);
}

vec3 scanline_gaussian_sampled_contrib(vec3 dist, vec3 color, float pixel_height, float sigma_range)
{
    vec3 param = color;
    float param_1 = sigma_range;
    vec3 sigma = get_gaussian_sigma(param, param_1);
    vec3 sigma_inv = vec3(1.0) / sigma;
    vec3 inner_denom_inv = (sigma_inv * 0.5) * sigma_inv;
    vec3 outer_denom_inv = sigma_inv / vec3(sqrt(2.0 * pi));
    if (beam_antialias_level > 0.5)
    {
        vec3 sample_offset = vec3(pixel_height / 3.0);
        vec3 dist2 = dist + sample_offset;
        vec3 dist3 = abs(dist - sample_offset);
        vec3 scale = (color / vec3(3.0)) * outer_denom_inv;
        vec3 weight1 = exp((-(dist * dist)) * inner_denom_inv);
        vec3 weight2 = exp((-(dist2 * dist2)) * inner_denom_inv);
        vec3 weight3 = exp((-(dist3 * dist3)) * inner_denom_inv);
        return scale * ((weight1 + weight2) + weight3);
    }
    else
    {
        return (color * exp((-(dist * dist)) * inner_denom_inv)) * outer_denom_inv;
    }
}

vec3 scanline_contrib(vec3 dist, vec3 color, float pixel_height, float sigma_range, float shape_range)
{
    if (beam_generalized_gaussian)
    {
        if (beam_antialias_level > 1.5)
        {
            vec3 param = dist;
            vec3 param_1 = color;
            float param_2 = pixel_height;
            float param_3 = sigma_range;
            float param_4 = shape_range;
            return scanline_generalized_gaussian_integral_contrib(param, param_1, param_2, param_3, param_4);
        }
        else
        {
            vec3 param_5 = dist;
            vec3 param_6 = color;
            float param_7 = pixel_height;
            float param_8 = sigma_range;
            float param_9 = shape_range;
            return scanline_generalized_gaussian_sampled_contrib(param_5, param_6, param_7, param_8, param_9);
        }
    }
    else
    {
        if (beam_antialias_level > 1.5)
        {
            vec3 param_10 = dist;
            vec3 param_11 = color;
            float param_12 = pixel_height;
            float param_13 = sigma_range;
            return scanline_gaussian_integral_contrib(param_10, param_11, param_12, param_13);
        }
        else
        {
            vec3 param_14 = dist;
            vec3 param_15 = color;
            float param_16 = pixel_height;
            float param_17 = sigma_range;
            return scanline_gaussian_sampled_contrib(param_14, param_15, param_16, param_17);
        }
    }
}

float get_pass_output_gamma()
{
    return 1.0;
}

vec4 encode_output(vec4 color)
{
    if (gamma_encode_output)
    {
        if (assume_opaque_alpha)
        {
            return vec4(pow(color.xyz, vec3(1.0 / get_pass_output_gamma())), 1.0);
        }
        else
        {
            return vec4(pow(color.xyz, vec3(1.0 / get_pass_output_gamma())), color.w);
        }
    }
    else
    {
        return color;
    }
}

void main()
{
    crt_gamma_static = 2.5;
    lcd_gamma_static = 2.2000000476837158203125;
    levels_contrast_static = 1.0;
    levels_autodim_temp = 0.5;
    halation_weight_static = 0.0;
    diffusion_weight_static = 0.07500000298023223876953125;
    bloom_underestimate_levels_static = 0.800000011920928955078125;
    bloom_excess_static = 0.0;
    bloom_approx_filter_static = 2.0;
    beam_num_scanlines = 3.0;
    beam_generalized_gaussian = true;
    beam_antialias_level = 1.0;
    beam_min_sigma_static = 0.0199999995529651641845703125;
    beam_max_sigma_static = 0.300000011920928955078125;
    beam_spot_shape_function = 0.0;
    beam_spot_power_static = 0.3333333432674407958984375;
    beam_min_shape_static = 2.0;
    beam_max_shape_static = 4.0;
    beam_shape_power_static = 0.25;
    beam_horiz_filter_static = 0.0;
    beam_horiz_sigma_static = 0.3499999940395355224609375;
    beam_horiz_linear_rgb_weight_static = 1.0;
    beam_misconvergence = true;
    convergence_offsets_r_static = vec2(0.100000001490116119384765625, 0.20000000298023223876953125);
    convergence_offsets_g_static = vec2(0.300000011920928955078125, 0.4000000059604644775390625);
    convergence_offsets_b_static = vec2(0.5, 0.60000002384185791015625);
    interlace_detect_static = true;
    interlace_1080i_static = false;
    interlace_bff_static = false;
    aa_level = 12.0;
    aa_filter = 6.0;
    aa_temporal = false;
    aa_subpixel_r_offset_static = vec2(-0.3333333432674407958984375, 0.0);
    aa_cubic_c_static = 0.5;
    aa_gauss_sigma_static = 0.5;
    mask_type_static = 1.0;
    mask_sample_mode_static = 0.0;
    mask_specify_num_triads_static = 0.0;
    mask_triad_size_desired_static = 3.0;
    mask_num_triads_desired_static = 480.0;
    mask_sinc_lobes = 3.0;
    mask_min_allowed_triad_size = 2.0;
    geom_mode_static = 0.0;
    geom_radius_static = 2.0;
    geom_view_dist_static = 2.0;
    geom_tilt_angle_static = vec2(0.0);
    geom_aspect_ratio_static = 1.31306993961334228515625;
    geom_overscan_static = vec2(1.0);
    geom_force_correct_tangent_matrix = true;
    border_size_static = 0.014999999664723873138427734375;
    border_darkness_static = 2.0;
    border_compress_static = 2.5;
    bloom_approx_size_x = 320.0;
    bloom_approx_size_x_for_fake = 400.0;
    mask_resize_viewport_scale = vec2(0.0625);
    geom_max_aspect_ratio = 1.33333337306976318359375;
    mask_texture_small_size = vec2(64.0);
    mask_texture_large_size = vec2(512.0);
    mask_triads_per_tile = 8.0;
    mask_grille14_avg_color = 0.19869281351566314697265625;
    mask_grille15_avg_color = 0.20784313976764678955078125;
    mask_slot_avg_color = 0.18039216101169586181640625;
    mask_shadow_avg_color = 0.16078431904315948486328125;
    mask_grille_avg_color = mask_grille15_avg_color;
    bloom_approx_filter = bloom_approx_filter_static;
    mask_resize_src_lut_size = mask_texture_small_size;
    max_aa_base_pixel_border = 0.0;
    max_aniso_pixel_border = max_aa_base_pixel_border + 0.5;
    max_tiled_pixel_border = max_aniso_pixel_border;
    max_mask_texel_border = ceil(max_tiled_pixel_border);
    max_mask_tile_border = max_mask_texel_border / (mask_min_allowed_triad_size * mask_triads_per_tile);
    mask_resize_num_tiles = 2.0;
    mask_start_texels = 0.0;
    mask_resize_num_triads = mask_resize_num_tiles * mask_triads_per_tile;
    min_allowed_viewport_triads = vec2(mask_resize_num_triads) / mask_resize_viewport_scale;
    pi = 3.1415927410125732421875;
    under_half = 0.4995000064373016357421875;
    gba_gamma = 3.5;
    interlace_detect = LIBRA_UNIFORM_interlace_detect_toggle != 0.0;
    ntsc_gamma = 2.2000000476837158203125;
    pal_gamma = 2.7999999523162841796875;
    crt_reference_gamma_high = 2.5;
    crt_reference_gamma_low = 2.349999904632568359375;
    lcd_reference_gamma = 2.5;
    crt_office_gamma = 2.2000000476837158203125;
    lcd_office_gamma = 2.2000000476837158203125;
    assume_opaque_alpha = false;
    linearize_input = false;
    gamma_encode_output = false;
    gamma_aware_bilinear = !linearize_input;
    vec2 texture_size_ = LIBRA_UNIFORM_SourceSize.xy;
    vec2 texture_size_inv = vec2(1.0) / texture_size_;
    float FrameCount = float(LIBRA_UNIFORM_FrameCount);
    float ph = LIBRA_VARYING_3;
    vec2 param = LIBRA_VARYING_0;
    vec2 param_1 = texture_size_;
    vec2 param_2 = texture_size_inv;
    vec2 param_3 = LIBRA_VARYING_2;
    float param_4 = FrameCount;
    float param_5;
    vec2 _1157 = get_last_scanline_uv(param, param_1, param_2, param_3, param_4, param_5);
    float dist = param_5;
    vec2 scanline_uv = _1157;
    vec2 v_step = vec2(0.0, LIBRA_VARYING_1.y);
    vec2 param_6 = scanline_uv;
    vec3 scanline2_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_6).xyz;
    vec2 param_7 = scanline_uv + v_step;
    vec3 scanline3_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_7).xyz;
    vec3 scanline1_color;
    vec3 scanline4_color;
    vec3 scanline0_color;
    vec3 scanline5_color;
    float dist_round;
    vec3 scanline_outside_color;
    if (beam_num_scanlines > 5.5)
    {
        vec2 param_8 = scanline_uv - v_step;
        scanline1_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_8).xyz;
        vec2 param_9 = scanline_uv + (v_step * 2.0);
        scanline4_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_9).xyz;
        vec2 param_10 = scanline_uv - (v_step * 2.0);
        scanline0_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_10).xyz;
        vec2 param_11 = scanline_uv + (v_step * 3.0);
        scanline5_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_11).xyz;
    }
    else
    {
        if (beam_num_scanlines > 4.5)
        {
            vec2 param_12 = scanline_uv - v_step;
            scanline1_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_12).xyz;
            vec2 param_13 = scanline_uv + (v_step * 2.0);
            scanline4_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_13).xyz;
            dist_round = round(dist);
            vec2 sample_0_or_5_uv_off = mix(v_step * (-2.0), v_step * 3.0, vec2(dist_round));
            vec2 param_14 = scanline_uv + sample_0_or_5_uv_off;
            scanline_outside_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_14).xyz;
        }
        else
        {
            if (beam_num_scanlines > 3.5)
            {
                vec2 param_15 = scanline_uv - v_step;
                scanline1_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_15).xyz;
                vec2 param_16 = scanline_uv + (v_step * 2.0);
                scanline4_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_16).xyz;
            }
            else
            {
                if (beam_num_scanlines > 2.5)
                {
                    dist_round = round(dist);
                    vec2 sample_1or4_uv_off = mix(-v_step, v_step * 2.0, vec2(dist_round));
                    vec2 param_17 = scanline_uv + sample_1or4_uv_off;
                    scanline_outside_color = tex2D_linearize(LIBRA_TEXTURE_Source, param_17).xyz;
                }
            }
        }
    }
    vec3 dist2 = vec3(dist);
    if (beam_misconvergence)
    {
        vec3 convergence_offsets_vert_rgb = get_convergence_offsets_y_vector();
        dist2 = vec3(dist) - convergence_offsets_vert_rgb;
    }
    float sigma_range = max(LIBRA_UNIFORM_beam_max_sigma, LIBRA_UNIFORM_beam_min_sigma) - LIBRA_UNIFORM_beam_min_sigma;
    float shape_range = max(LIBRA_UNIFORM_beam_max_shape, LIBRA_UNIFORM_beam_min_shape) - LIBRA_UNIFORM_beam_min_shape;
    vec3 param_18 = dist2;
    vec3 param_19 = scanline2_color;
    float param_20 = ph;
    float param_21 = sigma_range;
    float param_22 = shape_range;
    vec3 scanline2_contrib = scanline_contrib(param_18, param_19, param_20, param_21, param_22);
    vec3 param_23 = abs(vec3(1.0) - dist2);
    vec3 param_24 = scanline3_color;
    float param_25 = ph;
    float param_26 = sigma_range;
    float param_27 = shape_range;
    vec3 scanline3_contrib = scanline_contrib(param_23, param_24, param_25, param_26, param_27);
    vec3 scanline_intensity = scanline2_contrib + scanline3_contrib;
    if (beam_num_scanlines > 5.5)
    {
        vec3 param_28 = dist2 + vec3(2.0);
        vec3 param_29 = scanline0_color;
        float param_30 = ph;
        float param_31 = sigma_range;
        float param_32 = shape_range;
        vec3 scanline0_contrib = scanline_contrib(param_28, param_29, param_30, param_31, param_32);
        vec3 param_33 = dist2 + vec3(1.0);
        vec3 param_34 = scanline1_color;
        float param_35 = ph;
        float param_36 = sigma_range;
        float param_37 = shape_range;
        vec3 scanline1_contrib = scanline_contrib(param_33, param_34, param_35, param_36, param_37);
        vec3 param_38 = abs(vec3(2.0) - dist2);
        vec3 param_39 = scanline4_color;
        float param_40 = ph;
        float param_41 = sigma_range;
        float param_42 = shape_range;
        vec3 scanline4_contrib = scanline_contrib(param_38, param_39, param_40, param_41, param_42);
        vec3 param_43 = abs(vec3(3.0) - dist2);
        vec3 param_44 = scanline5_color;
        float param_45 = ph;
        float param_46 = sigma_range;
        float param_47 = shape_range;
        vec3 scanline5_contrib = scanline_contrib(param_43, param_44, param_45, param_46, param_47);
        scanline_intensity += (((scanline0_contrib + scanline1_contrib) + scanline4_contrib) + scanline5_contrib);
    }
    else
    {
        if (beam_num_scanlines > 4.5)
        {
            vec3 param_48 = dist2 + vec3(1.0);
            vec3 param_49 = scanline1_color;
            float param_50 = ph;
            float param_51 = sigma_range;
            float param_52 = shape_range;
            vec3 scanline1_contrib_1 = scanline_contrib(param_48, param_49, param_50, param_51, param_52);
            vec3 param_53 = abs(vec3(2.0) - dist2);
            vec3 param_54 = scanline4_color;
            float param_55 = ph;
            float param_56 = sigma_range;
            float param_57 = shape_range;
            vec3 scanline4_contrib_1 = scanline_contrib(param_53, param_54, param_55, param_56, param_57);
            vec3 dist0or5 = mix(dist2 + vec3(2.0), vec3(3.0) - dist2, vec3(dist_round));
            vec3 param_58 = dist0or5;
            vec3 param_59 = scanline_outside_color;
            float param_60 = ph;
            float param_61 = sigma_range;
            float param_62 = shape_range;
            vec3 scanline0or5_contrib = scanline_contrib(param_58, param_59, param_60, param_61, param_62);
            scanline_intensity += ((scanline1_contrib_1 + scanline4_contrib_1) + scanline0or5_contrib);
        }
        else
        {
            if (beam_num_scanlines > 3.5)
            {
                vec3 param_63 = dist2 + vec3(1.0);
                vec3 param_64 = scanline1_color;
                float param_65 = ph;
                float param_66 = sigma_range;
                float param_67 = shape_range;
                vec3 scanline1_contrib_2 = scanline_contrib(param_63, param_64, param_65, param_66, param_67);
                vec3 param_68 = abs(vec3(2.0) - dist2);
                vec3 param_69 = scanline4_color;
                float param_70 = ph;
                float param_71 = sigma_range;
                float param_72 = shape_range;
                vec3 scanline4_contrib_2 = scanline_contrib(param_68, param_69, param_70, param_71, param_72);
                scanline_intensity += (scanline1_contrib_2 + scanline4_contrib_2);
            }
            else
            {
                if (beam_num_scanlines > 2.5)
                {
                    vec3 dist1or4 = mix(dist2 + vec3(1.0), vec3(2.0) - dist2, vec3(dist_round));
                    vec3 param_73 = dist1or4;
                    vec3 param_74 = scanline_outside_color;
                    float param_75 = ph;
                    float param_76 = sigma_range;
                    float param_77 = shape_range;
                    vec3 scanline1or4_contrib = scanline_contrib(param_73, param_74, param_75, param_76, param_77);
                    scanline_intensity += scanline1or4_contrib;
                }
            }
        }
    }
    vec4 param_78 = vec4(scanline_intensity * levels_autodim_temp, 1.0);
    FragColor = encode_output(param_78);
}
