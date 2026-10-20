#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_CSHARPEN;
uniform float LIBRA_UNIFORM_CCONTR;
uniform float LIBRA_UNIFORM_CDETAILS;
uniform float LIBRA_UNIFORM_NDEBLUR;
uniform float LIBRA_UNIFORM_DEDGE;
uniform float LIBRA_UNIFORM_DESHARP;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_PrePass0;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

float get_luma(vec3 c)
{
    return dot(c, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
}

vec3 rgb2yuv(vec3 rgb)
{
    return rgb * mat3(vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625), vec3(-0.14712999761104583740234375, -0.2888599932193756103515625, 0.43599998950958251953125), vec3(0.6150000095367431640625, -0.51498997211456298828125, -0.100010000169277191162109375));
}

vec3 yuv2rgb(vec3 yuv)
{
    return yuv * mat3(vec3(1.0, 0.0, 1.13982999324798583984375), vec3(1.0, -0.3946500122547149658203125, -0.5806000232696533203125), vec3(1.0, 2.0321099758148193359375, 0.0));
}

void main()
{
    vec2 g01 = vec2(-0.001562500023283064365386962890625, 0.0);
    vec2 g21 = vec2(0.001562500023283064365386962890625, 0.0);
    vec3 c01 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + g01).xyz;
    vec3 c21 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + g21).xyz;
    vec3 c11 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 b11 = (c01 + c21) * 0.5;
    float contrast = max(max(c11.x, c11.y), c11.z);
    contrast = mix(2.0 * LIBRA_UNIFORM_CCONTR, LIBRA_UNIFORM_CCONTR, contrast);
    vec3 mn = min(min(c01, c21), c11);
    vec3 mn1 = min(mn, c11 * (1.0 - contrast));
    vec3 mx = max(max(c01, c21), c11);
    vec3 mx1 = max(mx, c11 * (1.0 + contrast));
    vec3 dif = pow((mx1 - mn1) + vec3(9.9999997473787516355514526367188e-06), vec3(0.75));
    vec3 sharpen = mix(vec3(LIBRA_UNIFORM_CSHARPEN * LIBRA_UNIFORM_CDETAILS), vec3(LIBRA_UNIFORM_CSHARPEN), dif);
    vec3 res = clamp(mix(c11, b11, -sharpen), mn1, mx1);
    if (LIBRA_UNIFORM_NDEBLUR > 1.125)
    {
        vec2 tex0 = (floor(LIBRA_UNIFORM_OriginalSize.xy * LIBRA_VARYING_0) + vec2(0.5)) * LIBRA_UNIFORM_OriginalSize.zw;
        vec3 param = texture(LIBRA_TEXTURE_PrePass0, LIBRA_VARYING_0 + (g01 * 2.0)).xyz;
        float l01 = get_luma(param);
        vec3 param_1 = texture(LIBRA_TEXTURE_PrePass0, LIBRA_VARYING_0 + (g21 * 2.0)).xyz;
        float l21 = get_luma(param_1);
        vec3 param_2 = texture(LIBRA_TEXTURE_PrePass0, tex0).xyz;
        float l11 = get_luma(param_2);
        float dth = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).w;
        float DEB = LIBRA_UNIFORM_NDEBLUR;
        float v11 = min(min(l01, l21), l11);
        l11 = max(max(l01, l21), l11);
        vec3 param_3 = c01;
        vec3 param_4 = c21;
        vec3 param_5 = c11;
        float lmin = min(min(get_luma(param_3), get_luma(param_4)), get_luma(param_5));
        vec3 param_6 = c01;
        vec3 param_7 = c21;
        vec3 param_8 = c11;
        float lmax = max(max(get_luma(param_6), get_luma(param_7)), get_luma(param_8));
        float ln1 = min(mix(v11, lmin, lmin), lmin);
        float lx1 = max(mix(lmax, l11, lmax), lmax);
        vec3 param_9 = res;
        float r11 = get_luma(param_9);
        float dif1 = max(r11 - ln1, 0.0) + 9.9999997473787516355514526367188e-06;
        dif1 = pow(dif1, DEB);
        float dif2 = max(lx1 - r11, 0.0) + 9.9999997473787516355514526367188e-06;
        dif2 = pow(dif2, DEB);
        float ratio = dif1 / (dif1 + dif2);
        float lsharpen = mix(ln1, lx1, ratio);
        lsharpen = min(lsharpen, max(LIBRA_UNIFORM_DEDGE * lsharpen, r11));
        vec3 param_10 = res;
        res = rgb2yuv(param_10);
        v11 = res.x;
        res.x = lsharpen;
        res.x = clamp(((1.0 + LIBRA_UNIFORM_DESHARP) * res.x) - (LIBRA_UNIFORM_DESHARP * v11), ln1 * (1.0 - contrast), lx1 * (1.0 + contrast));
        vec3 param_11 = res;
        res = max(yuv2rgb(param_11), vec3(0.0));
    }
    FragColor = vec4(res, 1.0);
}
