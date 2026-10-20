#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform float LIBRA_UNIFORM_YIQ;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 gamma_to_yiq(vec3 rgb)
{
    return mat3(vec3(0.300000011920928955078125, 0.59899997711181640625, 0.212999999523162841796875), vec3(0.589999973773956298828125, -0.27730000019073486328125, -0.5250999927520751953125), vec3(0.10999999940395355224609375, -0.3217000067234039306640625, 0.312099993228912353515625)) * rgb;
}

void main()
{
    ivec2 pix_loc = ivec2(int(floor(LIBRA_VARYING_0.x * LIBRA_UNIFORM_SourceSize.x)), int(floor(LIBRA_VARYING_0.y * LIBRA_UNIFORM_SourceSize.y)));
    vec3 rgb = texelFetch(LIBRA_TEXTURE_Source, pix_loc, 0).xyz;
    if (LIBRA_UNIFORM_YIQ > 0.5)
    {
        vec3 param = rgb;
        rgb = gamma_to_yiq(param);
    }
    FragColor.x = rgb.x;
    FragColor.y = rgb.y;
    FragColor.z = rgb.z;
}
