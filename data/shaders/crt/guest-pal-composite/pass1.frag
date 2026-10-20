#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_PR;
uniform float LIBRA_UNIFORM_PG;
uniform float LIBRA_UNIFORM_PB;
uniform float LIBRA_UNIFORM_esrc;
uniform float LIBRA_UNIFORM_bth;

uniform sampler2D LIBRA_TEXTURE_OriginalHistory0;
uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_AfterglowPassFeedback;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec2 dx = vec2(LIBRA_UNIFORM_OriginalSize.z, 0.0);
    vec2 dy = vec2(0.0, LIBRA_UNIFORM_OriginalSize.w);
    vec3 color0 = texture(LIBRA_TEXTURE_OriginalHistory0, LIBRA_VARYING_0).xyz;
    vec3 color1 = texture(LIBRA_TEXTURE_OriginalHistory0, LIBRA_VARYING_0 - dx).xyz;
    vec3 color2 = texture(LIBRA_TEXTURE_OriginalHistory0, LIBRA_VARYING_0 + dx).xyz;
    vec3 color3 = texture(LIBRA_TEXTURE_OriginalHistory0, LIBRA_VARYING_0 - dy).xyz;
    vec3 color4 = texture(LIBRA_TEXTURE_OriginalHistory0, LIBRA_VARYING_0 + dy).xyz;
    if (LIBRA_UNIFORM_esrc > 1.5)
    {
        color0 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
        color1 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 - dx).xyz;
        color2 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + dx).xyz;
        color3 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 - dy).xyz;
        color4 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + dy).xyz;
    }
    vec3 color = (((((color0 * 2.5) + color1) + color2) + color3) + color4) / vec3(6.5);
    vec3 accumulate = texture(LIBRA_TEXTURE_AfterglowPassFeedback, LIBRA_VARYING_0).xyz;
    float w = 1.0;
    float b = LIBRA_UNIFORM_bth / 255.0;
    float c = max(max(color0.x, color0.y), color0.z);
    w = smoothstep(b, 2.0 * b, c);
    vec3 result = mix(max(mix(color, accumulate, vec3(0.4900000095367431640625) + vec3(LIBRA_UNIFORM_PR, LIBRA_UNIFORM_PG, LIBRA_UNIFORM_PB)) - vec3(0.0049019609577953815460205078125), vec3(0.0)), color, vec3(w));
    FragColor = vec4(result, w);
}
