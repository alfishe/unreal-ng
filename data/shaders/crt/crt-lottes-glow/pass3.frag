#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec3 col = vec3(0.0);
    float dy = LIBRA_UNIFORM_SourceSize.w;
    float k_total = 0.0;
    for (int i = -4; i <= 4; i++)
    {
        float k = exp(((-0.3499999940395355224609375) * float(i)) * float(i));
        k_total += k;
        col += (texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + vec2(0.0, float(i) * dy)).xyz * k);
    }
    FragColor = vec4(col / vec3(k_total), 1.0);
}
