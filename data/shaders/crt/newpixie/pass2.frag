#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_blur_y;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
vec2 blur;

void main()
{
    blur = vec2(0.0, LIBRA_UNIFORM_blur_y) * LIBRA_UNIFORM_OutputSize.zw;
    vec2 uv = LIBRA_VARYING_0;
    vec4 sum = texture(LIBRA_TEXTURE_Source, uv) * 0.2270270287990570068359375;
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x - (4.0 * blur.x), uv.y - (4.0 * blur.y))) * 0.01621621660888195037841796875);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x - (3.0 * blur.x), uv.y - (3.0 * blur.y))) * 0.0540540553629398345947265625);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x - (2.0 * blur.x), uv.y - (2.0 * blur.y))) * 0.12162162363529205322265625);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x - (1.0 * blur.x), uv.y - (1.0 * blur.y))) * 0.1945945918560028076171875);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x + (1.0 * blur.x), uv.y + (1.0 * blur.y))) * 0.1945945918560028076171875);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x + (2.0 * blur.x), uv.y + (2.0 * blur.y))) * 0.12162162363529205322265625);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x + (3.0 * blur.x), uv.y + (3.0 * blur.y))) * 0.0540540553629398345947265625);
    sum += (texture(LIBRA_TEXTURE_Source, vec2(uv.x + (4.0 * blur.x), uv.y + (4.0 * blur.y))) * 0.01621621660888195037841796875);
    FragColor = sum;
}
