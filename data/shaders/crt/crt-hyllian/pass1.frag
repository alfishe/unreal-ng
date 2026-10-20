#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform float LIBRA_UNIFORM_PRESET_OPTION;
uniform float LIBRA_UNIFORM_BEAM_MIN_WIDTH;
uniform float LIBRA_UNIFORM_BEAM_MAX_WIDTH;
uniform float LIBRA_UNIFORM_SCANLINES_STRENGTH;
uniform float LIBRA_UNIFORM_SCANLINES_SHAPE;
uniform float LIBRA_UNIFORM_SHARPNESS_HACK;
uniform float LIBRA_UNIFORM_SCANLINES_CUTOFF;
uniform float LIBRA_UNIFORM_SCANLINES_HIRES;
uniform float LIBRA_UNIFORM_IR_SCALE;
uniform float LIBRA_UNIFORM_CRT_ANTI_RINGING;
uniform float LIBRA_UNIFORM_VSCANLINES;
uniform float LIBRA_UNIFORM_BRIGHTBOOST;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
in vec4 LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_2;
in vec2 LIBRA_VARYING_3;
in vec4 LIBRA_VARYING_4;
in float LIBRA_VARYING_5;
layout(location = 0) out vec4 FragColor;

vec3 get_scanlines(inout vec3 d0, inout vec3 d1, vec3 color0, vec3 color1, float scan_shape)
{
    if (scan_shape > 0.5)
    {
        d0 = exp((d0 * (-16.0)) * d0);
        d1 = exp((d1 * (-16.0)) * d1);
    }
    else
    {
        d0 = vec3(1.0) - smoothstep(vec3(0.0), vec3(0.5), d0);
        d1 = vec3(1.0) - smoothstep(vec3(0.0), vec3(0.5), d1);
    }
    return (color0 * d0) + (color1 * d1);
}

void main()
{
    vec2 pix_coord = (LIBRA_VARYING_0 * LIBRA_VARYING_1.xy) - vec2(0.5);
    vec2 lr_pxc = floor(pix_coord);
    vec2 hr_pxc = mix(vec2(lr_pxc.x, pix_coord.y), vec2(pix_coord.x, lr_pxc.y), vec2(LIBRA_UNIFORM_VSCANLINES));
    bvec2 _107 = bvec2(LIBRA_UNIFORM_SCANLINES_HIRES > 0.5);
    vec2 tc = (vec2(_107.x ? hr_pxc.x : lr_pxc.x, _107.y ? hr_pxc.y : lr_pxc.y) + vec2(0.5)) * LIBRA_VARYING_1.zw;
    vec2 fp = mix(fract(pix_coord), fract(pix_coord.yx), vec2(LIBRA_UNIFORM_VSCANLINES));
    vec3 c00 = texture(LIBRA_TEXTURE_Source, tc - LIBRA_VARYING_2).xyz;
    vec3 c01 = texture(LIBRA_TEXTURE_Source, tc).xyz;
    vec3 c02 = texture(LIBRA_TEXTURE_Source, tc + LIBRA_VARYING_2).xyz;
    vec3 c03 = texture(LIBRA_TEXTURE_Source, tc + (LIBRA_VARYING_2 * 2.0)).xyz;
    vec3 c10;
    vec3 c11;
    vec3 c12;
    vec3 c13;
    if (LIBRA_UNIFORM_SCANLINES_HIRES < 0.5)
    {
        c10 = texture(LIBRA_TEXTURE_Source, (tc - LIBRA_VARYING_2) + LIBRA_VARYING_3).xyz;
        c11 = texture(LIBRA_TEXTURE_Source, tc + LIBRA_VARYING_3).xyz;
        c12 = texture(LIBRA_TEXTURE_Source, (tc + LIBRA_VARYING_2) + LIBRA_VARYING_3).xyz;
        c13 = texture(LIBRA_TEXTURE_Source, (tc + (LIBRA_VARYING_2 * 2.0)) + LIBRA_VARYING_3).xyz;
    }
    else
    {
        c10 = c00;
        c11 = c01;
        c12 = c02;
        c13 = c03;
    }
    mat4x3 color_matrix0 = mat4x3(vec3(c00), vec3(c01), vec3(c02), vec3(c03));
    mat4x3 color_matrix1 = mat4x3(vec3(c10), vec3(c11), vec3(c12), vec3(c13));
    vec4 invX_Px = vec4((fp.x * fp.x) * fp.x, fp.x * fp.x, fp.x, 1.0) * mat4(vec4(-0.5, 1.0, -0.5, 0.0), vec4(1.5, -2.5, 0.0, 1.0), vec4(-1.5, 2.0, 0.5, 0.0), vec4(0.5, -0.5, 0.0, 0.0));
    vec3 color0 = color_matrix0 * invX_Px;
    vec3 color1 = color_matrix1 * invX_Px;
    vec3 min_sample0 = min(c01, c02);
    vec3 max_sample0 = max(c01, c02);
    vec3 min_sample1 = min(c11, c12);
    vec3 max_sample1 = max(c11, c12);
    vec3 aux = color0;
    color0 = clamp(color0, min_sample0, max_sample0);
    color0 = mix(aux, color0, vec3(LIBRA_UNIFORM_CRT_ANTI_RINGING));
    aux = color1;
    color1 = clamp(color1, min_sample1, max_sample1);
    color1 = mix(aux, color1, vec3(LIBRA_UNIFORM_CRT_ANTI_RINGING));
    float pos0 = fp.y;
    float pos1 = 1.0 - fp.y;
    vec3 lum0 = mix(vec3(LIBRA_VARYING_4.y), vec3(LIBRA_VARYING_4.z), color0);
    vec3 lum1 = mix(vec3(LIBRA_VARYING_4.y), vec3(LIBRA_VARYING_4.z), color1);
    vec3 d0 = vec3(LIBRA_VARYING_4.x * pos0) / ((lum0 * lum0) + vec3(1.0000000116860974230803549289703e-07));
    vec3 d1 = vec3(LIBRA_VARYING_4.x * pos1) / ((lum1 * lum1) + vec3(1.0000000116860974230803549289703e-07));
    vec3 _382;
    if (LIBRA_VARYING_5 > 0.5)
    {
        vec3 param = d0;
        vec3 param_1 = d1;
        vec3 param_2 = color0;
        vec3 param_3 = color1;
        float param_4 = LIBRA_VARYING_4.w;
        vec3 _397 = get_scanlines(param, param_1, param_2, param_3, param_4);
        _382 = _397;
    }
    else
    {
        _382 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    }
    vec3 color = _382;
    FragColor = vec4(color, 1.0);
}
