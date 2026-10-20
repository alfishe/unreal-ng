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

uniform sampler2D LIBRA_TEXTURE_mask_grille_texture_small;
uniform sampler2D LIBRA_TEXTURE_mask_slot_texture_small;
uniform sampler2D LIBRA_TEXTURE_mask_shadow_texture_small;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
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
float mask_min_allowed_tile_size;
float mask_min_expected_tile_size;
float pi_over_lobes;
float max_sinc_resize_samples_float;
float max_sinc_resize_samples_m4;

float get_mask_sample_mode()
{
    return LIBRA_UNIFORM_mask_sample_mode_desired;
}

vec2 get_first_texel_tile_uv_and_dist(vec2 tex_uv, vec2 tex_size, float dr, float input_tiles_per_texture_r, float samples, bool vertical)
{
    vec2 curr_texel = tex_uv * tex_size;
    vec2 prev_texel = floor(curr_texel - vec2(under_half)) + vec2(0.5);
    vec2 first_texel = prev_texel - vec2((samples / 2.0) - 1.0);
    vec2 first_texel_uv_wrap_2D = first_texel * dr;
    vec2 first_texel_dist_2D = curr_texel - first_texel;
    vec2 first_texel_tile_uv_wrap_2D = first_texel_uv_wrap_2D * input_tiles_per_texture_r;
    vec2 coord_negative = vec2(float(first_texel_tile_uv_wrap_2D.x < 0.0), float(first_texel_tile_uv_wrap_2D.y < 0.0));
    vec2 first_texel_tile_uv_2D = fract(first_texel_tile_uv_wrap_2D) + coord_negative;
    vec2 tile_u_and_dist = vec2(first_texel_tile_uv_2D.x, first_texel_dist_2D.x);
    vec2 tile_v_and_dist = vec2(first_texel_tile_uv_2D.y, first_texel_dist_2D.y);
    bvec2 _299 = bvec2(vertical);
    return vec2(_299.x ? tile_v_and_dist.x : tile_u_and_dist.x, _299.y ? tile_v_and_dist.y : tile_u_and_dist.y);
}

vec4 tex2Dlod0try(sampler2D tex, vec2 tex_uv)
{
    return texture(tex, tex_uv);
}

