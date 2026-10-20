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
uniform vec4 LIBRA_UNIFORM_ORIG_LINEARIZEDSize;

uniform sampler2D LIBRA_TEXTURE_ORIG_LINEARIZED;
uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_Original;

in vec2 LIBRA_VARYING_2;
in vec2 LIBRA_VARYING_0;
in float LIBRA_VARYING_3;
in vec2 LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_4;
in vec2 LIBRA_VARYING_5;
layout(location = 0) out vec4 FragColor;
float bloom_approx_scale_x;
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
float blur3_std_dev;
float blur4_std_dev;
float blur5_std_dev;
float blur6_std_dev;
float blur7_std_dev;
float blur8_std_dev;
float blur9_std_dev;
float blur10_std_dev;
float blur11_std_dev;
float blur12_std_dev;
float blur17_std_dev;
float blur25_std_dev;
float blur31_std_dev;
float blur43_std_dev;
float error_blurring;
float bloom_diff_thresh;

vec2 get_convergence_offsets_r_vector()
{
    return vec2(LIBRA_UNIFORM_convergence_offset_x_r, LIBRA_UNIFORM_convergence_offset_y_r);
}

vec2 get_convergence_offsets_g_vector()
{
    return vec2(LIBRA_UNIFORM_convergence_offset_x_g, LIBRA_UNIFORM_convergence_offset_y_g);
}

vec2 get_convergence_offsets_b_vector()
{
    return vec2(LIBRA_UNIFORM_convergence_offset_x_b, LIBRA_UNIFORM_convergence_offset_y_b);
}

float get_min_sigma_to_blur_triad(float triad_size, float thresh)
{
    return ((-0.0516799986362457275390625) + (0.611299991607666015625 * triad_size)) - ((1.1219999790191650390625 * triad_size) * sqrt(0.00041599999531172215938568115234375 + thresh));
}

