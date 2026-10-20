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

uniform sampler2D LIBRA_TEXTURE_Source;

in vec4 LIBRA_VARYING_4;
in vec4 LIBRA_VARYING_1;
in vec3 LIBRA_VARYING_5;
in vec3 LIBRA_VARYING_6;
in vec3 LIBRA_VARYING_7;
in vec2 LIBRA_VARYING_0;
in vec3 LIBRA_VARYING_3;
in vec2 LIBRA_VARYING_2;
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

vec2 quadratic_solve(float a, float b_over_2, float c)
{
    float discriminant = (b_over_2 * b_over_2) - (a * c);
    float solution0 = c / ((-b_over_2) + sqrt(discriminant));
    return vec2(solution0, discriminant);
}

vec2 intersect_sphere(vec3 view_vec, vec3 eye_pos_vec)
{
    float a = dot(view_vec, view_vec);
    float b_over_2 = dot(view_vec, eye_pos_vec);
    float c = dot(eye_pos_vec, eye_pos_vec) - (LIBRA_UNIFORM_geom_radius * LIBRA_UNIFORM_geom_radius);
    float param = a;
    float param_1 = b_over_2;
    float param_2 = c;
    return quadratic_solve(param, param_1, param_2);
}

vec2 intersect_cylinder(vec3 view_vec, vec3 eye_pos_vec)
{
    vec3 cylinder_top_vec = vec3(0.0, LIBRA_UNIFORM_geom_radius, 0.0);
    vec3 cylinder_axis_vec = vec3(0.0, 1.0, 0.0);
    vec3 top_to_eye_vec = eye_pos_vec - cylinder_top_vec;
    vec3 axis_x_view = cross(cylinder_axis_vec, view_vec);
    vec3 axis_x_top_to_eye = cross(cylinder_axis_vec, top_to_eye_vec);
    float a = dot(axis_x_view, axis_x_view);
    float b_over_2 = dot(axis_x_top_to_eye, axis_x_view);
    float c = dot(axis_x_top_to_eye, axis_x_top_to_eye) - (LIBRA_UNIFORM_geom_radius * LIBRA_UNIFORM_geom_radius);
    float param = a;
    float param_1 = b_over_2;
    float param_2 = c;
    return quadratic_solve(param, param_1, param_2);
}

vec2 intersect(vec3 view_vec_local, vec3 eye_pos_local, float geom_mode)
{
    vec2 _4867;
    if (geom_mode < 2.5)
    {
        vec3 param = view_vec_local;
        vec3 param_1 = eye_pos_local;
        _4867 = intersect_sphere(param, param_1);
    }
    else
    {
        vec3 param_2 = view_vec_local;
        vec3 param_3 = eye_pos_local;
        _4867 = intersect_cylinder(param_2, param_3);
    }
    return _4867;
}

vec2 sphere_xyz_to_uv(vec3 intersection_pos_local, vec2 geom_aspect)
{
    vec3 image_center_pos_local = vec3(0.0, 0.0, LIBRA_UNIFORM_geom_radius);
    float cp_len = length(cross(intersection_pos_local, image_center_pos_local));
    float dp = dot(intersection_pos_local, image_center_pos_local);
    float angle_from_image_center = atan(cp_len, dp);
    float arc_len = angle_from_image_center * LIBRA_UNIFORM_geom_radius;
    vec2 square_uv_unit = normalize(vec2(intersection_pos_local.x, -intersection_pos_local.y));
    vec2 square_uv = square_uv_unit * arc_len;
    vec2 video_uv = square_uv / geom_aspect;
    return video_uv;
}

vec2 sphere_alt_xyz_to_uv(vec3 intersection_pos_local, vec2 geom_aspect)
{
    vec2 angle_from_image_center = atan(vec2(intersection_pos_local.x, -intersection_pos_local.y), intersection_pos_local.zz);
    vec2 signed_arc_len = angle_from_image_center * LIBRA_UNIFORM_geom_radius;
    vec2 video_uv = signed_arc_len / geom_aspect;
    return video_uv;
}

vec2 cylinder_xyz_to_uv(vec3 intersection_pos_local, vec2 geom_aspect)
{
    float angle_from_image_center = atan(intersection_pos_local.x, intersection_pos_local.z);
    float signed_arc_len = angle_from_image_center * LIBRA_UNIFORM_geom_radius;
    vec2 square_uv = vec2(signed_arc_len, -intersection_pos_local.y);
    vec2 video_uv = square_uv / geom_aspect;
    return video_uv;
}

vec2 xyz_to_uv(vec3 intersection_pos_local, vec2 geom_aspect, float geom_mode)
{
    vec2 _4886;
    if (geom_mode < 1.5)
    {
        vec3 param = intersection_pos_local;
        vec2 param_1 = geom_aspect;
        _4886 = sphere_xyz_to_uv(param, param_1);
    }
    else
    {
        vec2 _4897;
        if (geom_mode < 2.5)
        {
            vec3 param_2 = intersection_pos_local;
            vec2 param_3 = geom_aspect;
            _4897 = sphere_alt_xyz_to_uv(param_2, param_3);
        }
        else
        {
            vec3 param_4 = intersection_pos_local;
            vec2 param_5 = geom_aspect;
            _4897 = cylinder_xyz_to_uv(param_4, param_5);
        }
        _4886 = _4897;
    }
    return _4886;
}

vec2 view_vec_to_uv(vec3 view_vec_local, vec3 eye_pos_local, vec2 geom_aspect, float geom_mode, out vec3 intersection_pos)
{
    vec3 param = view_vec_local;
    vec3 param_1 = eye_pos_local;
    float param_2 = geom_mode;
    vec2 intersect_dist_and_discriminant = intersect(param, param_1, param_2);
    vec3 intersection_pos_local = eye_pos_local + (view_vec_local * intersect_dist_and_discriminant.x);
    intersection_pos = intersection_pos_local;
    vec2 _4935;
    if (intersect_dist_and_discriminant.y > 0.004999999888241291046142578125)
    {
        vec3 param_3 = intersection_pos_local;
        vec2 param_4 = geom_aspect;
        float param_5 = geom_mode;
        _4935 = xyz_to_uv(param_3, param_4, param_5);
    }
    else
    {
        _4935 = vec2(1.0);
    }
    return _4935;
}

mat3 get_pixel_to_object_matrix(mat3 global_to_local, vec3 eye_pos_local, vec3 view_vec_global, vec3 intersection_pos_local, vec3 normal, vec2 output_size_inv)
{
    vec3 pos = intersection_pos_local;
    vec3 eye_pos = eye_pos_local;
    vec3 view_vec_right_global = view_vec_global + vec3(output_size_inv.x, 0.0, 0.0);
    vec3 view_vec_down_global = view_vec_global + vec3(0.0, -output_size_inv.y, 0.0);
    vec3 view_vec_right_local = view_vec_right_global * global_to_local;
    vec3 view_vec_down_local = view_vec_down_global * global_to_local;
    vec3 intersection_vec_dot_normal = vec3(dot(pos - eye_pos, normal), dot(pos - eye_pos, normal), dot(pos - eye_pos, normal));
    vec3 right_pos = eye_pos + ((intersection_vec_dot_normal / vec3(dot(view_vec_right_local, normal))) * view_vec_right_local);
    vec3 down_pos = eye_pos + ((intersection_vec_dot_normal / vec3(dot(view_vec_down_local, normal))) * view_vec_down_local);
    vec3 object_right_vec = right_pos - pos;
    vec3 object_down_vec = down_pos - pos;
    mat3 pixel_to_object = mat3(vec3(object_right_vec.x, object_down_vec.x, 0.0), vec3(object_right_vec.y, object_down_vec.y, 0.0), vec3(object_right_vec.z, object_down_vec.z, 0.0));
    return pixel_to_object;
}

