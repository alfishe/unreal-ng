#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform float LIBRA_UNIFORM_OutputGamma;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_ScanlinesSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_GlowSigma;
uniform float LIBRA_UNIFORM_GlowAmount;

uniform sampler2D LIBRA_TEXTURE_BlueNoiseTex;
uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_Scanlines;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 linear_to_srgb(inout vec3 rgb)
{
    rgb = clamp(rgb, vec3(0.0), vec3(1.0));
    bvec3 cutoff = lessThan(rgb, vec3(0.003130800090730190277099609375));
    vec3 lower = rgb * vec3(12.9200000762939453125);
    vec3 higher = (vec3(1.05499994754791259765625) * pow(rgb, vec3(0.4166666567325592041015625))) - vec3(0.054999999701976776123046875);
    return vec3(cutoff.x ? lower.x : higher.x, cutoff.y ? lower.y : higher.y, cutoff.z ? lower.z : higher.z);
}

vec3 srgb_to_linear(inout vec3 rgb)
{
    rgb = clamp(rgb, vec3(0.0), vec3(1.0));
    bvec3 cutoff = lessThan(rgb, vec3(0.040449999272823333740234375));
    vec3 lower = rgb / vec3(12.9200000762939453125);
    vec3 higher = pow((rgb + vec3(0.054999999701976776123046875)) / vec3(1.05499994754791259765625), vec3(2.400000095367431640625));
    return vec3(cutoff.x ? lower.x : higher.x, cutoff.y ? lower.y : higher.y, cutoff.z ? lower.z : higher.z);
}

vec3 dither_srgb(vec3 rgb, ivec2 icoords)
{
    ivec2 offset = ivec2(int(LIBRA_UNIFORM_FrameCount & 63u)) * ivec2(17, 13);
    vec3 _noise = texelFetch(LIBRA_TEXTURE_BlueNoiseTex, (icoords + offset) & ivec2(63), 0).xyz;
    vec3 param = rgb;
    vec3 _155 = linear_to_srgb(param);
    vec3 lower = floor(_155 / vec3(0.0039215688593685626983642578125)) * 0.0039215688593685626983642578125;
    vec3 upper = lower + vec3(0.0039215688593685626983642578125);
    vec3 param_1 = lower;
    vec3 _168 = srgb_to_linear(param_1);
    vec3 param_2 = upper;
    vec3 _171 = srgb_to_linear(param_2);
    vec3 threshold = mix(_168, _171, _noise);
    bvec3 _178 = lessThan(threshold, rgb);
    return vec3(_178.x ? upper.x : lower.x, _178.y ? upper.y : lower.y, _178.z ? upper.z : lower.z);
}

vec3 linear_to_gamma(inout vec3 rgb)
{
    rgb = clamp(rgb, vec3(0.0), vec3(1.0));
    return pow(rgb, vec3(0.4545454680919647216796875));
}

vec3 gamma_to_linear(inout vec3 rgb)
{
    rgb = clamp(rgb, vec3(0.0), vec3(1.0));
    return pow(rgb, vec3(2.2000000476837158203125));
}

vec3 dither_gamma(vec3 rgb, ivec2 icoords)
{
    ivec2 offset = ivec2(int(LIBRA_UNIFORM_FrameCount & 63u)) * ivec2(17, 13);
    vec3 _noise = texelFetch(LIBRA_TEXTURE_BlueNoiseTex, (icoords + offset) & ivec2(63), 0).xyz;
    vec3 param = rgb;
    vec3 _202 = linear_to_gamma(param);
    vec3 lower = floor(_202 / vec3(0.0039215688593685626983642578125)) * 0.0039215688593685626983642578125;
    vec3 upper = lower + vec3(0.0039215688593685626983642578125);
    vec3 param_1 = lower;
    vec3 _214 = gamma_to_linear(param_1);
    vec3 param_2 = upper;
    vec3 _217 = gamma_to_linear(param_2);
    vec3 threshold = mix(_214, _217, _noise);
    bvec3 _224 = lessThan(threshold, rgb);
    return vec3(_224.x ? upper.x : lower.x, _224.y ? upper.y : lower.y, _224.z ? upper.z : lower.z);
}

void main()
{
    vec3 blur_value = textureLod(LIBRA_TEXTURE_Source, LIBRA_VARYING_0, 0.0).xyz;
    ivec2 icoords = ivec2(floor(LIBRA_VARYING_0 * LIBRA_UNIFORM_ScanlinesSize.xy));
    vec3 scanlines_value = texelFetch(LIBRA_TEXTURE_Scanlines, icoords, 0).xyz;
    vec3 out_value = mix(scanlines_value, blur_value, vec3(LIBRA_UNIFORM_GlowAmount));
    if (LIBRA_UNIFORM_OutputGamma < 0.5)
    {
        vec3 param = out_value;
        ivec2 param_1 = icoords;
        out_value = dither_srgb(param, param_1);
    }
    else
    {
        vec3 param_2 = out_value;
        ivec2 param_3 = icoords;
        out_value = dither_gamma(param_2, param_3);
    }
    FragColor = vec4(out_value, 1.0);
}
