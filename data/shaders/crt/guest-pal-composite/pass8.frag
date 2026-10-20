#version 330

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_GAMMA_INPUT;
uniform float LIBRA_UNIFORM_downsample_levelx;
uniform float LIBRA_UNIFORM_downsample_levely;
uniform float LIBRA_UNIFORM_gamma_out;

uniform sampler2D LIBRA_TEXTURE_NtscPass;
uniform sampler2D LIBRA_TEXTURE_InterlacePass;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 fetch_pixel(vec2 coord)
{
    vec2 dx = vec2(LIBRA_UNIFORM_SourceSize.z, 0.0) * LIBRA_UNIFORM_downsample_levelx;
    vec2 dy = vec2(0.0, LIBRA_UNIFORM_SourceSize.w) * LIBRA_UNIFORM_downsample_levely;
    vec2 d1 = dx + dy;
    vec2 d2 = dx - dy;
    float sum = 15.0;
    vec3 result = ((((((((texture(LIBRA_TEXTURE_NtscPass, coord).xyz * 3.0) + (texture(LIBRA_TEXTURE_NtscPass, coord + dx).xyz * 2.0)) + (texture(LIBRA_TEXTURE_NtscPass, coord - dx).xyz * 2.0)) + (texture(LIBRA_TEXTURE_NtscPass, coord + dy).xyz * 2.0)) + (texture(LIBRA_TEXTURE_NtscPass, coord - dy).xyz * 2.0)) + texture(LIBRA_TEXTURE_NtscPass, coord + d1).xyz) + texture(LIBRA_TEXTURE_NtscPass, coord - d1).xyz) + texture(LIBRA_TEXTURE_NtscPass, coord + d2).xyz) + texture(LIBRA_TEXTURE_NtscPass, coord - d2).xyz;
    return result / vec3(sum);
}

void main()
{
    vec3 c = texture(LIBRA_TEXTURE_NtscPass, LIBRA_VARYING_0).xyz;
    if ((LIBRA_UNIFORM_downsample_levelx + LIBRA_UNIFORM_downsample_levely) > 0.02500000037252902984619140625)
    {
        vec2 param = LIBRA_VARYING_0;
        c = fetch_pixel(param);
    }
    float intera = texture(LIBRA_TEXTURE_InterlacePass, vec2(0.75)).w;
    float gamma_in = 1.0;
    c = pow(c, vec3(gamma_in));
    if (LIBRA_VARYING_0.x > 0.5)
    {
        gamma_in = intera;
    }
    else
    {
        gamma_in = 1.0 / gamma_in;
    }
    FragColor = vec4(c, gamma_in);
}
