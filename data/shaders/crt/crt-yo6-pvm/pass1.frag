#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_TEX_CRT;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec3 _packed = texture(LIBRA_TEXTURE_TEX_CRT, LIBRA_VARYING_0).xyz;
    vec2 dudv = ((_packed.xy * 255.0) / vec2(16.0)) - vec2(7.0);
    vec2 uv0 = LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy;
    vec2 uv = (uv0 + dudv) / LIBRA_UNIFORM_SourceSize.xy;
    FragColor = vec4(texture(LIBRA_TEXTURE_Source, uv).xyz * _packed.z, 1.0);
}