mat3 get_object_to_tangent_matrix(vec3 intersection_pos_local, vec3 normal, vec2 geom_aspect, float geom_mode)
{
    vec3 pos = intersection_pos_local;
    vec3 x_vec = vec3(1.0, 0.0, 0.0);
    vec3 y_vec = vec3(0.0, 1.0, 0.0);
    vec3 cotangent_unscaled;
    vec3 cobitangent_unscaled;
    if (geom_mode < 1.5)
    {
        cotangent_unscaled = normalize(cross(y_vec, pos)) * geom_aspect.y;
        cobitangent_unscaled = normalize(cross(x_vec, pos)) * geom_aspect.x;
    }
    else
    {
        if (geom_mode < 2.5)
        {
            vec3 tangent = normalize(cross(y_vec, vec3(pos.x, 0.0, pos.z))) * geom_aspect.x;
            vec3 bitangent = normalize(cross(x_vec, vec3(0.0, pos.yz))) * geom_aspect.y;
            cotangent_unscaled = cross(normal, bitangent);
            cobitangent_unscaled = cross(tangent, normal);
        }
        else
        {
            cotangent_unscaled = cross(y_vec, normal) * geom_aspect.y;
            cobitangent_unscaled = vec3(0.0, -geom_aspect.x, 0.0);
        }
    }
    vec3 computed_normal = cross(cobitangent_unscaled, cotangent_unscaled);
    float inv_determinant = inversesqrt(dot(computed_normal, computed_normal));
    vec3 cotangent = cotangent_unscaled * inv_determinant;
    vec3 cobitangent = cobitangent_unscaled * inv_determinant;
    mat3 object_to_tangent = mat3(vec3(cotangent), vec3(cobitangent), vec3(normal));
    return object_to_tangent;
}

vec2 get_curved_video_uv_coords_and_tangent_matrix(vec2 flat_video_uv, vec3 eye_pos_local, vec2 output_size_inv, vec2 geom_aspect, float geom_mode, mat3 global_to_local, out mat2 pixel_to_tangent_video_uv)
{
    vec2 view_uv = (flat_video_uv - vec2(0.5)) * geom_aspect;
    vec3 view_vec_global = vec3(view_uv.x, -view_uv.y, -LIBRA_UNIFORM_geom_view_dist);
    vec3 view_vec_local = view_vec_global * global_to_local;
    vec3 param = view_vec_local;
    vec3 param_1 = eye_pos_local;
    vec2 param_2 = geom_aspect;
    float param_3 = geom_mode;
    vec3 param_4;
    vec2 _5181 = view_vec_to_uv(param, param_1, param_2, param_3, param_4);
    vec3 pos = param_4;
    vec2 centered_uv = _5181;
    vec2 video_uv = centered_uv + vec2(0.5);
    if (geom_force_correct_tangent_matrix)
    {
        vec3 _5192;
        if (geom_mode < 2.5)
        {
            _5192 = pos;
        }
        else
        {
            _5192 = vec3(pos.x, 0.0, pos.z);
        }
        vec3 normal_base = _5192;
        vec3 normal = normalize(normal_base);
        mat3 param_5 = global_to_local;
        vec3 param_6 = eye_pos_local;
        vec3 param_7 = view_vec_global;
        vec3 param_8 = pos;
        vec3 param_9 = normal;
        vec2 param_10 = output_size_inv;
        mat3 pixel_to_object = get_pixel_to_object_matrix(param_5, param_6, param_7, param_8, param_9, param_10);
        vec3 param_11 = pos;
        vec3 param_12 = normal;
        vec2 param_13 = geom_aspect;
        float param_14 = geom_mode;
        mat3 object_to_tangent = get_object_to_tangent_matrix(param_11, param_12, param_13, param_14);
        mat3 pixel_to_tangent3x3 = pixel_to_object * object_to_tangent;
        pixel_to_tangent_video_uv = mat2(vec2(pixel_to_tangent3x3[0].x, pixel_to_tangent3x3[0].y), vec2(pixel_to_tangent3x3[1].x, pixel_to_tangent3x3[1].y));
    }
    else
    {
        pixel_to_tangent_video_uv = mat2(vec2(output_size_inv.x, 0.0), vec2(0.0, output_size_inv.y));
    }
    return video_uv;
}

mat2 mul_scale(vec2 scale, mat2 matrix)
{
    vec4 temp_matrix = vec4(matrix[0].x, matrix[0].y, matrix[1].x, matrix[1].y) * scale.xxyy;
    return mat2(vec2(temp_matrix.x, temp_matrix.y), vec2(temp_matrix.z, temp_matrix.w));
}

