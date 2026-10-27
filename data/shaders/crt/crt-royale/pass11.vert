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
uniform vec4 LIBRA_UNIFORM_MASKED_SCANLINESSize;
uniform vec4 LIBRA_UNIFORM_HALATION_BLURSize;
uniform vec4 LIBRA_UNIFORM_BRIGHTPASSSize;

layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out vec4 LIBRA_VARYING_1;
out vec2 LIBRA_VARYING_2;
out vec4 LIBRA_VARYING_4;
out vec3 LIBRA_VARYING_5;
out vec3 LIBRA_VARYING_6;
out vec3 LIBRA_VARYING_7;
out vec3 LIBRA_VARYING_3;
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
float aa_pixel_diameter;
float aa_lanczos_lobes;
float aa_gauss_support;
float aa_tent_support;
vec2 aa_xy_axis_importance;
float aa_box_support;
float aa_cubic_support;
float aa_cubic_b;
float cubic_branch1_x3_coeff;
float cubic_branch1_x2_coeff;
float cubic_branch1_x0_coeff;
float cubic_branch2_x3_coeff;
float cubic_branch2_x2_coeff;
float cubic_branch2_x1_coeff;
float cubic_branch2_x0_coeff;

vec2 get_aspect_vector(float geom_aspect_ratio)
{
    float geom_clamped_aspect_ratio = min(geom_aspect_ratio, geom_max_aspect_ratio);
    vec2 geom_aspect = normalize(vec2(geom_clamped_aspect_ratio, 1.0));
    return geom_aspect;
}

vec2 get_geom_overscan_vector()
{
    return vec2(LIBRA_UNIFORM_geom_overscan_x, LIBRA_UNIFORM_geom_overscan_y);
}

vec2 get_geom_tilt_angle_vector()
{
    return vec2(LIBRA_UNIFORM_geom_tilt_angle_x, LIBRA_UNIFORM_geom_tilt_angle_y);
}

vec3 sphere_uv_to_xyz(vec2 video_uv, vec2 geom_aspect)
{
    vec2 square_uv = video_uv * geom_aspect;
    vec2 square_uv_unit = normalize(square_uv);
    float arc_len = square_uv.y / square_uv_unit.y;
    float angle_from_image_center = arc_len / LIBRA_UNIFORM_geom_radius;
    float xy_dist_from_sphere_center = sin(angle_from_image_center) * LIBRA_UNIFORM_geom_radius;
    vec2 xy_pos = square_uv_unit * xy_dist_from_sphere_center;
    float z_pos = cos(angle_from_image_center) * LIBRA_UNIFORM_geom_radius;
    vec3 intersection_pos_local = vec3(xy_pos.x, -xy_pos.y, z_pos);
    return intersection_pos_local;
}

vec3 sphere_alt_uv_to_xyz(vec2 video_uv, vec2 geom_aspect)
{
    vec2 square_uv = video_uv * geom_aspect;
    vec2 arc_len = square_uv;
    vec2 angle_from_image_center = arc_len / vec2(LIBRA_UNIFORM_geom_radius);
    vec2 xy_pos = sin(angle_from_image_center) * LIBRA_UNIFORM_geom_radius;
    float z_pos = sqrt((LIBRA_UNIFORM_geom_radius * LIBRA_UNIFORM_geom_radius) - dot(xy_pos, xy_pos));
    return vec3(xy_pos.x, -xy_pos.y, z_pos);
}

vec3 cylinder_uv_to_xyz(vec2 video_uv, vec2 geom_aspect)
{
    vec2 square_uv = video_uv * geom_aspect;
    float arc_len = square_uv.x;
    float angle_from_image_center = arc_len / LIBRA_UNIFORM_geom_radius;
    float x_pos = sin(angle_from_image_center) * LIBRA_UNIFORM_geom_radius;
    float z_pos = cos(angle_from_image_center) * LIBRA_UNIFORM_geom_radius;
    vec3 intersection_pos_local = vec3(x_pos, -square_uv.y, z_pos);
    return intersection_pos_local;
}

vec3 uv_to_xyz(vec2 uv, vec2 geom_aspect, float geom_mode)
{
    vec3 _430;
    if (geom_mode < 1.5)
    {
        vec2 param = uv;
        vec2 param_1 = geom_aspect;
        _430 = sphere_uv_to_xyz(param, param_1);
    }
    else
    {
        vec3 _441;
        if (geom_mode < 2.5)
        {
            vec2 param_2 = uv;
            vec2 param_3 = geom_aspect;
            _441 = sphere_alt_uv_to_xyz(param_2, param_3);
        }
        else
        {
            vec2 param_4 = uv;
            vec2 param_5 = geom_aspect;
            _441 = cylinder_uv_to_xyz(param_4, param_5);
        }
        _430 = _441;
    }
    return _430;
}