float get_bloom_approx_sigma(float output_size_x_runtime, float estimated_viewport_size_x)
{
    float mask_num_triads_static = max(min_allowed_viewport_triads.x, mask_num_triads_desired_static);
    float mask_num_triads_from_size = estimated_viewport_size_x / LIBRA_UNIFORM_mask_triad_size_desired;
    float mask_num_triads_runtime = max(min_allowed_viewport_triads.x, mix(mask_num_triads_from_size, LIBRA_UNIFORM_mask_num_triads_desired, LIBRA_UNIFORM_mask_specify_num_triads));
    float max_viewport_size_x = 1474560.0;
    if (bloom_approx_filter > 1.5)
    {
        float asymptotic_triad_size = max_viewport_size_x / mask_num_triads_runtime;
        float param = asymptotic_triad_size;
        float param_1 = bloom_diff_thresh;
        float asymptotic_sigma = get_min_sigma_to_blur_triad(param, param_1);
        float bloom_approx_sigma = (asymptotic_sigma * output_size_x_runtime) / max_viewport_size_x;
        return length(vec2(bloom_approx_sigma, LIBRA_UNIFORM_beam_max_sigma));
    }
    else
    {
        float output_size_x_static = bloom_approx_size_x;
        float asymptotic_triad_size_1 = max_viewport_size_x / mask_num_triads_static;
        float param_2 = asymptotic_triad_size_1;
        float param_3 = bloom_diff_thresh;
        float asymptotic_sigma_1 = get_min_sigma_to_blur_triad(param_2, param_3);
        float bloom_approx_sigma_1 = (asymptotic_sigma_1 * output_size_x_static) / max_viewport_size_x;
        return length(vec2(bloom_approx_sigma_1, beam_max_sigma_static));
    }
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

vec3 tex2Dresize_gaussian4x4(sampler2D tex, vec2 tex_uv, vec2 dxdy, vec2 tex_size, vec2 texture_size_inv, vec2 tex_uv_to_pixel_scale, float sigma)
{
    float denom_inv = 0.5 / (sigma * sigma);
    vec2 curr_texel = tex_uv * tex_size;
    vec2 prev_texel = floor(curr_texel - vec2(under_half)) + vec2(0.5);
    vec2 prev_texel_uv = prev_texel * texture_size_inv;
    vec2 snap = vec2(float(dxdy.x <= texture_size_inv.x), float(dxdy.y <= texture_size_inv.y));
    vec2 sample5_downsize_uv = tex_uv - (dxdy * 0.5);
    vec2 sample5_uv = mix(sample5_downsize_uv, prev_texel_uv, snap);
    vec2 dx = vec2(dxdy.x, 0.0);
    vec2 sample0_uv = sample5_uv - dxdy;
    vec2 sample10_uv = sample5_uv + dxdy;
    vec2 sample15_uv = sample5_uv + (dxdy * 2.0);
    vec2 sample1_uv = sample0_uv + dx;
    vec2 sample2_uv = sample0_uv + (dx * 2.0);
    vec2 sample3_uv = sample0_uv + (dx * 3.0);
    vec2 sample4_uv = sample5_uv - dx;
    vec2 sample6_uv = sample5_uv + dx;
    vec2 sample7_uv = sample5_uv + (dx * 2.0);
    vec2 sample8_uv = sample10_uv - (dx * 2.0);
    vec2 sample9_uv = sample10_uv - dx;
    vec2 sample11_uv = sample10_uv + dx;
    vec2 sample12_uv = sample15_uv - (dx * 3.0);
    vec2 sample13_uv = sample15_uv - (dx * 2.0);
    vec2 sample14_uv = sample15_uv - dx;
    vec2 param = sample0_uv;
    vec3 sample0 = tex2D_linearize(tex, param).xyz;
    vec2 param_1 = sample1_uv;
    vec3 sample1 = tex2D_linearize(tex, param_1).xyz;
    vec2 param_2 = dx;
    vec3 sample2 = tex2D_linearize(tex, param_2).xyz;
    vec2 param_3 = sample3_uv;
    vec3 sample3 = tex2D_linearize(tex, param_3).xyz;
    vec2 param_4 = sample4_uv;
    vec3 sample4 = tex2D_linearize(tex, param_4).xyz;
    vec2 param_5 = sample5_uv;
    vec3 sample5 = tex2D_linearize(tex, param_5).xyz;
    vec2 param_6 = sample6_uv;
    vec3 sample6 = tex2D_linearize(tex, param_6).xyz;
    vec2 param_7 = sample7_uv;
    vec3 sample7 = tex2D_linearize(tex, param_7).xyz;
    vec2 param_8 = sample8_uv;
    vec3 sample8 = tex2D_linearize(tex, param_8).xyz;
    vec2 param_9 = sample9_uv;
    vec3 sample9 = tex2D_linearize(tex, param_9).xyz;
    vec2 param_10 = sample10_uv;
    vec3 sample10 = tex2D_linearize(tex, param_10).xyz;
    vec2 param_11 = sample11_uv;
    vec3 sample11 = tex2D_linearize(tex, param_11).xyz;
    vec2 param_12 = sample12_uv;
    vec3 sample12 = tex2D_linearize(tex, param_12).xyz;
    vec2 param_13 = sample13_uv;
    vec3 sample13 = tex2D_linearize(tex, param_13).xyz;
    vec2 param_14 = sample14_uv;
    vec3 sample14 = tex2D_linearize(tex, param_14).xyz;
    vec2 param_15 = sample15_uv;
    vec3 sample15 = tex2D_linearize(tex, param_15).xyz;
    vec2 dest_pixel = tex_uv * tex_uv_to_pixel_scale;
    vec2 sample0_offset = (sample0_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample1_offset = (sample1_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample2_offset = (sample2_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample3_offset = (sample3_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample4_offset = (sample4_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample5_offset = (sample5_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample6_offset = (sample6_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample7_offset = (sample7_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample8_offset = (sample8_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample9_offset = (sample9_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample10_offset = (sample10_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample11_offset = (sample11_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample12_offset = (sample12_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample13_offset = (sample13_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample14_offset = (sample14_uv * tex_uv_to_pixel_scale) - dest_pixel;
    vec2 sample15_offset = (sample15_uv * tex_uv_to_pixel_scale) - dest_pixel;
    float w0 = exp((-dot(sample0_offset, sample0_offset)) * denom_inv);
    float w1 = exp((-dot(sample1_offset, sample1_offset)) * denom_inv);
    float w2 = exp((-dot(sample2_offset, sample2_offset)) * denom_inv);
    float w3 = exp((-dot(sample3_offset, sample3_offset)) * denom_inv);
    float w4 = exp((-dot(sample4_offset, sample4_offset)) * denom_inv);
    float w5 = exp((-dot(sample5_offset, sample5_offset)) * denom_inv);
    float w6 = exp((-dot(sample6_offset, sample6_offset)) * denom_inv);
    float w7 = exp((-dot(sample7_offset, sample7_offset)) * denom_inv);
    float w8 = exp((-dot(sample8_offset, sample8_offset)) * denom_inv);
    float w9 = exp((-dot(sample9_offset, sample9_offset)) * denom_inv);
    float w10 = exp((-dot(sample10_offset, sample10_offset)) * denom_inv);
    float w11 = exp((-dot(sample11_offset, sample11_offset)) * denom_inv);
    float w12 = exp((-dot(sample12_offset, sample12_offset)) * denom_inv);
    float w13 = exp((-dot(sample13_offset, sample13_offset)) * denom_inv);
    float w14 = exp((-dot(sample14_offset, sample14_offset)) * denom_inv);
    float w15 = exp((-dot(sample15_offset, sample15_offset)) * denom_inv);
    float weight_sum_inv = 1.0 / (((((((((((((((w0 + w1) + w2) + w3) + w4) + w5) + w6) + w7) + w8) + w9) + w10) + w11) + w12) + w13) + w14) + w15);
    vec3 sum = (((((((((((((((sample0 * w0) + (sample1 * w1)) + (sample2 * w2)) + (sample3 * w3)) + (sample4 * w4)) + (sample5 * w5)) + (sample6 * w6)) + (sample7 * w7)) + (sample8 * w8)) + (sample9 * w9)) + (sample10 * w10)) + (sample11 * w11)) + (sample12 * w12)) + (sample13 * w13)) + (sample14 * w14)) + (sample15 * w15);
    return sum * weight_sum_inv;
}

vec3 tex2Dblur3x3resize(sampler2D tex, vec2 tex_uv, vec2 dxdy, float sigma)
{
    float denom_inv = 0.5 / (sigma * sigma);
    vec2 sample4_uv = tex_uv;
    vec2 dx = vec2(dxdy.x, 0.0);
    vec2 dy = vec2(0.0, dxdy.y);
    vec2 sample1_uv = sample4_uv - dy;
    vec2 sample7_uv = sample4_uv + dy;
    vec2 param = sample1_uv - dx;
    vec3 sample0 = tex2D_linearize(tex, param).xyz;
    vec2 param_1 = sample1_uv;
    vec3 sample1 = tex2D_linearize(tex, param_1).xyz;
    vec2 param_2 = sample1_uv + dx;
    vec3 sample2 = tex2D_linearize(tex, param_2).xyz;
    vec2 param_3 = sample4_uv - dx;
    vec3 sample3 = tex2D_linearize(tex, param_3).xyz;
    vec2 param_4 = sample4_uv;
    vec3 sample4 = tex2D_linearize(tex, param_4).xyz;
    vec2 param_5 = sample4_uv + dx;
    vec3 sample5 = tex2D_linearize(tex, param_5).xyz;
    vec2 param_6 = sample7_uv - dx;
    vec3 sample6 = tex2D_linearize(tex, param_6).xyz;
    vec2 param_7 = sample7_uv;
    vec3 sample7 = tex2D_linearize(tex, param_7).xyz;
    vec2 param_8 = sample7_uv + dx;
    vec3 sample8 = tex2D_linearize(tex, param_8).xyz;
    float w4 = 1.0;
    float w1_3_5_7 = exp((-1.0) * denom_inv);
    float w0_2_6_8 = exp((-2.0) * denom_inv);
    float weight_sum_inv = 1.0 / (w4 + (4.0 * (w1_3_5_7 + w0_2_6_8)));
    vec3 sum = ((sample4 * w4) + ((((sample1 + sample3) + sample5) + sample7) * w1_3_5_7)) + ((((sample0 + sample2) + sample6) + sample8) * w0_2_6_8);
    return sum * weight_sum_inv;
}

vec3 tex2Dblur3x3resize(sampler2D tex, vec2 tex_uv, vec2 dxdy)
{
    vec2 param = tex_uv;
    vec2 param_1 = dxdy;
    float param_2 = blur3_std_dev;
    return tex2Dblur3x3resize(tex, param, param_1, param_2);
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
    bloom_approx_scale_x = LIBRA_UNIFORM_OutputSize.x / LIBRA_UNIFORM_SourceSize.y;
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
    blur3_std_dev = 0.626660168170928955078125;
    blur4_std_dev = 0.66171872615814208984375;
    blur5_std_dev = 0.984570324420928955078125;
    blur6_std_dev = 1.02626955509185791015625;
    blur7_std_dev = 1.3610351085662841796875;
    blur8_std_dev = 1.4080078601837158203125;
    blur9_std_dev = 1.75332033634185791015625;
    blur10_std_dev = 1.80478513240814208984375;
    blur11_std_dev = 2.1598632335662841796875;
    blur12_std_dev = 2.215234279632568359375;
    blur17_std_dev = 3.4553558826446533203125;
    blur25_std_dev = 5.3409576416015625;
    blur31_std_dev = 6.86488056182861328125;
    blur43_std_dev = 10.1852054595947265625;
    error_blurring = 0.5;
    bloom_diff_thresh = 0.00390625;
    vec2 texture_size_ = LIBRA_UNIFORM_SourceSize.xy;
    vec2 tex_uv_r;
    vec2 tex_uv_g;
    vec2 tex_uv_b;
    if (beam_misconvergence)
    {
        vec2 uv_scanline_step = LIBRA_VARYING_2;
        vec2 convergence_offsets_r = get_convergence_offsets_r_vector();
        vec2 convergence_offsets_g = get_convergence_offsets_g_vector();
        vec2 convergence_offsets_b = get_convergence_offsets_b_vector();
        tex_uv_r = LIBRA_VARYING_0 - (convergence_offsets_r * uv_scanline_step);
        tex_uv_g = LIBRA_VARYING_0 - (convergence_offsets_g * uv_scanline_step);
        tex_uv_b = LIBRA_VARYING_0 - (convergence_offsets_b * uv_scanline_step);
    }
    float param = LIBRA_UNIFORM_OutputSize.x;
    float param_1 = LIBRA_VARYING_3;
    float bloom_approx_sigma = get_bloom_approx_sigma(param, param_1);
    vec3 color_r;
    vec3 color_g;
    vec3 color_b;
    vec3 color;
    if (bloom_approx_filter > 1.5)
    {
        if (beam_misconvergence)
        {
            vec2 param_2 = tex_uv_r;
            vec2 param_3 = LIBRA_VARYING_1;
            vec2 param_4 = texture_size_;
            vec2 param_5 = LIBRA_VARYING_4;
            vec2 param_6 = LIBRA_VARYING_5;
            float param_7 = bloom_approx_sigma;
            color_r = tex2Dresize_gaussian4x4(LIBRA_TEXTURE_ORIG_LINEARIZED, param_2, param_3, param_4, param_5, param_6, param_7);
            vec2 param_8 = tex_uv_g;
            vec2 param_9 = LIBRA_VARYING_1;
            vec2 param_10 = texture_size_;
            vec2 param_11 = LIBRA_VARYING_4;
            vec2 param_12 = LIBRA_VARYING_5;
            float param_13 = bloom_approx_sigma;
            color_g = tex2Dresize_gaussian4x4(LIBRA_TEXTURE_ORIG_LINEARIZED, param_8, param_9, param_10, param_11, param_12, param_13);
            vec2 param_14 = tex_uv_b;
            vec2 param_15 = LIBRA_VARYING_1;
            vec2 param_16 = texture_size_;
            vec2 param_17 = LIBRA_VARYING_4;
            vec2 param_18 = LIBRA_VARYING_5;
            float param_19 = bloom_approx_sigma;
            color_b = tex2Dresize_gaussian4x4(LIBRA_TEXTURE_ORIG_LINEARIZED, param_14, param_15, param_16, param_17, param_18, param_19);
        }
        else
        {
            vec2 param_20 = LIBRA_VARYING_0;
            vec2 param_21 = LIBRA_VARYING_1;
            vec2 param_22 = texture_size_;
            vec2 param_23 = LIBRA_VARYING_4;
            vec2 param_24 = LIBRA_VARYING_5;
            float param_25 = bloom_approx_sigma;
            color = tex2Dresize_gaussian4x4(LIBRA_TEXTURE_ORIG_LINEARIZED, param_20, param_21, param_22, param_23, param_24, param_25);
        }
    }
    else
    {
        if (bloom_approx_filter > 0.5)
        {
            if (beam_misconvergence)
            {
                vec2 param_26 = tex_uv_r;
                vec2 param_27 = LIBRA_VARYING_1;
                float param_28 = bloom_approx_sigma;
                color_r = tex2Dblur3x3resize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_26, param_27, param_28);
                vec2 param_29 = tex_uv_g;
                vec2 param_30 = LIBRA_VARYING_1;
                float param_31 = bloom_approx_sigma;
                color_g = tex2Dblur3x3resize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_29, param_30, param_31);
                vec2 param_32 = tex_uv_b;
                vec2 param_33 = LIBRA_VARYING_1;
                float param_34 = bloom_approx_sigma;
                color_b = tex2Dblur3x3resize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_32, param_33, param_34);
            }
            else
            {
                vec2 param_35 = LIBRA_VARYING_0;
                vec2 param_36 = LIBRA_VARYING_1;
                color = tex2Dblur3x3resize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_35, param_36);
            }
        }
        else
        {
            if (beam_misconvergence)
            {
                vec2 param_37 = tex_uv_r;
                color_r = tex2D_linearize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_37).xyz;
                vec2 param_38 = tex_uv_g;
                color_g = tex2D_linearize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_38).xyz;
                vec2 param_39 = tex_uv_b;
                color_b = tex2D_linearize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_39).xyz;
            }
            else
            {
                vec2 param_40 = LIBRA_VARYING_0;
                color = tex2D_linearize(LIBRA_TEXTURE_ORIG_LINEARIZED, param_40).xyz;
            }
        }
    }
    if (beam_misconvergence)
    {
        color = vec3(color_r.x, color_g.y, color_b.z);
    }
    vec4 param_41 = vec4(color, 1.0);
    FragColor = encode_output(param_41);
}
