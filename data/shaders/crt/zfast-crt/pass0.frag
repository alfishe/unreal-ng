#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_BLURSCALEX;
uniform float LIBRA_UNIFORM_LOWLUMSCAN;
uniform float LIBRA_UNIFORM_HILUMSCAN;
uniform float LIBRA_UNIFORM_BRIGHTBOOST;
uniform float LIBRA_UNIFORM_MASK_DARK;
uniform float LIBRA_UNIFORM_MASK_FADE;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_2;
layout(location = 0) out vec4 FragColor;
in float LIBRA_VARYING_1;

void main()
{
    vec2 p = LIBRA_VARYING_0 * LIBRA_UNIFORM_SourceSize.xy;
    vec2 i = floor(p) + vec2(0.5);
    vec2 f = p - i;
    p = (i + (((f * 4.0) * f) * f)) * LIBRA_VARYING_2;
    p.x = mix(p.x, LIBRA_VARYING_0.x, LIBRA_UNIFORM_BLURSCALEX);
    float Y = f.y * f.y;
    float YY = Y * Y;
    float whichmask = fract(floor(LIBRA_VARYING_0.x * LIBRA_UNIFORM_OutputSize.x) * (-0.4999000132083892822265625));
    float mask = 1.0 + (float(whichmask < 0.5) * (-LIBRA_UNIFORM_MASK_DARK));
    vec3 colour = texture(LIBRA_TEXTURE_Source, p).xyz;
    float scanLineWeight = LIBRA_UNIFORM_BRIGHTBOOST - (LIBRA_UNIFORM_LOWLUMSCAN * (Y - (2.0499999523162841796875 * YY)));
    float scanLineWeightB = 1.0 - (LIBRA_UNIFORM_HILUMSCAN * (YY - ((2.7999999523162841796875 * YY) * Y)));
    vec3 _146 = colour * mix(scanLineWeight * mask, scanLineWeightB, dot(colour, vec3(LIBRA_VARYING_1)));
    FragColor.x = _146.x;
    FragColor.y = _146.y;
    FragColor.z = _146.z;
}