vec3 downsample_vertical_sinc_tiled(sampler2D tex, vec2 tex_uv, vec2 tex_size, float dr, float magnification_scale, float tile_size_uv_r)
{
    int samples = int(max_sinc_resize_samples_m4);
    float input_tiles_per_texture_r = 1.0 / tile_size_uv_r;
    vec2 param = tex_uv;
    vec2 param_1 = tex_size;
    float param_2 = dr;
    float param_3 = input_tiles_per_texture_r;
    float param_4 = float(samples);
    bool param_5 = true;
    vec2 first_texel_tile_r_and_dist = get_first_texel_tile_uv_and_dist(param, param_1, param_2, param_3, param_4, param_5);
    vec4 first_texel_tile_uv_rrrr = first_texel_tile_r_and_dist.xxxx;
    vec4 first_dist_unscaled = first_texel_tile_r_and_dist.yyyy;
    float tile_dr = dr * input_tiles_per_texture_r;
    int i_base = 0;
    vec4 weight_sum = vec4(0.0);
    vec3 pixel_color = vec3(0.0);
    for (int i_step = 4, i = 0; i < samples; i += i_step)
    {
        vec4 true_i = vec4(float(i_base + i)) + vec4(0.0, 1.0, 2.0, 3.0);
        vec4 tile_uv_r = fract(first_texel_tile_uv_rrrr + (true_i * tile_dr));
        vec4 tex_uv_r = tile_uv_r * tile_size_uv_r;
        vec2 param_6 = vec2(tex_uv.x, tex_uv_r.x);
        vec3 new_sample0 = tex2Dlod0try(tex, param_6).xyz;
        vec2 param_7 = vec2(tex_uv.x, tex_uv_r.y);
        vec3 new_sample1 = tex2Dlod0try(tex, param_7).xyz;
        vec2 param_8 = vec2(tex_uv.x, tex_uv_r.z);
        vec3 new_sample2 = tex2Dlod0try(tex, param_8).xyz;
        vec2 param_9 = vec2(tex_uv.x, tex_uv_r.w);
        vec3 new_sample3 = tex2Dlod0try(tex, param_9).xyz;
        vec4 dist = abs(first_dist_unscaled - true_i) * magnification_scale;
        vec4 pi_dist = dist * pi;
        vec4 pi_dist_over_lobes = dist * pi_over_lobes;
        vec4 weights = min((sin(pi_dist) * sin(pi_dist_over_lobes)) / (pi_dist * pi_dist_over_lobes), vec4(1.0));
        pixel_color += (new_sample0 * weights.xxx);
        pixel_color += (new_sample1 * weights.yyy);
        pixel_color += (new_sample2 * weights.zzz);
        pixel_color += (new_sample3 * weights.www);
        weight_sum += weights;
    }
    vec2 weight_sum_reduce = weight_sum.xy + weight_sum.zw;
    vec3 scalar_weight_sum = vec3(weight_sum_reduce.x + weight_sum_reduce.y);
    return pixel_color / scalar_weight_sum;
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
    mask_min_allowed_tile_size = ceil(mask_min_allowed_triad_size * mask_triads_per_tile);
    mask_min_expected_tile_size = mask_min_allowed_tile_size;
    pi_over_lobes = pi / mask_sinc_lobes;
    max_sinc_resize_samples_float = ((2.0 * mask_sinc_lobes) * mask_resize_src_lut_size.x) / mask_min_expected_tile_size;
    max_sinc_resize_samples_m4 = ceil(max_sinc_resize_samples_float * 0.25) * 4.0;
    vec2 tile_uv_wrap = LIBRA_VARYING_0;
    bool _495 = get_mask_sample_mode() < 0.5;
    bool _502;
    if (_495)
    {
        _502 = tile_uv_wrap.y <= mask_resize_num_tiles;
    }
    else
    {
        _502 = _495;
    }
    if (_502)
    {
        float src_dy = 1.0 / mask_resize_src_lut_size.y;
        vec2 src_tex_uv = fract(LIBRA_VARYING_0);
        vec3 pixel_color;
        if (LIBRA_UNIFORM_mask_type < 0.5)
        {
            vec2 param = src_tex_uv;
            vec2 param_1 = mask_resize_src_lut_size;
            float param_2 = src_dy;
            float param_3 = LIBRA_VARYING_1.y;
            float param_4 = 1.0;
            pixel_color = downsample_vertical_sinc_tiled(LIBRA_TEXTURE_mask_grille_texture_small, param, param_1, param_2, param_3, param_4);
        }
        else
        {
            if (LIBRA_UNIFORM_mask_type < 1.5)
            {
                vec2 param_5 = src_tex_uv;
                vec2 param_6 = mask_resize_src_lut_size;
                float param_7 = src_dy;
                float param_8 = LIBRA_VARYING_1.y;
                float param_9 = 1.0;
                pixel_color = downsample_vertical_sinc_tiled(LIBRA_TEXTURE_mask_slot_texture_small, param_5, param_6, param_7, param_8, param_9);
            }
            else
            {
                vec2 param_10 = src_tex_uv;
                vec2 param_11 = mask_resize_src_lut_size;
                float param_12 = src_dy;
                float param_13 = LIBRA_VARYING_1.y;
                float param_14 = 1.0;
                pixel_color = downsample_vertical_sinc_tiled(LIBRA_TEXTURE_mask_shadow_texture_small, param_10, param_11, param_12, param_13, param_14);
            }
        }
        FragColor = vec4(pixel_color, 1.0);
    }
    else
    {
        discard;
    }
}