vec3 get_ideal_global_eye_pos_for_points(inout vec3 eye_pos, vec2 geom_aspect, vec3 global_coords[9], int num_points)
{
    int max_centering_iters = 1;
    vec3 eyespace_coords[9];
    float _642;
    float _655;
    float _668;
    float _681;
    for (int iter = 0; iter < max_centering_iters; iter++)
    {
        for (int i = 0; i < num_points; i++)
        {
            eyespace_coords[i] = global_coords[i] - eye_pos;
        }
        float abs_radius = abs(LIBRA_UNIFORM_geom_radius);
        vec2 offset_dr_min = vec2(10.0 * abs_radius, 10.0 * abs_radius);
        vec2 offset_ul_max = vec2((-10.0) * abs_radius, (-10.0) * abs_radius);
        for (int i_1 = 0; i_1 < num_points; i_1++)
        {
            vec2 flipy = vec2(1.0, -1.0);
            vec3 eyespace_xyz = eyespace_coords[i_1];
            vec2 offset_dr = eyespace_xyz.xy - ((vec2(-0.5) * (geom_aspect * (-eyespace_xyz.z))) / (flipy * LIBRA_UNIFORM_geom_view_dist));
            vec2 offset_ul = eyespace_xyz.xy - ((vec2(0.5) * (geom_aspect * (-eyespace_xyz.z))) / (flipy * LIBRA_UNIFORM_geom_view_dist));
            offset_dr_min = min(offset_dr_min, offset_dr);
            offset_ul_max = max(offset_ul_max, offset_ul);
        }
        vec2 center_offset = (offset_ul_max + offset_dr_min) * 0.5;
        vec3 _573 = eye_pos;
        vec2 _575 = _573.xy + center_offset;
        eye_pos.x = _575.x;
        eye_pos.y = _575.y;
        for (int i_2 = 0; i_2 < num_points; i_2++)
        {
            eyespace_coords[i_2] = global_coords[i_2] - eye_pos;
        }
        float offset_z_max = ((-10.0) * LIBRA_UNIFORM_geom_radius) * LIBRA_UNIFORM_geom_view_dist;
        for (int i_3 = 0; i_3 < num_points; i_3++)
        {
            vec3 eyespace_xyz_flipy = eyespace_coords[i_3] * vec3(1.0, -1.0, 1.0);
            vec4 offset_zzzz = eyespace_xyz_flipy.zzzz + ((eyespace_xyz_flipy.xyxy * LIBRA_UNIFORM_geom_view_dist) / (vec4(-0.5, -0.5, 0.5, 0.5) * vec4(geom_aspect.xyxy)));
            if (eyespace_xyz_flipy.x < 0.0)
            {
                _642 = max(offset_z_max, offset_zzzz.x);
            }
            else
            {
                _642 = offset_z_max;
            }
            offset_z_max = _642;
            if (eyespace_xyz_flipy.y < 0.0)
            {
                _655 = max(offset_z_max, offset_zzzz.y);
            }
            else
            {
                _655 = offset_z_max;
            }
            offset_z_max = _655;
            if (eyespace_xyz_flipy.x > 0.0)
            {
                _668 = max(offset_z_max, offset_zzzz.z);
            }
            else
            {
                _668 = offset_z_max;
            }
            offset_z_max = _668;
            if (eyespace_xyz_flipy.y > 0.0)
            {
                _681 = max(offset_z_max, offset_zzzz.w);
            }
            else
            {
                _681 = offset_z_max;
            }
            offset_z_max = _681;
            offset_z_max = max(offset_z_max, eyespace_xyz_flipy.z);
        }
        eye_pos.z += offset_z_max;
    }
    return eye_pos;
}