vec2 get_aa_subpixel_r_offset()
{
    return aa_subpixel_r_offset_static;
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

vec3 tex2Daa_subpixel_weights_only(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv)
{
    vec2 aa_r_offset = get_aa_subpixel_r_offset();
    vec2 aa_r_offset_uv_offset = aa_r_offset * pixel_to_tex_uv;
    vec2 param = tex_uv;
    float color_g = tex2D_linearize(tex, param).y;
    vec2 param_1 = tex_uv + aa_r_offset_uv_offset;
    float color_r = tex2D_linearize(tex, param_1).x;
    vec2 param_2 = tex_uv - aa_r_offset_uv_offset;
    float color_b = tex2D_linearize(tex, param_2).z;
    return vec3(color_r, color_g, color_b);
}

void assign_aa_cubic_constants()
{
    if ((aa_filter > 5.5) && (aa_filter < 7.5))
    {
        aa_cubic_b = 1.0 - (2.0 * LIBRA_UNIFORM_aa_cubic_c);
        cubic_branch1_x3_coeff = (12.0 - (9.0 * aa_cubic_b)) - (6.0 * LIBRA_UNIFORM_aa_cubic_c);
        cubic_branch1_x2_coeff = ((-18.0) + (12.0 * aa_cubic_b)) + (6.0 * LIBRA_UNIFORM_aa_cubic_c);
        cubic_branch1_x0_coeff = 6.0 - (2.0 * aa_cubic_b);
        cubic_branch2_x3_coeff = (-aa_cubic_b) - (6.0 * LIBRA_UNIFORM_aa_cubic_c);
        cubic_branch2_x2_coeff = (6.0 * aa_cubic_b) + (30.0 * LIBRA_UNIFORM_aa_cubic_c);
        cubic_branch2_x1_coeff = ((-12.0) * aa_cubic_b) - (48.0 * LIBRA_UNIFORM_aa_cubic_c);
        cubic_branch2_x0_coeff = (8.0 * aa_cubic_b) + (24.0 * LIBRA_UNIFORM_aa_cubic_c);
    }
}

vec4 get_subpixel_support_diam_and_final_axis_importance()
{
    float _641;
    if (aa_filter < 1.5)
    {
        _641 = aa_box_support;
    }
    else
    {
        float _648;
        if (aa_filter < 3.5)
        {
            _648 = aa_tent_support;
        }
        else
        {
            float _655;
            if (aa_filter < 5.5)
            {
                _655 = aa_gauss_support;
            }
            else
            {
                float _662;
                if (aa_filter < 7.5)
                {
                    _662 = aa_cubic_support;
                }
                else
                {
                    _662 = (aa_filter < 9.5) ? aa_lanczos_lobes : aa_box_support;
                }
                _655 = _662;
            }
            _648 = _655;
        }
        _641 = _648;
    }
    float base_support_radius = _641;
    vec2 subpixel_support_radius_raw = vec2(base_support_radius) + abs(get_aa_subpixel_r_offset());
    if (aa_filter < 1.5)
    {
        vec2 subpixel_support_diam = subpixel_support_radius_raw * 2.0;
        vec2 final_axis_importance = vec2(1.0);
        return vec4(subpixel_support_diam, final_axis_importance);
    }
    else
    {
        vec2 subpixel_support_radius = max(vec2(aa_box_support), subpixel_support_radius_raw * aa_xy_axis_importance);
        vec2 final_axis_importance_1 = (aa_xy_axis_importance * subpixel_support_radius_raw) / subpixel_support_radius;
        vec2 subpixel_support_diam_1 = subpixel_support_radius * 2.0;
        return vec4(subpixel_support_diam_1, final_axis_importance_1);
    }
}

float eval_separable_box_filter(vec2 offset)
{
    return float(all(bvec2(abs(offset.x) <= aa_box_support, abs(offset.y) <= aa_box_support)));
}

float eval_box_filter(float dist)
{
    return float(abs(dist) <= aa_box_support);
}

float eval_tent_filter(float dist)
{
    return clamp((aa_tent_support - dist) / aa_tent_support, 0.0, 1.0);
}

float eval_gaussian_filter(float dist)
{
    return exp((-(dist * dist)) / ((2.0 * LIBRA_UNIFORM_aa_gauss_sigma) * LIBRA_UNIFORM_aa_gauss_sigma));
}

float eval_cubic_filter(float dist)
{
    float abs_dist = abs(dist);
    float _777;
    if (abs_dist < 1.0)
    {
        _777 = ((((cubic_branch1_x3_coeff * abs_dist) + cubic_branch1_x2_coeff) * abs_dist) * abs_dist) + cubic_branch1_x0_coeff;
    }
    else
    {
        float _794;
        if (abs_dist < 2.0)
        {
            _794 = (((((cubic_branch2_x3_coeff * abs_dist) + cubic_branch2_x2_coeff) * abs_dist) + cubic_branch2_x1_coeff) * abs_dist) + cubic_branch2_x0_coeff;
        }
        else
        {
            _794 = 0.0;
        }
        _777 = _794;
    }
    return _777 / 6.0;
}

vec2 eval_sinc_filter(vec2 offset)
{
    vec2 pi_offset = offset * pi;
    return sin(pi_offset) / pi_offset;
}

float eval_separable_lanczos_sinc_filter(vec2 offset_unsafe)
{
    vec2 offset = max(abs(offset_unsafe), vec2(1.52587890625e-05));
    vec2 param = offset;
    vec2 param_1 = offset / vec2(aa_lanczos_lobes);
    vec2 xy_weights = eval_sinc_filter(param) * eval_sinc_filter(param_1);
    return xy_weights.x * xy_weights.y;
}

float eval_jinc_filter_unorm(float x)
{
    float point3845_x = 0.384485661983489990234375 * x;
    float exp_term = exp(-(point3845_x * point3845_x));
    float point8154_plus_x = 0.81536233425140380859375 + x;
    float cos_term = cos(point8154_plus_x);
    return (((0.02647273242473602294921875 * min(x, 6.831349849700927734375)) + (0.6808235645294189453125 * exp_term)) + ((((-0.05972559750080108642578125) * min(7.41043186187744140625, x)) * cos_term) / ((point8154_plus_x + (0.06460745632648468017578125 * (x * x))) + (cos(x) * max(exp_term, cos(x) + cos_term))))) - 0.18083749711513519287109375;
}

float eval_jinc_filter(float dist)
{
    float param = pi * dist;
    return eval_jinc_filter_unorm(param);
}

float eval_lanczos_jinc_filter(float dist)
{
    float param = dist;
    float param_1 = dist / aa_lanczos_lobes;
    return eval_jinc_filter(param) * eval_jinc_filter(param_1);
}

vec3 eval_unorm_rgb_weights(vec2 offset, vec2 final_axis_importance)
{
    vec2 offset_g = offset * final_axis_importance;
    vec2 aa_r_offset = get_aa_subpixel_r_offset();
    vec2 offset_r = offset_g - (aa_r_offset * final_axis_importance);
    vec2 offset_b = offset_g + (aa_r_offset * final_axis_importance);
    if (aa_filter < 0.5)
    {
        vec2 param = offset_r;
        vec2 param_1 = offset_g;
        vec2 param_2 = offset_b;
        return vec3(eval_separable_box_filter(param), eval_separable_box_filter(param_1), eval_separable_box_filter(param_2));
    }
    else
    {
        if (aa_filter < 1.5)
        {
            float param_3 = length(offset_r);
            float param_4 = length(offset_g);
            float param_5 = length(offset_b);
            return vec3(eval_box_filter(param_3), eval_box_filter(param_4), eval_box_filter(param_5));
        }
        else
        {
            if (aa_filter < 2.5)
            {
                float param_6 = offset_r.x;
                float param_7 = offset_r.y;
                float param_8 = offset_g.x;
                float param_9 = offset_g.y;
                float param_10 = offset_b.x;
                float param_11 = offset_b.y;
                return vec3(eval_tent_filter(param_6) * eval_tent_filter(param_7), eval_tent_filter(param_8) * eval_tent_filter(param_9), eval_tent_filter(param_10) * eval_tent_filter(param_11));
            }
            else
            {
                if (aa_filter < 3.5)
                {
                    float param_12 = length(offset_r);
                    float param_13 = length(offset_g);
                    float param_14 = length(offset_b);
                    return vec3(eval_tent_filter(param_12), eval_tent_filter(param_13), eval_tent_filter(param_14));
                }
                else
                {
                    if (aa_filter < 4.5)
                    {
                        float param_15 = offset_r.x;
                        float param_16 = offset_r.y;
                        float param_17 = offset_g.x;
                        float param_18 = offset_g.y;
                        float param_19 = offset_b.x;
                        float param_20 = offset_b.y;
                        return vec3(eval_gaussian_filter(param_15) * eval_gaussian_filter(param_16), eval_gaussian_filter(param_17) * eval_gaussian_filter(param_18), eval_gaussian_filter(param_19) * eval_gaussian_filter(param_20));
                    }
                    else
                    {
                        if (aa_filter < 5.5)
                        {
                            float param_21 = length(offset_r);
                            float param_22 = length(offset_g);
                            float param_23 = length(offset_b);
                            return vec3(eval_gaussian_filter(param_21), eval_gaussian_filter(param_22), eval_gaussian_filter(param_23));
                        }
                        else
                        {
                            if (aa_filter < 6.5)
                            {
                                float param_24 = offset_r.x;
                                float param_25 = offset_r.y;
                                float param_26 = offset_g.x;
                                float param_27 = offset_g.y;
                                float param_28 = offset_b.x;
                                float param_29 = offset_b.y;
                                return vec3(eval_cubic_filter(param_24) * eval_cubic_filter(param_25), eval_cubic_filter(param_26) * eval_cubic_filter(param_27), eval_cubic_filter(param_28) * eval_cubic_filter(param_29));
                            }
                            else
                            {
                                if (aa_filter < 7.5)
                                {
                                    float param_30 = length(offset_r);
                                    float param_31 = length(offset_g);
                                    float param_32 = length(offset_b);
                                    return vec3(eval_cubic_filter(param_30), eval_cubic_filter(param_31), eval_cubic_filter(param_32));
                                }
                                else
                                {
                                    if (aa_filter < 8.5)
                                    {
                                        vec2 param_33 = offset_r;
                                        vec2 param_34 = offset_g;
                                        vec2 param_35 = offset_b;
                                        return vec3(eval_separable_lanczos_sinc_filter(param_33), eval_separable_lanczos_sinc_filter(param_34), eval_separable_lanczos_sinc_filter(param_35));
                                    }
                                    else
                                    {
                                        if (aa_filter < 9.5)
                                        {
                                            float param_36 = length(offset_r);
                                            float param_37 = length(offset_g);
                                            float param_38 = length(offset_b);
                                            return vec3(eval_lanczos_jinc_filter(param_36), eval_lanczos_jinc_filter(param_37), eval_lanczos_jinc_filter(param_38));
                                        }
                                        else
                                        {
                                            vec2 param_39 = offset_r;
                                            vec2 param_40 = offset_g;
                                            vec2 param_41 = offset_b;
                                            return vec3(eval_separable_box_filter(param_39), eval_separable_box_filter(param_40), eval_separable_box_filter(param_41));
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

vec2 get_frame_sign(float frame)
{
    if (aa_temporal)
    {
        float frame_odd = float(mod(frame, 2.0) > 0.5);
        vec2 aa_r_offset = get_aa_subpixel_r_offset();
        vec2 mirror = -vec2(float(abs(aa_r_offset.x) < 1.52587890625e-05), float(abs(aa_r_offset.y) < 1.52587890625e-05));
        return mirror;
    }
    else
    {
        return vec2(1.0);
    }
}

vec4 tex2Daa_tiled_linearize(sampler2D samp, vec2 s)
{
    vec2 param = s;
    return tex2D_linearize(samp, param);
}

vec3 tex2Daa4x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 4.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5), 0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(2.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(0.0, 1.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec3 w2 = w1.zyx;
    vec3 w3 = w0.zyx;
    vec3 half_sum = w0 + w1;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _1322 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_1322[0], _1322[1]);
    float param_4 = frame;
    vec2 frame_sign = get_frame_sign(param_4);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_5 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_5).xyz;
    vec2 param_6 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_6).xyz;
    vec2 param_7 = tex_uv - uv_offset1;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_7).xyz;
    vec2 param_8 = tex_uv - uv_offset0;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_8).xyz;
    return w_sum_inv * ((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3));
}

vec3 tex2Daa5x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 5.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(1.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(4.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(2.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec3 w3 = w1.zyx;
    vec3 w4 = w0.zyx;
    vec3 w_sum_inv = vec3(1.0) / ((((w0 + w1) + w2) + w3) + w4);
    mat2 _1468 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_1468[0], _1468[1]);
    float param_6 = frame;
    vec2 frame_sign = get_frame_sign(param_6);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_7 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_7).xyz;
    vec2 param_8 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_8).xyz;
    vec2 param_9 = tex_uv;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_9).xyz;
    vec2 param_10 = tex_uv - uv_offset1;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_10).xyz;
    vec2 param_11 = tex_uv - uv_offset0;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_11).xyz;
    return w_sum_inv * (((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4));
}

vec3 tex2Daa6x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 6.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(4.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(2.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(0.0, 2.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec3 w3 = w2.zyx;
    vec3 w4 = w1.zyx;
    vec3 w5 = w0.zyx;
    vec3 half_sum = (w0 + w1) + w2;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _1629 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_1629[0], _1629[1]);
    float param_6 = frame;
    vec2 frame_sign = get_frame_sign(param_6);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_7 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_7).xyz;
    vec2 param_8 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_8).xyz;
    vec2 param_9 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_9).xyz;
    vec2 param_10 = tex_uv - uv_offset2;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_10).xyz;
    vec2 param_11 = tex_uv - uv_offset1;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_11).xyz;
    vec2 param_12 = tex_uv - uv_offset0;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_12).xyz;
    return w_sum_inv * ((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5));
}

