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
uniform vec4 LIBRA_UNIFORM_VERTICAL_SCANLINESSize;
uniform vec4 LIBRA_UNIFORM_BLOOM_APPROXSize;
uniform vec4 LIBRA_UNIFORM_HALATION_BLURSize;
uniform vec4 LIBRA_UNIFORM_MASK_RESIZESize;

uniform sampler2D LIBRA_TEXTURE_VERTICAL_SCANLINES;
uniform sampler2D LIBRA_TEXTURE_mask_grille_texture_large;
uniform sampler2D LIBRA_TEXTURE_mask_slot_texture_large;
uniform sampler2D LIBRA_TEXTURE_mask_shadow_texture_large;
uniform sampler2D LIBRA_TEXTURE_MASK_RESIZE;
uniform sampler2D LIBRA_TEXTURE_HALATION_BLUR;
uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_BLOOM_APPROX;

in vec2 LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_4;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_6;
in vec4 LIBRA_VARYING_5;
in vec2 LIBRA_VARYING_3;
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
float mask_min_allowed_tile_size;
float mask_min_expected_tile_size;
float pi_over_lobes;
float max_sinc_resize_samples_float;
float max_sinc_resize_samples_m4;
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

vec3 get_convergence_offsets_x_vector()
{
    return vec3(LIBRA_UNIFORM_convergence_offset_x_r, LIBRA_UNIFORM_convergence_offset_x_g, LIBRA_UNIFORM_convergence_offset_x_b);
}

float get_intermediate_gamma()
{
    return ntsc_gamma;
}

vec3 get_raw_interpolated_color(vec3 color0, vec3 color1, vec3 color2, vec3 color3, vec4 weights)
{
    return max(mat4x3(vec3(color0), vec3(color1), vec3(color2), vec3(color3)) * weights, vec3(0.0));
}

vec3 get_interpolated_linear_color(vec3 color0, vec3 color1, vec3 color2, vec3 color3, vec4 weights)
{
    float intermediate_gamma = get_intermediate_gamma();
    vec3 param = color0;
    vec3 param_1 = color1;
    vec3 param_2 = color2;
    vec3 param_3 = color3;
    vec4 param_4 = weights;
    vec3 linear_mixed_color = get_raw_interpolated_color(param, param_1, param_2, param_3, param_4);
    vec3 param_5 = pow(color0, vec3(1.0 / intermediate_gamma));
    vec3 param_6 = pow(color1, vec3(1.0 / intermediate_gamma));
    vec3 param_7 = pow(color2, vec3(1.0 / intermediate_gamma));
    vec3 param_8 = pow(color3, vec3(1.0 / intermediate_gamma));
    vec4 param_9 = weights;
    vec3 gamma_mixed_color = get_raw_interpolated_color(param_5, param_6, param_7, param_8, param_9);
    return mix(gamma_mixed_color, linear_mixed_color, vec3(LIBRA_UNIFORM_beam_horiz_linear_rgb_weight));
}

vec3 get_scanline_color(sampler2D tex, vec2 scanline_uv, vec2 uv_step_x, vec4 weights)
{
    vec3 color1 = texture(tex, scanline_uv).xyz;
    vec3 color2 = texture(tex, scanline_uv + uv_step_x).xyz;
    vec3 color0 = vec3(0.0);
    vec3 color3 = vec3(0.0);
    if (LIBRA_UNIFORM_beam_horiz_filter > 0.5)
    {
        color0 = texture(tex, scanline_uv - uv_step_x).xyz;
        color3 = texture(tex, scanline_uv + (uv_step_x * 2.0)).xyz;
    }
    vec3 param = color0;
    vec3 param_1 = color1;
    vec3 param_2 = color2;
    vec3 param_3 = color3;
    vec4 param_4 = weights;
    return get_interpolated_linear_color(param, param_1, param_2, param_3, param_4);
}

