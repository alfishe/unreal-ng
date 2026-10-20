#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform float LIBRA_UNIFORM_Cutoff;
uniform float LIBRA_UNIFORM_ICutoff;
uniform float LIBRA_UNIFORM_QCutoff;
uniform float LIBRA_UNIFORM_YIQ;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 gamma_to_linear(vec3 rgb)
{
    return pow(clamp(rgb, vec3(0.0), vec3(1.0)), vec3(2.400000095367431640625));
}

vec3 yiq_to_linear(vec3 rgb)
{
    vec3 param = mat3(vec3(1.0), vec3(0.946882188320159912109375, -0.2747876346111297607421875, -1.1085450649261474609375), vec3(0.623556554317474365234375, -0.635691106319427490234375, 1.70900690555572509765625)) * rgb;
    return gamma_to_linear(param);
}

void main()
{
    vec3 _57;
    if (LIBRA_UNIFORM_YIQ > 0.5)
    {
        _57 = vec3(1.0) / ((vec3(LIBRA_UNIFORM_Cutoff, LIBRA_UNIFORM_ICutoff, LIBRA_UNIFORM_QCutoff) * 53.3300018310546875) * 2.0);
    }
    else
    {
        _57 = vec3(1.0) / ((vec3(LIBRA_UNIFORM_Cutoff) * 53.3300018310546875) * 2.0);
    }
    vec3 L = _57;
    float L_max = max(L.x, max(L.y, L.z));
    vec3 L_rcp = vec3(1.0) / L;
    vec3 filtered = vec3(0.0);
    int pix_y = int(floor(LIBRA_VARYING_0.y * LIBRA_UNIFORM_SourceSize.y));
    float t = LIBRA_VARYING_0.x;
    int start = max(int(floor(LIBRA_UNIFORM_SourceSize.x * (LIBRA_VARYING_0.x - L_max))), 0);
    int end = min(int(floor(LIBRA_UNIFORM_SourceSize.x * (LIBRA_VARYING_0.x + L_max))), (int(LIBRA_UNIFORM_SourceSize.x) - 1));
    float t0 = float(start) * LIBRA_UNIFORM_SourceSize.z;
    vec3 u0 = clamp(L_rcp * (t - t0), vec3(-1.0), vec3(1.0));
    vec3 left = (u0 * 3.1415927410125732421875) + sin(u0 * 3.1415927410125732421875);
    for (int pix_x = start; pix_x <= end; pix_x++)
    {
        vec3 s = texelFetch(LIBRA_TEXTURE_Source, ivec2(pix_x, pix_y), 0).xyz;
        float t1 = float(pix_x + 1) * LIBRA_UNIFORM_SourceSize.z;
        vec3 u1 = clamp(L_rcp * (t - t1), vec3(-1.0), vec3(1.0));
        vec3 right = (u1 * 3.1415927410125732421875) + sin(u1 * 3.1415927410125732421875);
        filtered += (s * (left - right));
        left = right;
    }
    filtered /= vec3(6.283185482025146484375);
    vec3 _237;
    if (LIBRA_UNIFORM_YIQ > 0.5)
    {
        vec3 param = filtered;
        _237 = yiq_to_linear(param);
    }
    else
    {
        vec3 param_1 = filtered;
        _237 = gamma_to_linear(param_1);
    }
    FragColor.x = _237.x;
    FragColor.y = _237.y;
    FragColor.z = _237.z;
}
