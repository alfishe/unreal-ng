#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_JINC2_WINDOW_SINC;
uniform float LIBRA_UNIFORM_JINC2_SINC;
uniform float LIBRA_UNIFORM_JINC2_AR_STRENGTH;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

float d(vec2 pt1, vec2 pt2)
{
    vec2 v = pt2 - pt1;
    return sqrt(dot(v, v));
}

vec4 resampler(vec4 x)
{
    float _72;
    if (x.x == 0.0)
    {
        _72 = (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875) * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875);
    }
    else
    {
        _72 = (sin(x.x * (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875)) * sin(x.x * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875))) / (x.x * x.x);
    }
    vec4 res;
    res.x = _72;
    float _118;
    if (x.y == 0.0)
    {
        _118 = (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875) * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875);
    }
    else
    {
        _118 = (sin(x.y * (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875)) * sin(x.y * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875))) / (x.y * x.y);
    }
    res.y = _118;
    float _156;
    if (x.z == 0.0)
    {
        _156 = (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875) * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875);
    }
    else
    {
        _156 = (sin(x.z * (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875)) * sin(x.z * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875))) / (x.z * x.z);
    }
    res.z = _156;
    float _194;
    if (x.w == 0.0)
    {
        _194 = (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875) * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875);
    }
    else
    {
        _194 = (sin(x.w * (LIBRA_UNIFORM_JINC2_WINDOW_SINC * 3.1415927410125732421875)) * sin(x.w * (LIBRA_UNIFORM_JINC2_SINC * 3.1415927410125732421875))) / (x.w * x.w);
    }
    res.w = _194;
    return res;
}

vec3 min4(vec3 a, vec3 b, vec3 c, vec3 d_1)
{
    return min(a, min(b, min(c, d_1)));
}

vec3 max4(vec3 a, vec3 b, vec3 c, vec3 d_1)
{
    return max(a, max(b, max(c, d_1)));
}

void main()
{
    vec2 dx = vec2(1.0, 0.0);
    vec2 dy = vec2(0.0, 1.0);
    vec2 pc = LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy;
    vec2 tc = floor(pc - vec2(0.5)) + vec2(0.5);
    vec2 param = pc;
    vec2 param_1 = (tc - dx) - dy;
    vec2 param_2 = pc;
    vec2 param_3 = tc - dy;
    vec2 param_4 = pc;
    vec2 param_5 = (tc + dx) - dy;
    vec2 param_6 = pc;
    vec2 param_7 = (tc + (dx * 2.0)) - dy;
    vec4 param_8 = vec4(d(param, param_1), d(param_2, param_3), d(param_4, param_5), d(param_6, param_7));
    mat4 weights;
    weights[0] = resampler(param_8);
    vec2 param_9 = pc;
    vec2 param_10 = tc - dx;
    vec2 param_11 = pc;
    vec2 param_12 = tc;
    vec2 param_13 = pc;
    vec2 param_14 = tc + dx;
    vec2 param_15 = pc;
    vec2 param_16 = tc + (dx * 2.0);
    vec4 param_17 = vec4(d(param_9, param_10), d(param_11, param_12), d(param_13, param_14), d(param_15, param_16));
    weights[1] = resampler(param_17);
    vec2 param_18 = pc;
    vec2 param_19 = (tc - dx) + dy;
    vec2 param_20 = pc;
    vec2 param_21 = tc + dy;
    vec2 param_22 = pc;
    vec2 param_23 = (tc + dx) + dy;
    vec2 param_24 = pc;
    vec2 param_25 = (tc + (dx * 2.0)) + dy;
    vec4 param_26 = vec4(d(param_18, param_19), d(param_20, param_21), d(param_22, param_23), d(param_24, param_25));
    weights[2] = resampler(param_26);
    vec2 param_27 = pc;
    vec2 param_28 = (tc - dx) + (dy * 2.0);
    vec2 param_29 = pc;
    vec2 param_30 = tc + (dy * 2.0);
    vec2 param_31 = pc;
    vec2 param_32 = (tc + dx) + (dy * 2.0);
    vec2 param_33 = pc;
    vec2 param_34 = (tc + (dx * 2.0)) + (dy * 2.0);
    vec4 param_35 = vec4(d(param_27, param_28), d(param_29, param_30), d(param_31, param_32), d(param_33, param_34));
    weights[3] = resampler(param_35);
    dx *= LIBRA_UNIFORM_SourceSize.zw;
    dy *= LIBRA_UNIFORM_SourceSize.zw;
    tc *= LIBRA_UNIFORM_SourceSize.zw;
    vec3 c00 = texture(LIBRA_TEXTURE_Source, (tc - dx) - dy).xyz;
    vec3 c10 = texture(LIBRA_TEXTURE_Source, tc - dy).xyz;
    vec3 c20 = texture(LIBRA_TEXTURE_Source, (tc + dx) - dy).xyz;
    vec3 c30 = texture(LIBRA_TEXTURE_Source, (tc + (dx * 2.0)) - dy).xyz;
    vec3 c01 = texture(LIBRA_TEXTURE_Source, tc - dx).xyz;
    vec3 c11 = texture(LIBRA_TEXTURE_Source, tc).xyz;
    vec3 c21 = texture(LIBRA_TEXTURE_Source, tc + dx).xyz;
    vec3 c31 = texture(LIBRA_TEXTURE_Source, tc + (dx * 2.0)).xyz;
    vec3 c02 = texture(LIBRA_TEXTURE_Source, (tc - dx) + dy).xyz;
    vec3 c12 = texture(LIBRA_TEXTURE_Source, tc + dy).xyz;
    vec3 c22 = texture(LIBRA_TEXTURE_Source, (tc + dx) + dy).xyz;
    vec3 c32 = texture(LIBRA_TEXTURE_Source, (tc + (dx * 2.0)) + dy).xyz;
    vec3 c03 = texture(LIBRA_TEXTURE_Source, (tc - dx) + (dy * 2.0)).xyz;
    vec3 c13 = texture(LIBRA_TEXTURE_Source, tc + (dy * 2.0)).xyz;
    vec3 c23 = texture(LIBRA_TEXTURE_Source, (tc + dx) + (dy * 2.0)).xyz;
    vec3 c33 = texture(LIBRA_TEXTURE_Source, (tc + (dx * 2.0)) + (dy * 2.0)).xyz;
    vec3 param_36 = c11;
    vec3 param_37 = c21;
    vec3 param_38 = c12;
    vec3 param_39 = c22;
    vec3 min_sample = min4(param_36, param_37, param_38, param_39);
    vec3 param_40 = c11;
    vec3 param_41 = c21;
    vec3 param_42 = c12;
    vec3 param_43 = c22;
    vec3 max_sample = max4(param_40, param_41, param_42, param_43);
    vec3 color = mat4x3(vec3(c00), vec3(c10), vec3(c20), vec3(c30)) * weights[0];
    color += (mat4x3(vec3(c01), vec3(c11), vec3(c21), vec3(c31)) * weights[1]);
    color += (mat4x3(vec3(c02), vec3(c12), vec3(c22), vec3(c32)) * weights[2]);
    color += (mat4x3(vec3(c03), vec3(c13), vec3(c23), vec3(c33)) * weights[3]);
    color /= vec3(dot(weights * vec4(1.0), vec4(1.0)));
    vec3 aux = color;
    color = clamp(color, min_sample, max_sample);
    color = mix(aux, color, vec3(LIBRA_UNIFORM_JINC2_AR_STRENGTH));
    FragColor = vec4(color, 1.0);
}