vec3 sample_single_scanline_horizontal(sampler2D tex, vec2 tex_uv, vec2 tex_size, vec2 texture_size_inv)
{
    vec2 curr_texel = tex_uv * tex_size;
    vec2 prev_texel = floor(curr_texel - vec2(under_half)) + vec2(0.5);
    vec2 prev_texel_hor = vec2(prev_texel.x, curr_texel.y);
    vec2 prev_texel_hor_uv = prev_texel_hor * texture_size_inv;
    float prev_dist = curr_texel.x - prev_texel_hor.x;
    vec4 sample_dists = vec4(1.0 + prev_dist, prev_dist, 1.0 - prev_dist, 2.0 - prev_dist);
    vec4 weights;
    if (LIBRA_UNIFORM_beam_horiz_filter < 0.5)
    {
        float x = sample_dists.y;
        float w2 = ((x * x) * x) * ((x * ((x * 6.0) - 15.0)) + 10.0);
        weights = vec4(0.0, 1.0 - w2, w2, 0.0);
    }
    else
    {
        if (LIBRA_UNIFORM_beam_horiz_filter < 1.5)
        {
            float inner_denom_inv = 1.0 / ((2.0 * LIBRA_UNIFORM_beam_horiz_sigma) * LIBRA_UNIFORM_beam_horiz_sigma);
            weights = exp((-(sample_dists * sample_dists)) * inner_denom_inv);
        }
        else
        {
            vec4 pi_dists = max(abs(sample_dists * pi), vec4(1.52587890625e-05));
            weights = ((sin(pi_dists) * 2.0) * sin(pi_dists * 0.5)) / (pi_dists * pi_dists);
        }
    }
    vec4 final_weights = weights / vec4(dot(weights, vec4(1.0)));
    vec2 uv_step_x = vec2(texture_size_inv.x, 0.0);
    vec2 param = prev_texel_hor_uv;
    vec2 param_1 = uv_step_x;
    vec4 param_2 = final_weights;
    return get_scanline_color(tex, param, param_1, param_2);
}

vec3 sample_rgb_scanline_horizontal(sampler2D tex, vec2 tex_uv, vec2 tex_size, vec2 texture_size_inv)
{
    if (beam_misconvergence)
    {
        vec3 convergence_offsets_rgb = get_convergence_offsets_x_vector();
        vec3 offset_u_rgb = convergence_offsets_rgb * texture_size_inv.xxx;
        vec2 scanline_uv_r = tex_uv - vec2(offset_u_rgb.x, 0.0);
        vec2 scanline_uv_g = tex_uv - vec2(offset_u_rgb.y, 0.0);
        vec2 scanline_uv_b = tex_uv - vec2(offset_u_rgb.z, 0.0);
        vec2 param = scanline_uv_r;
        vec2 param_1 = tex_size;
        vec2 param_2 = texture_size_inv;
        vec3 sample_r = sample_single_scanline_horizontal(tex, param, param_1, param_2);
        vec2 param_3 = scanline_uv_g;
        vec2 param_4 = tex_size;
        vec2 param_5 = texture_size_inv;
        vec3 sample_g = sample_single_scanline_horizontal(tex, param_3, param_4, param_5);
        vec2 param_6 = scanline_uv_b;
        vec2 param_7 = tex_size;
        vec2 param_8 = texture_size_inv;
        vec3 sample_b = sample_single_scanline_horizontal(tex, param_6, param_7, param_8);
        return vec3(sample_r.x, sample_g.y, sample_b.z);
    }
    else
    {
        vec2 param_9 = tex_uv;
        vec2 param_10 = tex_size;
        vec2 param_11 = texture_size_inv;
        return sample_single_scanline_horizontal(tex, param_9, param_10, param_11);
    }
}

float get_mask_sample_mode()
{
    return LIBRA_UNIFORM_mask_sample_mode_desired;
}

