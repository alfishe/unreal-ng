#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_Source;
uniform sampler2D LIBRA_TEXTURE_PalPass1Feedback;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 rgb2yuv(vec3 rgb)
{
    return rgb * mat3(vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625), vec3(-0.14712999761104583740234375, -0.2888599932193756103515625, 0.43599998950958251953125), vec3(0.6150000095367431640625, -0.51498997211456298828125, -0.100010000169277191162109375));
}

void main()
{
    vec3 param = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 ycolor = rgb2yuv(param);
    vec4 pcolor = texture(LIBRA_TEXTURE_PalPass1Feedback, LIBRA_VARYING_0);
    FragColor = vec4(ycolor.yz, pcolor.xy);
}
