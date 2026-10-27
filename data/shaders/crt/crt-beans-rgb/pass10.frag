#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform sampler2D LIBRA_TEXTURE_Source;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;

void main()
{
    vec3 _21 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    FragColor.x = _21.x;
    FragColor.y = _21.y;
    FragColor.z = _21.z;
}