vec2 convert_phosphor_tile_uv_wrap_to_tex_uv(vec2 tile_uv_wrap, vec4 mask_tile_start_uv_and_size)
{
    if (get_mask_sample_mode() < 0.5)
    {
        vec2 tile_uv = fract(tile_uv_wrap * 0.5) * 2.0;
        vec2 mask_tex_uv = mask_tile_start_uv_and_size.xy + (tile_uv * mask_tile_start_uv_and_size.zw);
        return mask_tex_uv;
    }
    else
    {
        return tile_uv_wrap;
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

vec4 tex2Dtiled_mask_linearize(sampler2D tex, vec2 tex_uv)
{
    vec2 param = tex_uv;
    return tex2D_linearize(tex, param);
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
    mask_min_allowed_tile_size = ceil(mask_min_allowed_triad_size * mask_triads_per_tile);
    mask_min_expected_tile_size = mask_min_allowed_tile_size;
    pi_over_lobes = pi / mask_sinc_lobes;
    max_sinc_resize_samples_float = ((2.0 * mask_sinc_lobes) * mask_resize_src_lut_size.x) / mask_min_expected_tile_size;
    max_sinc_resize_samples_m4 = ceil(max_sinc_resize_samples_float * 0.25) * 4.0;
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
    vec2 param = LIBRA_VARYING_1;
    vec2 param_1 = LIBRA_UNIFORM_VERTICAL_SCANLINESSize.xy;
    vec2 param_2 = LIBRA_VARYING_4;
    vec3 scanline_color_dim = sample_rgb_scanline_horizontal(LIBRA_TEXTURE_VERTICAL_SCANLINES, param, param_1, param_2);
    float auto_dim_factor = levels_autodim_temp;
    vec2 tile_uv_wrap = LIBRA_VARYING_0 * LIBRA_VARYING_6;
    vec2 param_3 = tile_uv_wrap;
    vec4 param_4 = LIBRA_VARYING_5;
    vec2 mask_tex_uv = convert_phosphor_tile_uv_wrap_to_tex_uv(param_3, param_4);
    bool sample_orig_luts = get_mask_sample_mode() > 0.5;
    vec3 phosphor_mask_sample;
    if (sample_orig_luts)
    {
        if (LIBRA_UNIFORM_mask_type < 0.5)
        {
            vec2 param_5 = mask_tex_uv;
            phosphor_mask_sample = tex2D_linearize(LIBRA_TEXTURE_mask_grille_texture_large, param_5).xyz;
        }
        else
        {
            if (LIBRA_UNIFORM_mask_type < 1.5)
            {
                vec2 param_6 = mask_tex_uv;
                phosphor_mask_sample = tex2D_linearize(LIBRA_TEXTURE_mask_slot_texture_large, param_6).xyz;
            }
            else
            {
                vec2 param_7 = mask_tex_uv;
                phosphor_mask_sample = tex2D_linearize(LIBRA_TEXTURE_mask_shadow_texture_large, param_7).xyz;
            }
        }
    }
    else
    {
        vec2 param_8 = mask_tex_uv;
        phosphor_mask_sample = tex2Dtiled_mask_linearize(LIBRA_TEXTURE_MASK_RESIZE, param_8).xyz;
    }
    vec2 param_9 = LIBRA_VARYING_3;
    vec3 halation_color = tex2D_linearize(LIBRA_TEXTURE_HALATION_BLUR, param_9).xyz;
    vec3 halation_intensity_dim = vec3(dot(halation_color, vec3(auto_dim_factor / 3.0)));
    vec3 electron_intensity_dim = mix(scanline_color_dim, halation_intensity_dim, vec3(LIBRA_UNIFORM_halation_weight));
    vec3 phosphor_emission_dim = electron_intensity_dim * phosphor_mask_sample;
    vec3 pixel_color = phosphor_emission_dim;
    vec4 param_10 = vec4(pixel_color, 1.0);
    FragColor = encode_output(param_10);
}