vec3 tex2Daa7x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 7.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(1.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(4.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(0.0, 2.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(3.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec3 w4 = w2.zyx;
    vec3 w5 = w1.zyx;
    vec3 w6 = w0.zyx;
    vec3 half_sum = (w0 + w1) + w2;
    vec3 w_sum = (half_sum + half_sum.zyx) + w3;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _1821 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_1821[0], _1821[1]);
    float param_8 = frame;
    vec2 frame_sign = get_frame_sign(param_8);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_9 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_9).xyz;
    vec2 param_10 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_10).xyz;
    vec2 param_11 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_11).xyz;
    vec2 param_12 = tex_uv;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_12).xyz;
    vec2 param_13 = tex_uv - uv_offset2;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_13).xyz;
    vec2 param_14 = tex_uv - uv_offset1;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_14).xyz;
    vec2 param_15 = tex_uv - uv_offset0;
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_15).xyz;
    return w_sum_inv * (((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6));
}

vec3 tex2Daa8x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 8.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(2.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(4.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(1.0, 2.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(7.0, 3.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec3 w4 = w3.zyx;
    vec3 w5 = w2.zyx;
    vec3 w6 = w1.zyx;
    vec3 w7 = w0.zyx;
    vec3 half_sum = ((w0 + w1) + w2) + w3;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _2025 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_2025[0], _2025[1]);
    float param_8 = frame;
    vec2 frame_sign = get_frame_sign(param_8);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset3 = (xy_offset3 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_9 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_9).xyz;
    vec2 param_10 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_10).xyz;
    vec2 param_11 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_11).xyz;
    vec2 param_12 = tex_uv + uv_offset3;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_12).xyz;
    vec2 param_13 = tex_uv - uv_offset3;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_13).xyz;
    vec2 param_14 = tex_uv - uv_offset2;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_14).xyz;
    vec2 param_15 = tex_uv - uv_offset1;
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_15).xyz;
    vec2 param_16 = tex_uv - uv_offset0;
    vec3 sample7 = tex2Daa_tiled_linearize(tex, param_16).xyz;
    return w_sum_inv * ((((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6)) + (w7 * sample7));
}

vec3 tex2Daa12x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 12.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(3.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(9.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(6.0, 2.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(1.0, 3.0) * xy_step);
    vec2 xy_offset4 = xy_start_offset + (vec2(11.0, 4.0) * xy_step);
    vec2 xy_offset5 = xy_start_offset + (vec2(4.0, 5.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec2 param_8 = xy_offset4;
    vec2 param_9 = final_axis_importance;
    vec3 w4 = eval_unorm_rgb_weights(param_8, param_9);
    vec2 param_10 = xy_offset5;
    vec2 param_11 = final_axis_importance;
    vec3 w5 = eval_unorm_rgb_weights(param_10, param_11);
    vec3 w6 = w5.zyx;
    vec3 w7 = w4.zyx;
    vec3 w8 = w3.zyx;
    vec3 w9 = w2.zyx;
    vec3 w10 = w1.zyx;
    vec3 w11 = w0.zyx;
    vec3 half_sum = ((((w0 + w1) + w2) + w3) + w4) + w5;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _2285 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_2285[0], _2285[1]);
    float param_12 = frame;
    vec2 frame_sign = get_frame_sign(param_12);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset3 = (xy_offset3 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset4 = (xy_offset4 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset5 = (xy_offset5 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_13 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_13).xyz;
    vec2 param_14 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_14).xyz;
    vec2 param_15 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_15).xyz;
    vec2 param_16 = tex_uv + uv_offset3;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_16).xyz;
    vec2 param_17 = tex_uv + uv_offset4;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_17).xyz;
    vec2 param_18 = tex_uv + uv_offset5;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_18).xyz;
    vec2 param_19 = tex_uv - uv_offset5;
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_19).xyz;
    vec2 param_20 = tex_uv - uv_offset4;
    vec3 sample7 = tex2Daa_tiled_linearize(tex, param_20).xyz;
    vec2 param_21 = tex_uv - uv_offset3;
    vec3 sample8 = tex2Daa_tiled_linearize(tex, param_21).xyz;
    vec2 param_22 = tex_uv - uv_offset2;
    vec3 sample9 = tex2Daa_tiled_linearize(tex, param_22).xyz;
    vec2 param_23 = tex_uv - uv_offset1;
    vec3 sample10 = tex2Daa_tiled_linearize(tex, param_23).xyz;
    vec2 param_24 = tex_uv - uv_offset0;
    vec3 sample11 = tex2Daa_tiled_linearize(tex, param_24).xyz;
    return w_sum_inv * ((((((((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6)) + (w7 * sample7)) + (w8 * sample8)) + (w9 * sample9)) + (w10 * sample10)) + (w11 * sample11));
}