vec3 get_ideal_global_eye_pos(mat3 local_to_global, vec2 geom_aspect, float geom_mode)
{
    vec3 high_view = vec3(0.0, geom_aspect.y, -LIBRA_UNIFORM_geom_view_dist);
    vec3 low_view = high_view * vec3(1.0, -1.0, 1.0);
    float len_sq = dot(high_view, high_view);
    float fov = abs(acos(dot(high_view, low_view) / len_sq));
    float eye_z_spherical = LIBRA_UNIFORM_geom_radius / sin(fov * 0.5);
    vec3 _740;
    if (geom_mode < 2.5)
    {
        _740 = vec3(0.0, 0.0, eye_z_spherical);
    }
    else
    {
        _740 = vec3(0.0, 0.0, max(LIBRA_UNIFORM_geom_view_dist, eye_z_spherical));
    }
    vec3 eye_pos = _740;
    int num_points = 9;
    vec2 param = vec2(0.0);
    vec2 param_1 = geom_aspect;
    float param_2 = geom_mode;
    vec3 global_coords[9];
    global_coords[0] = uv_to_xyz(param, param_1, param_2) * local_to_global;
    vec2 param_3 = vec2(0.0, -0.5);
    vec2 param_4 = geom_aspect;
    float param_5 = geom_mode;
    global_coords[1] = uv_to_xyz(param_3, param_4, param_5) * local_to_global;
    vec2 param_6 = vec2(0.0, 0.5);
    vec2 param_7 = geom_aspect;
    float param_8 = geom_mode;
    global_coords[2] = uv_to_xyz(param_6, param_7, param_8) * local_to_global;
    vec2 param_9 = vec2(-0.5, 0.0);
    vec2 param_10 = geom_aspect;
    float param_11 = geom_mode;
    global_coords[3] = uv_to_xyz(param_9, param_10, param_11) * local_to_global;
    vec2 param_12 = vec2(0.5, 0.0);
    vec2 param_13 = geom_aspect;
    float param_14 = geom_mode;
    global_coords[4] = uv_to_xyz(param_12, param_13, param_14) * local_to_global;
    vec2 param_15 = vec2(-0.5);
    vec2 param_16 = geom_aspect;
    float param_17 = geom_mode;
    global_coords[5] = uv_to_xyz(param_15, param_16, param_17) * local_to_global;
    vec2 param_18 = vec2(0.5, -0.5);
    vec2 param_19 = geom_aspect;
    float param_20 = geom_mode;
    global_coords[6] = uv_to_xyz(param_18, param_19, param_20) * local_to_global;
    vec2 param_21 = vec2(-0.5, 0.5);
    vec2 param_22 = geom_aspect;
    float param_23 = geom_mode;
    global_coords[7] = uv_to_xyz(param_21, param_22, param_23) * local_to_global;
    vec2 param_24 = vec2(0.5);
    vec2 param_25 = geom_aspect;
    float param_26 = geom_mode;
    global_coords[8] = uv_to_xyz(param_24, param_25, param_26) * local_to_global;
    float num_negative_z_coords = 0.0;
    for (int i = 0; i < num_points; i++)
    {
        num_negative_z_coords += float(global_coords[0].z < 0.0);
    }
    vec3 _869;
    if (num_negative_z_coords > 0.5)
    {
        _869 = eye_pos;
    }
    else
    {
        vec3 param_27 = eye_pos;
        vec2 param_28 = geom_aspect;
        vec3 param_29[9] = global_coords;
        int param_30 = num_points;
        vec3 _882 = get_ideal_global_eye_pos_for_points(param_27, param_28, param_29, param_30);
        _869 = _882;
    }
    return _869;
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
    gamma_encode_output = true;
    gamma_aware_bilinear = !linearize_input;
    aa_pixel_diameter = 1.0;
    aa_lanczos_lobes = 3.0;
    aa_gauss_support = 1.0 / aa_pixel_diameter;
    aa_tent_support = 1.0 / aa_pixel_diameter;
    vec2 _251;
    if (aa_filter < 5.5)
    {
        _251 = vec2(1.0);
    }
    else
    {
        vec2 _258;
        if (aa_filter < 8.5)
        {
            _258 = vec2(1.0, 0.0);
        }
        else
        {
            vec2 _266;
            if (aa_filter < 9.5)
            {
                _266 = vec2(1.0, 1.0 / aa_lanczos_lobes);
            }
            else
            {
                _266 = vec2(1.0);
            }
            _258 = _266;
        }
        _251 = _258;
    }
    aa_xy_axis_importance = _251;
    aa_box_support = 0.5;
    aa_cubic_support = 2.0;
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord;
    LIBRA_VARYING_1 = vec4(1.0) / vec4(LIBRA_UNIFORM_SourceSize.xy, LIBRA_UNIFORM_SourceSize.xy);
    LIBRA_VARYING_2 = vec2(1.0) / LIBRA_UNIFORM_OutputSize.xy;
    float viewport_aspect_ratio = LIBRA_UNIFORM_OutputSize.x / LIBRA_UNIFORM_OutputSize.y;
    float param = viewport_aspect_ratio;
    vec2 geom_aspect = get_aspect_vector(param);
    vec2 geom_overscan = get_geom_overscan_vector();
    LIBRA_VARYING_4 = vec4(geom_aspect, geom_overscan);
    vec2 geom_tilt_angle = get_geom_tilt_angle_vector();
    vec2 sin_tilt = sin(geom_tilt_angle);
    vec2 cos_tilt = cos(geom_tilt_angle);
    mat3 rot_x_matrix = mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, cos_tilt.y, -sin_tilt.y), vec3(0.0, sin_tilt.y, cos_tilt.y));
    mat3 rot_y_matrix = mat3(vec3(cos_tilt.x, 0.0, sin_tilt.x), vec3(0.0, 1.0, 0.0), vec3(-sin_tilt.x, 0.0, cos_tilt.x));
    mat3 local_to_global = rot_x_matrix * rot_y_matrix;
    mat3 global_to_local = transpose(local_to_global);
    LIBRA_VARYING_5 = vec3(global_to_local[0].x, global_to_local[0].y, global_to_local[0].z);
    LIBRA_VARYING_6 = vec3(global_to_local[1].x, global_to_local[1].y, global_to_local[1].z);
    LIBRA_VARYING_7 = vec3(global_to_local[2].x, global_to_local[2].y, global_to_local[2].z);
    float geom_mode = LIBRA_UNIFORM_geom_mode_runtime;
    mat3 param_1 = local_to_global;
    vec2 param_2 = geom_aspect;
    float param_3 = geom_mode;
    vec3 eye_pos_global = get_ideal_global_eye_pos(param_1, param_2, param_3);
    LIBRA_VARYING_3 = eye_pos_global * global_to_local;
}