vec3 tex2Daa16x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 16.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(2.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(9.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(12.0, 2.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(4.0, 3.0) * xy_step);
    vec2 xy_offset4 = xy_start_offset + (vec2(8.0, 4.0) * xy_step);
    vec2 xy_offset5 = xy_start_offset + (vec2(14.0, 5.0) * xy_step);
    vec2 xy_offset6 = xy_start_offset + (vec2(0.0, 6.0) * xy_step);
    vec2 xy_offset7 = xy_start_offset + (vec2(10.0, 7.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec2 param_8 = xy_offset4;
    vec2 param_9 = final_axis_importance;
    vec3 w4 = eval_unorm_rgb_weights(param_8, param_9);
    vec2 param_10 = xy_offset5;
    vec2 param_11 = final_axis_importance;
    vec3 w5 = eval_unorm_rgb_weights(param_10, param_11);
    vec2 param_12 = xy_offset6;
    vec2 param_13 = final_axis_importance;
    vec3 w6 = eval_unorm_rgb_weights(param_12, param_13);
    vec2 param_14 = xy_offset7;
    vec2 param_15 = final_axis_importance;
    vec3 w7 = eval_unorm_rgb_weights(param_14, param_15);
    vec3 w8 = w7.zyx;
    vec3 w9 = w6.zyx;
    vec3 w10 = w5.zyx;
    vec3 w11 = w4.zyx;
    vec3 w12 = w3.zyx;
    vec3 w13 = w2.zyx;
    vec3 w14 = w1.zyx;
    vec3 w15 = w0.zyx;
    vec3 half_sum = ((((((w0 + w1) + w2) + w3) + w4) + w5) + w6) + w7;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _2635 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_2635[0], _2635[1]);
    float param_16 = frame;
    vec2 frame_sign = get_frame_sign(param_16);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset3 = (xy_offset3 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset4 = (xy_offset4 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset5 = (xy_offset5 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset6 = (xy_offset6 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset7 = (xy_offset7 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_17 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_17).xyz;
    vec2 param_18 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_18).xyz;
    vec2 param_19 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_19).xyz;
    vec2 param_20 = tex_uv + uv_offset3;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_20).xyz;
    vec2 param_21 = tex_uv + uv_offset4;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_21).xyz;
    vec2 param_22 = tex_uv + uv_offset5;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_22).xyz;
    vec2 param_23 = tex_uv + uv_offset6;
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_23).xyz;
    vec2 param_24 = tex_uv + uv_offset7;
    vec3 sample7 = tex2Daa_tiled_linearize(tex, param_24).xyz;
    vec2 param_25 = tex_uv - uv_offset7;
    vec3 sample8 = tex2Daa_tiled_linearize(tex, param_25).xyz;
    vec2 param_26 = tex_uv - uv_offset6;
    vec3 sample9 = tex2Daa_tiled_linearize(tex, param_26).xyz;
    vec2 param_27 = tex_uv - uv_offset5;
    vec3 sample10 = tex2Daa_tiled_linearize(tex, param_27).xyz;
    vec2 param_28 = tex_uv - uv_offset4;
    vec3 sample11 = tex2Daa_tiled_linearize(tex, param_28).xyz;
    vec2 param_29 = tex_uv - uv_offset3;
    vec3 sample12 = tex2Daa_tiled_linearize(tex, param_29).xyz;
    vec2 param_30 = tex_uv - uv_offset2;
    vec3 sample13 = tex2Daa_tiled_linearize(tex, param_30).xyz;
    vec2 param_31 = tex_uv - uv_offset1;
    vec3 sample14 = tex2Daa_tiled_linearize(tex, param_31).xyz;
    vec2 param_32 = tex_uv - uv_offset0;
    vec3 sample15 = tex2Daa_tiled_linearize(tex, param_32).xyz;
    return w_sum_inv * ((((((((((((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6)) + (w7 * sample7)) + (w8 * sample8)) + (w9 * sample9)) + (w10 * sample10)) + (w11 * sample11)) + (w12 * sample12)) + (w13 * sample13)) + (w14 * sample14)) + (w15 * sample15));
}

vec3 tex2Daa20x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 20.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(7.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(16.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(11.0, 2.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(1.0, 3.0) * xy_step);
    vec2 xy_offset4 = xy_start_offset + (vec2(5.0, 4.0) * xy_step);
    vec2 xy_offset5 = xy_start_offset + (vec2(15.0, 5.0) * xy_step);
    vec2 xy_offset6 = xy_start_offset + (vec2(10.0, 6.0) * xy_step);
    vec2 xy_offset7 = xy_start_offset + (vec2(19.0, 7.0) * xy_step);
    vec2 xy_offset8 = xy_start_offset + (vec2(2.0, 8.0) * xy_step);
    vec2 xy_offset9 = xy_start_offset + (vec2(6.0, 9.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec2 param_8 = xy_offset4;
    vec2 param_9 = final_axis_importance;
    vec3 w4 = eval_unorm_rgb_weights(param_8, param_9);
    vec2 param_10 = xy_offset5;
    vec2 param_11 = final_axis_importance;
    vec3 w5 = eval_unorm_rgb_weights(param_10, param_11);
    vec2 param_12 = xy_offset6;
    vec2 param_13 = final_axis_importance;
    vec3 w6 = eval_unorm_rgb_weights(param_12, param_13);
    vec2 param_14 = xy_offset7;
    vec2 param_15 = final_axis_importance;
    vec3 w7 = eval_unorm_rgb_weights(param_14, param_15);
    vec2 param_16 = xy_offset8;
    vec2 param_17 = final_axis_importance;
    vec3 w8 = eval_unorm_rgb_weights(param_16, param_17);
    vec2 param_18 = xy_offset9;
    vec2 param_19 = final_axis_importance;
    vec3 w9 = eval_unorm_rgb_weights(param_18, param_19);
    vec3 w10 = w9.zyx;
    vec3 w11 = w8.zyx;
    vec3 w12 = w7.zyx;
    vec3 w13 = w6.zyx;
    vec3 w14 = w5.zyx;
    vec3 w15 = w4.zyx;
    vec3 w16 = w3.zyx;
    vec3 w17 = w2.zyx;
    vec3 w18 = w1.zyx;
    vec3 w19 = w0.zyx;
    vec3 half_sum = ((((((((w0 + w1) + w2) + w3) + w4) + w5) + w6) + w7) + w8) + w9;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _3076 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_3076[0], _3076[1]);
    float param_20 = frame;
    vec2 frame_sign = get_frame_sign(param_20);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset3 = (xy_offset3 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset4 = (xy_offset4 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset5 = (xy_offset5 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset6 = (xy_offset6 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset7 = (xy_offset7 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset8 = (xy_offset8 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset9 = (xy_offset9 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_21 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_21).xyz;
    vec2 param_22 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_22).xyz;
    vec2 param_23 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_23).xyz;
    vec2 param_24 = tex_uv + uv_offset3;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_24).xyz;
    vec2 param_25 = tex_uv + uv_offset4;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_25).xyz;
    vec2 param_26 = tex_uv + uv_offset5;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_26).xyz;
    vec2 param_27 = tex_uv + uv_offset6;
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_27).xyz;
    vec2 param_28 = tex_uv + uv_offset7;
    vec3 sample7 = tex2Daa_tiled_linearize(tex, param_28).xyz;
    vec2 param_29 = tex_uv + uv_offset8;
    vec3 sample8 = tex2Daa_tiled_linearize(tex, param_29).xyz;
    vec2 param_30 = tex_uv + uv_offset9;
    vec3 sample9 = tex2Daa_tiled_linearize(tex, param_30).xyz;
    vec2 param_31 = tex_uv - uv_offset9;
    vec3 sample10 = tex2Daa_tiled_linearize(tex, param_31).xyz;
    vec2 param_32 = tex_uv - uv_offset8;
    vec3 sample11 = tex2Daa_tiled_linearize(tex, param_32).xyz;
    vec2 param_33 = tex_uv - uv_offset7;
    vec3 sample12 = tex2Daa_tiled_linearize(tex, param_33).xyz;
    vec2 param_34 = tex_uv - uv_offset6;
    vec3 sample13 = tex2Daa_tiled_linearize(tex, param_34).xyz;
    vec2 param_35 = tex_uv - uv_offset5;
    vec3 sample14 = tex2Daa_tiled_linearize(tex, param_35).xyz;
    vec2 param_36 = tex_uv - uv_offset4;
    vec3 sample15 = tex2Daa_tiled_linearize(tex, param_36).xyz;
    vec2 param_37 = tex_uv - uv_offset3;
    vec3 sample16 = tex2Daa_tiled_linearize(tex, param_37).xyz;
    vec2 param_38 = tex_uv - uv_offset2;
    vec3 sample17 = tex2Daa_tiled_linearize(tex, param_38).xyz;
    vec2 param_39 = tex_uv - uv_offset1;
    vec3 sample18 = tex2Daa_tiled_linearize(tex, param_39).xyz;
    vec2 param_40 = tex_uv - uv_offset0;
    vec3 sample19 = tex2Daa_tiled_linearize(tex, param_40).xyz;
    vec3 _3355 = (((((((((((((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6)) + (w7 * sample7)) + (w8 * sample8)) + (w9 * sample9)) + (w10 * sample10)) + (w11 * sample11)) + (w12 * sample12)) + (w13 * sample13)) + (w14 * sample14)) + (w15 * sample15)) + (w16 * sample16)) + (w17 * sample17);
    return w_sum_inv * ((_3355 + (w18 * sample18)) + (w19 * sample19));
}

vec3 tex2Daa24x(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 24.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(6.0, 0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(16.0, 1.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(10.0, 2.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(21.0, 3.0) * xy_step);
    vec2 xy_offset4 = xy_start_offset + (vec2(5.0, 4.0) * xy_step);
    vec2 xy_offset5 = xy_start_offset + (vec2(15.0, 5.0) * xy_step);
    vec2 xy_offset6 = xy_start_offset + (vec2(1.0, 6.0) * xy_step);
    vec2 xy_offset7 = xy_start_offset + (vec2(11.0, 7.0) * xy_step);
    vec2 xy_offset8 = xy_start_offset + (vec2(19.0, 8.0) * xy_step);
    vec2 xy_offset9 = xy_start_offset + (vec2(23.0, 9.0) * xy_step);
    vec2 xy_offset10 = xy_start_offset + (vec2(3.0, 10.0) * xy_step);
    vec2 xy_offset11 = xy_start_offset + (vec2(14.0, 11.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec2 param_8 = xy_offset4;
    vec2 param_9 = final_axis_importance;
    vec3 w4 = eval_unorm_rgb_weights(param_8, param_9);
    vec2 param_10 = xy_offset5;
    vec2 param_11 = final_axis_importance;
    vec3 w5 = eval_unorm_rgb_weights(param_10, param_11);
    vec2 param_12 = xy_offset6;
    vec2 param_13 = final_axis_importance;
    vec3 w6 = eval_unorm_rgb_weights(param_12, param_13);
    vec2 param_14 = xy_offset7;
    vec2 param_15 = final_axis_importance;
    vec3 w7 = eval_unorm_rgb_weights(param_14, param_15);
    vec2 param_16 = xy_offset8;
    vec2 param_17 = final_axis_importance;
    vec3 w8 = eval_unorm_rgb_weights(param_16, param_17);
    vec2 param_18 = xy_offset9;
    vec2 param_19 = final_axis_importance;
    vec3 w9 = eval_unorm_rgb_weights(param_18, param_19);
    vec2 param_20 = xy_offset10;
    vec2 param_21 = final_axis_importance;
    vec3 w10 = eval_unorm_rgb_weights(param_20, param_21);
    vec2 param_22 = xy_offset11;
    vec2 param_23 = final_axis_importance;
    vec3 w11 = eval_unorm_rgb_weights(param_22, param_23);
    vec3 w12 = w11.zyx;
    vec3 w13 = w10.zyx;
    vec3 w14 = w9.zyx;
    vec3 w15 = w8.zyx;
    vec3 w16 = w7.zyx;
    vec3 w17 = w6.zyx;
    vec3 w18 = w5.zyx;
    vec3 w19 = w4.zyx;
    vec3 w20 = w3.zyx;
    vec3 w21 = w2.zyx;
    vec3 w22 = w1.zyx;
    vec3 w23 = w0.zyx;
    vec3 half_sum = ((((((((((w0 + w1) + w2) + w3) + w4) + w5) + w6) + w7) + w8) + w9) + w10) + w11;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _3604 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_3604[0], _3604[1]);
    float param_24 = frame;
    vec2 frame_sign = get_frame_sign(param_24);
    vec2 uv_offset0 = (xy_offset0 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset1 = (xy_offset1 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset2 = (xy_offset2 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset3 = (xy_offset3 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset4 = (xy_offset4 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset5 = (xy_offset5 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset6 = (xy_offset6 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset7 = (xy_offset7 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset8 = (xy_offset8 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset9 = (xy_offset9 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset10 = (xy_offset10 * frame_sign) * true_pixel_to_tex_uv;
    vec2 uv_offset11 = (xy_offset11 * frame_sign) * true_pixel_to_tex_uv;
    vec2 param_25 = tex_uv + uv_offset0;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_25).xyz;
    vec2 param_26 = tex_uv + uv_offset1;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_26).xyz;
    vec2 param_27 = tex_uv + uv_offset2;
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_27).xyz;
    vec2 param_28 = tex_uv + uv_offset3;
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_28).xyz;
    vec2 param_29 = tex_uv + uv_offset4;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_29).xyz;
    vec2 param_30 = tex_uv + uv_offset5;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_30).xyz;
    vec2 param_31 = tex_uv + uv_offset6;
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_31).xyz;
    vec2 param_32 = tex_uv + uv_offset7;
    vec3 sample7 = tex2Daa_tiled_linearize(tex, param_32).xyz;
    vec2 param_33 = tex_uv + uv_offset8;
    vec3 sample8 = tex2Daa_tiled_linearize(tex, param_33).xyz;
    vec2 param_34 = tex_uv + uv_offset9;
    vec3 sample9 = tex2Daa_tiled_linearize(tex, param_34).xyz;
    vec2 param_35 = tex_uv + uv_offset10;
    vec3 sample10 = tex2Daa_tiled_linearize(tex, param_35).xyz;
    vec2 param_36 = tex_uv + uv_offset11;
    vec3 sample11 = tex2Daa_tiled_linearize(tex, param_36).xyz;
    vec2 param_37 = tex_uv - uv_offset11;
    vec3 sample12 = tex2Daa_tiled_linearize(tex, param_37).xyz;
    vec2 param_38 = tex_uv - uv_offset10;
    vec3 sample13 = tex2Daa_tiled_linearize(tex, param_38).xyz;
    vec2 param_39 = tex_uv - uv_offset9;
    vec3 sample14 = tex2Daa_tiled_linearize(tex, param_39).xyz;
    vec2 param_40 = tex_uv - uv_offset8;
    vec3 sample15 = tex2Daa_tiled_linearize(tex, param_40).xyz;
    vec2 param_41 = tex_uv - uv_offset7;
    vec3 sample16 = tex2Daa_tiled_linearize(tex, param_41).xyz;
    vec2 param_42 = tex_uv - uv_offset6;
    vec3 sample17 = tex2Daa_tiled_linearize(tex, param_42).xyz;
    vec2 param_43 = tex_uv - uv_offset5;
    vec3 sample18 = tex2Daa_tiled_linearize(tex, param_43).xyz;
    vec2 param_44 = tex_uv - uv_offset4;
    vec3 sample19 = tex2Daa_tiled_linearize(tex, param_44).xyz;
    vec2 param_45 = tex_uv - uv_offset3;
    vec3 sample20 = tex2Daa_tiled_linearize(tex, param_45).xyz;
    vec2 param_46 = tex_uv - uv_offset2;
    vec3 sample21 = tex2Daa_tiled_linearize(tex, param_46).xyz;
    vec2 param_47 = tex_uv - uv_offset1;
    vec3 sample22 = tex2Daa_tiled_linearize(tex, param_47).xyz;
    vec2 param_48 = tex_uv - uv_offset0;
    vec3 sample23 = tex2Daa_tiled_linearize(tex, param_48).xyz;
    vec3 _3923 = (((((((((((((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6)) + (w7 * sample7)) + (w8 * sample8)) + (w9 * sample9)) + (w10 * sample10)) + (w11 * sample11)) + (w12 * sample12)) + (w13 * sample13)) + (w14 * sample14)) + (w15 * sample15)) + (w16 * sample16)) + (w17 * sample17);
    return w_sum_inv * ((((((_3923 + (w18 * sample18)) + (w19 * sample19)) + (w20 * sample20)) + (w21 * sample21)) + (w22 * sample22)) + (w23 * sample23));
}

vec3 tex2Daa_debug_16x_regular(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    float grid_size = 4.0;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    vec2 xy_step = (vec2(1.0) / vec2(grid_size)) * subpixel_support_diameter;
    vec2 xy_start_offset = vec2(0.5 - (grid_size * 0.5)) * xy_step;
    vec2 xy_offset0 = xy_start_offset + (vec2(0.0) * xy_step);
    vec2 xy_offset1 = xy_start_offset + (vec2(1.0, 0.0) * xy_step);
    vec2 xy_offset2 = xy_start_offset + (vec2(2.0, 0.0) * xy_step);
    vec2 xy_offset3 = xy_start_offset + (vec2(3.0, 0.0) * xy_step);
    vec2 xy_offset4 = xy_start_offset + (vec2(0.0, 1.0) * xy_step);
    vec2 xy_offset5 = xy_start_offset + (vec2(1.0) * xy_step);
    vec2 xy_offset6 = xy_start_offset + (vec2(2.0, 1.0) * xy_step);
    vec2 xy_offset7 = xy_start_offset + (vec2(3.0, 1.0) * xy_step);
    vec2 param = xy_offset0;
    vec2 param_1 = final_axis_importance;
    vec3 w0 = eval_unorm_rgb_weights(param, param_1);
    vec2 param_2 = xy_offset1;
    vec2 param_3 = final_axis_importance;
    vec3 w1 = eval_unorm_rgb_weights(param_2, param_3);
    vec2 param_4 = xy_offset2;
    vec2 param_5 = final_axis_importance;
    vec3 w2 = eval_unorm_rgb_weights(param_4, param_5);
    vec2 param_6 = xy_offset3;
    vec2 param_7 = final_axis_importance;
    vec3 w3 = eval_unorm_rgb_weights(param_6, param_7);
    vec2 param_8 = xy_offset4;
    vec2 param_9 = final_axis_importance;
    vec3 w4 = eval_unorm_rgb_weights(param_8, param_9);
    vec2 param_10 = xy_offset5;
    vec2 param_11 = final_axis_importance;
    vec3 w5 = eval_unorm_rgb_weights(param_10, param_11);
    vec2 param_12 = xy_offset6;
    vec2 param_13 = final_axis_importance;
    vec3 w6 = eval_unorm_rgb_weights(param_12, param_13);
    vec2 param_14 = xy_offset7;
    vec2 param_15 = final_axis_importance;
    vec3 w7 = eval_unorm_rgb_weights(param_14, param_15);
    vec3 w8 = w7.zyx;
    vec3 w9 = w6.zyx;
    vec3 w10 = w5.zyx;
    vec3 w11 = w4.zyx;
    vec3 w12 = w3.zyx;
    vec3 w13 = w2.zyx;
    vec3 w14 = w1.zyx;
    vec3 w15 = w0.zyx;
    vec3 half_sum = ((((((w0 + w1) + w2) + w3) + w4) + w5) + w6) + w7;
    vec3 w_sum = half_sum + half_sum.zyx;
    vec3 w_sum_inv = vec3(1.0) / w_sum;
    mat2 _4114 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_4114[0], _4114[1]);
    vec2 uv_step_x = vec2(xy_step.x, 0.0) * true_pixel_to_tex_uv;
    vec2 uv_step_y = vec2(0.0, xy_step.y) * true_pixel_to_tex_uv;
    vec2 uv_offset0 = (uv_step_x + uv_step_y) * (-1.5);
    vec2 sample0_uv = tex_uv + uv_offset0;
    vec2 sample4_uv = sample0_uv + uv_step_y;
    vec2 sample8_uv = sample0_uv + (uv_step_y * 2.0);
    vec2 sample12_uv = sample0_uv + (uv_step_y * 3.0);
    vec2 param_16 = sample0_uv;
    vec3 sample0 = tex2Daa_tiled_linearize(tex, param_16).xyz;
    vec2 param_17 = sample0_uv + uv_step_x;
    vec3 sample1 = tex2Daa_tiled_linearize(tex, param_17).xyz;
    vec2 param_18 = sample0_uv + (uv_step_x * 2.0);
    vec3 sample2 = tex2Daa_tiled_linearize(tex, param_18).xyz;
    vec2 param_19 = sample0_uv + (uv_step_x * 3.0);
    vec3 sample3 = tex2Daa_tiled_linearize(tex, param_19).xyz;
    vec2 param_20 = sample4_uv;
    vec3 sample4 = tex2Daa_tiled_linearize(tex, param_20).xyz;
    vec2 param_21 = sample4_uv + uv_step_x;
    vec3 sample5 = tex2Daa_tiled_linearize(tex, param_21).xyz;
    vec2 param_22 = sample4_uv + (uv_step_x * 2.0);
    vec3 sample6 = tex2Daa_tiled_linearize(tex, param_22).xyz;
    vec2 param_23 = sample4_uv + (uv_step_x * 3.0);
    vec3 sample7 = tex2Daa_tiled_linearize(tex, param_23).xyz;
    vec2 param_24 = sample8_uv;
    vec3 sample8 = tex2Daa_tiled_linearize(tex, param_24).xyz;
    vec2 param_25 = sample8_uv + uv_step_x;
    vec3 sample9 = tex2Daa_tiled_linearize(tex, param_25).xyz;
    vec2 param_26 = sample8_uv + (uv_step_x * 2.0);
    vec3 sample10 = tex2Daa_tiled_linearize(tex, param_26).xyz;
    vec2 param_27 = sample8_uv + (uv_step_x * 3.0);
    vec3 sample11 = tex2Daa_tiled_linearize(tex, param_27).xyz;
    vec2 param_28 = sample12_uv;
    vec3 sample12 = tex2Daa_tiled_linearize(tex, param_28).xyz;
    vec2 param_29 = sample12_uv + uv_step_x;
    vec3 sample13 = tex2Daa_tiled_linearize(tex, param_29).xyz;
    vec2 param_30 = sample12_uv + (uv_step_x * 2.0);
    vec3 sample14 = tex2Daa_tiled_linearize(tex, param_30).xyz;
    vec2 param_31 = sample12_uv + (uv_step_x * 3.0);
    vec3 sample15 = tex2Daa_tiled_linearize(tex, param_31).xyz;
    return w_sum_inv * ((((((((((((((((w0 * sample0) + (w1 * sample1)) + (w2 * sample2)) + (w3 * sample3)) + (w4 * sample4)) + (w5 * sample5)) + (w6 * sample6)) + (w7 * sample7)) + (w8 * sample8)) + (w9 * sample9)) + (w10 * sample10)) + (w11 * sample11)) + (w12 * sample12)) + (w13 * sample13)) + (w14 * sample14)) + (w15 * sample15));
}

vec3 tex2Daa_debug_dynamic(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    int grid_size = 8;
    assign_aa_cubic_constants();
    vec4 ssd_fai = get_subpixel_support_diam_and_final_axis_importance();
    vec2 subpixel_support_diameter = ssd_fai.xy;
    vec2 final_axis_importance = ssd_fai.zw;
    float grid_radius_in_samples = (float(grid_size) - 1.0) / 2.0;
    vec2 filter_space_offset_step = subpixel_support_diameter / vec2(float(grid_size));
    vec2 sample0_filter_space_offset = filter_space_offset_step * (-grid_radius_in_samples);
    vec3 weight_sum = vec3(0.0);
    vec3 weights[64];
    for (int i = 0; i < grid_size; i++)
    {
        for (int j = 0; j < grid_size; j++)
        {
            vec2 offset = sample0_filter_space_offset + (vec2(float(j), float(i)) * filter_space_offset_step);
            vec2 param = offset;
            vec2 param_1 = final_axis_importance;
            vec3 weight = eval_unorm_rgb_weights(param, param_1);
            weights[(i * grid_size) + j] = weight;
            weight_sum += weight;
        }
    }
    mat2 _4420 = pixel_to_tex_uv * aa_pixel_diameter;
    mat2 true_pixel_to_tex_uv = mat2(_4420[0], _4420[1]);
    vec2 uv_offset_step_x = vec2(filter_space_offset_step.x, 0.0) * true_pixel_to_tex_uv;
    vec2 uv_offset_step_y = vec2(0.0, filter_space_offset_step.y) * true_pixel_to_tex_uv;
    vec2 sample0_uv_offset = (uv_offset_step_x + uv_offset_step_y) * (-grid_radius_in_samples);
    vec2 sample0_uv = tex_uv + sample0_uv_offset;
    vec3 sum = vec3(0.0);
    vec3 weight_sum_inv = vec3(1.0) / weight_sum;
    for (int i_1 = 0; i_1 < grid_size; i_1++)
    {
        vec2 row_i_first_sample_uv = sample0_uv + (uv_offset_step_y * float(i_1));
        for (int j_1 = 0; j_1 < grid_size; j_1++)
        {
            vec2 sample_uv = row_i_first_sample_uv + (uv_offset_step_x * float(j_1));
            vec2 param_2 = sample_uv;
            sum += (weights[(i_1 * grid_size) + j_1] * tex2Daa_tiled_linearize(tex, param_2).xyz);
        }
    }
    return sum * weight_sum_inv;
}

vec3 tex2Daa(sampler2D tex, vec2 tex_uv, mat2 pixel_to_tex_uv, float frame)
{
    vec3 _4508;
    if (aa_level < 0.5)
    {
        vec2 param = tex_uv;
        _4508 = tex2D_linearize(tex, param).xyz;
    }
    else
    {
        vec3 _4518;
        if (aa_level < 3.5)
        {
            vec2 param_1 = tex_uv;
            mat2 param_2 = pixel_to_tex_uv;
            _4518 = tex2Daa_subpixel_weights_only(tex, param_1, param_2);
        }
        else
        {
            vec3 _4529;
            if (aa_level < 4.5)
            {
                vec2 param_3 = tex_uv;
                mat2 param_4 = pixel_to_tex_uv;
                float param_5 = frame;
                vec3 _4538 = tex2Daa4x(tex, param_3, param_4, param_5);
                _4529 = _4538;
            }
            else
            {
                vec3 _4542;
                if (aa_level < 5.5)
                {
                    vec2 param_6 = tex_uv;
                    mat2 param_7 = pixel_to_tex_uv;
                    float param_8 = frame;
                    vec3 _4551 = tex2Daa5x(tex, param_6, param_7, param_8);
                    _4542 = _4551;
                }
                else
                {
                    vec3 _4555;
                    if (aa_level < 6.5)
                    {
                        vec2 param_9 = tex_uv;
                        mat2 param_10 = pixel_to_tex_uv;
                        float param_11 = frame;
                        vec3 _4564 = tex2Daa6x(tex, param_9, param_10, param_11);
                        _4555 = _4564;
                    }
                    else
                    {
                        vec3 _4568;
                        if (aa_level < 7.5)
                        {
                            vec2 param_12 = tex_uv;
                            mat2 param_13 = pixel_to_tex_uv;
                            float param_14 = frame;
                            vec3 _4577 = tex2Daa7x(tex, param_12, param_13, param_14);
                            _4568 = _4577;
                        }
                        else
                        {
                            vec3 _4582;
                            if (aa_level < 11.5)
                            {
                                vec2 param_15 = tex_uv;
                                mat2 param_16 = pixel_to_tex_uv;
                                float param_17 = frame;
                                vec3 _4591 = tex2Daa8x(tex, param_15, param_16, param_17);
                                _4582 = _4591;
                            }
                            else
                            {
                                vec3 _4596;
                                if (aa_level < 15.5)
                                {
                                    vec2 param_18 = tex_uv;
                                    mat2 param_19 = pixel_to_tex_uv;
                                    float param_20 = frame;
                                    vec3 _4605 = tex2Daa12x(tex, param_18, param_19, param_20);
                                    _4596 = _4605;
                                }
                                else
                                {
                                    vec3 _4610;
                                    if (aa_level < 19.5)
                                    {
                                        vec2 param_21 = tex_uv;
                                        mat2 param_22 = pixel_to_tex_uv;
                                        float param_23 = frame;
                                        vec3 _4619 = tex2Daa16x(tex, param_21, param_22, param_23);
                                        _4610 = _4619;
                                    }
                                    else
                                    {
                                        vec3 _4624;
                                        if (aa_level < 23.5)
                                        {
                                            vec2 param_24 = tex_uv;
                                            mat2 param_25 = pixel_to_tex_uv;
                                            float param_26 = frame;
                                            vec3 _4633 = tex2Daa20x(tex, param_24, param_25, param_26);
                                            _4624 = _4633;
                                        }
                                        else
                                        {
                                            vec3 _4638;
                                            if (aa_level < 253.5)
                                            {
                                                vec2 param_27 = tex_uv;
                                                mat2 param_28 = pixel_to_tex_uv;
                                                float param_29 = frame;
                                                vec3 _4647 = tex2Daa24x(tex, param_27, param_28, param_29);
                                                _4638 = _4647;
                                            }
                                            else
                                            {
                                                vec3 _4652;
                                                if (aa_level < 254.5)
                                                {
                                                    vec2 param_30 = tex_uv;
                                                    mat2 param_31 = pixel_to_tex_uv;
                                                    float param_32 = frame;
                                                    vec3 _4661 = tex2Daa_debug_16x_regular(tex, param_30, param_31, param_32);
                                                    _4652 = _4661;
                                                }
                                                else
                                                {
                                                    vec2 param_33 = tex_uv;
                                                    mat2 param_34 = pixel_to_tex_uv;
                                                    float param_35 = frame;
                                                    vec3 _4669 = tex2Daa_debug_dynamic(tex, param_33, param_34, param_35);
                                                    _4652 = _4669;
                                                }
                                                _4638 = _4652;
                                            }
                                            _4624 = _4638;
                                        }
                                        _4610 = _4624;
                                    }
                                    _4596 = _4610;
                                }
                                _4582 = _4596;
                            }
                            _4568 = _4582;
                        }
                        _4555 = _4568;
                    }
                    _4542 = _4555;
                }
                _4529 = _4542;
            }
            _4518 = _4529;
        }
        _4508 = _4518;
    }
    return _4508;
}

float get_border_dim_factor(vec2 video_uv, vec2 geom_aspect)
{
    vec2 edge_dists = min(video_uv, vec2(1.0) - video_uv) * geom_aspect;
    vec2 border_penetration = max(vec2(LIBRA_UNIFORM_border_size) - edge_dists, vec2(0.0));
    float penetration_ratio = length(border_penetration) / LIBRA_UNIFORM_border_size;
    if (LIBRA_UNIFORM_border_size == 0.0)
    {
        penetration_ratio = 0.0;
    }
    float border_escape_ratio = max(1.0 - penetration_ratio, 0.0);
    float border_dim_factor = pow(border_escape_ratio, LIBRA_UNIFORM_border_darkness) * max(1.0, LIBRA_UNIFORM_border_compress);
    return min(border_dim_factor, 1.0);
}

float get_lcd_gamma()
{
    return LIBRA_UNIFORM_lcd_gamma;
}

float get_output_gamma()
{
    return get_lcd_gamma();
}

float get_pass_output_gamma()
{
    return get_output_gamma();
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
    gamma_encode_output = true;
    gamma_aware_bilinear = !linearize_input;
    aa_pixel_diameter = 1.0;
    aa_lanczos_lobes = 3.0;
    aa_gauss_support = 1.0 / aa_pixel_diameter;
    aa_tent_support = 1.0 / aa_pixel_diameter;
    vec2 _449;
    if (aa_filter < 5.5)
    {
        _449 = vec2(1.0);
    }
    else
    {
        vec2 _456;
        if (aa_filter < 8.5)
        {
            _456 = vec2(1.0, 0.0);
        }
        else
        {
            vec2 _464;
            if (aa_filter < 9.5)
            {
                _464 = vec2(1.0, 1.0 / aa_lanczos_lobes);
            }
            else
            {
                _464 = vec2(1.0);
            }
            _456 = _464;
        }
        _449 = _456;
    }
    aa_xy_axis_importance = _449;
    aa_box_support = 0.5;
    aa_cubic_support = 2.0;
    vec2 geom_aspect = LIBRA_VARYING_4.xy;
    vec2 geom_overscan = LIBRA_VARYING_4.zw;
    vec2 video_size_inv = LIBRA_VARYING_1.xy;
    vec2 texture_size_inv = LIBRA_VARYING_1.zw;
    mat3 global_to_local = mat3(vec3(LIBRA_VARYING_5), vec3(LIBRA_VARYING_6), vec3(LIBRA_VARYING_7));
    float geom_mode = LIBRA_UNIFORM_geom_mode_runtime;
    vec2 flat_video_uv = LIBRA_VARYING_0 * (LIBRA_UNIFORM_SourceSize.xy * video_size_inv);
    vec2 video_uv_no_geom_overscan;
    mat2 pixel_to_video_uv;
    if (geom_mode > 0.5)
    {
        vec2 param = flat_video_uv;
        vec3 param_1 = LIBRA_VARYING_3;
        vec2 param_2 = LIBRA_VARYING_2;
        vec2 param_3 = geom_aspect;
        float param_4 = geom_mode;
        mat3 param_5 = global_to_local;
        mat2 param_6;
        vec2 _5402 = get_curved_video_uv_coords_and_tangent_matrix(param, param_1, param_2, param_3, param_4, param_5, param_6);
        pixel_to_video_uv = param_6;
        video_uv_no_geom_overscan = _5402;
    }
    else
    {
        video_uv_no_geom_overscan = flat_video_uv;
        pixel_to_video_uv = mat2(vec2(LIBRA_VARYING_2.x, 0.0), vec2(0.0, LIBRA_VARYING_2.y));
    }
    vec2 video_uv = ((video_uv_no_geom_overscan - vec2(0.5)) / geom_overscan) + vec2(0.5);
    vec2 tex_uv = video_uv * (LIBRA_UNIFORM_SourceSize.xy * texture_size_inv);
    vec2 param_7 = (LIBRA_UNIFORM_SourceSize.xy * texture_size_inv) / LIBRA_VARYING_4.zw;
    mat2 param_8 = pixel_to_video_uv;
    mat2 pixel_to_tex_uv = mul_scale(param_7, param_8);
    vec2 abs_aa_r_offset = abs(get_aa_subpixel_r_offset());
    bool need_subpixel_aa = false;
    bool _5447 = aa_level > 0.5;
    bool _5464;
    if (_5447)
    {
        bool _5451 = geom_mode > 0.5;
        bool _5463;
        if (!_5451)
        {
            _5463 = any(bvec2(geom_overscan.x != 1.0, geom_overscan.y != 1.0));
        }
        else
        {
            _5463 = _5451;
        }
        _5464 = _5463;
    }
    else
    {
        _5464 = _5447;
    }
    vec3 color;
    if (_5464)
    {
        vec2 param_9 = tex_uv;
        mat2 param_10 = pixel_to_tex_uv;
        float param_11 = float(LIBRA_UNIFORM_FrameCount);
        vec3 _5479 = tex2Daa(LIBRA_TEXTURE_Source, param_9, param_10, param_11);
        color = _5479;
    }
    else
    {
        if ((aa_level > 0.5) && need_subpixel_aa)
        {
            vec2 param_12 = tex_uv;
            mat2 param_13 = pixel_to_tex_uv;
            color = tex2Daa_subpixel_weights_only(LIBRA_TEXTURE_Source, param_12, param_13);
        }
        else
        {
            vec2 param_14 = tex_uv;
            color = tex2D_linearize(LIBRA_TEXTURE_Source, param_14).xyz;
        }
    }
    vec2 param_15 = video_uv;
    vec2 param_16 = geom_aspect;
    float border_dim_factor = get_border_dim_factor(param_15, param_16);
    vec3 final_color = color * border_dim_factor;
    vec4 param_17 = vec4(final_color, 1.0);
    FragColor = encode_output(param_17);
}
