#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform float LIBRA_UNIFORM_CURVATURE_X;
uniform float LIBRA_UNIFORM_CURVATURE_Y;
uniform float LIBRA_UNIFORM_MASK_BRIGHTNESS;
uniform float LIBRA_UNIFORM_SCANLINE_WEIGHT;
uniform float LIBRA_UNIFORM_SCANLINE_GAP_BRIGHTNESS;
uniform float LIBRA_UNIFORM_BLOOM_FACTOR;
uniform float LIBRA_UNIFORM_INPUT_GAMMA;
uniform float LIBRA_UNIFORM_OUTPUT_GAMMA;

uniform sampler2D LIBRA_TEXTURE_Source;

in float LIBRA_VARYING_1;
in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
vec2 CURVATURE_DISTORTION;
vec2 barrelScale;

vec2 Distort(inout vec2 coord)
{
    coord -= vec2(0.5);
    float rsq = (coord.x * coord.x) + (coord.y * coord.y);
    coord += (coord * (CURVATURE_DISTORTION * rsq));
    coord *= barrelScale;
    bool _75 = abs(coord.x) >= 0.5;
    bool _83;
    if (!_75)
    {
        _83 = abs(coord.y) >= 0.5;
    }
    else
    {
        _83 = _75;
    }
    if (_83)
    {
        coord = vec2(-1.0);
    }
    else
    {
        coord += vec2(0.5);
    }
    return coord;
}

float CalcScanLineWeight(float dist)
{
    return max(1.0 - ((dist * dist) * LIBRA_UNIFORM_SCANLINE_WEIGHT), LIBRA_UNIFORM_SCANLINE_GAP_BRIGHTNESS);
}

float CalcScanLine(float dy)
{
    float param = dy;
    float scanLineWeight = CalcScanLineWeight(param);
    float param_1 = dy - LIBRA_VARYING_1;
    scanLineWeight += CalcScanLineWeight(param_1);
    float param_2 = dy + LIBRA_VARYING_1;
    scanLineWeight += CalcScanLineWeight(param_2);
    scanLineWeight *= 0.333333313465118408203125;
    return scanLineWeight;
}

void main()
{
    CURVATURE_DISTORTION = vec2(LIBRA_UNIFORM_CURVATURE_X, LIBRA_UNIFORM_CURVATURE_Y);
    barrelScale = vec2(1.0) - (CURVATURE_DISTORTION * 0.23000000417232513427734375);
    vec2 texcoord = LIBRA_VARYING_0;
    vec2 param = texcoord;
    vec2 _140 = Distort(param);
    texcoord = _140;
    if (texcoord.x < 0.0)
    {
        FragColor = vec4(0.0);
        return;
    }
    vec2 texcoordInPixels = texcoord * LIBRA_UNIFORM_SourceSize.xy;
    float tempCoord = floor(texcoordInPixels.y) + 0.5;
    float coord = tempCoord * LIBRA_UNIFORM_SourceSize.w;
    float deltas = texcoordInPixels.y - tempCoord;
    float param_1 = deltas;
    float scanLineWeight = CalcScanLine(param_1);
    float signs = sign(deltas);
    deltas = abs(deltas) * 2.0;
    deltas = (deltas * deltas) * deltas;
    deltas *= ((0.5 * LIBRA_UNIFORM_SourceSize.w) * signs);
    vec2 tc = vec2(texcoord.x, coord + deltas);
    vec3 colour = texture(LIBRA_TEXTURE_Source, tc).xyz;
    scanLineWeight *= LIBRA_UNIFORM_BLOOM_FACTOR;
    colour *= scanLineWeight;
    float whichMask = fract((LIBRA_VARYING_0.x * LIBRA_UNIFORM_OutputSize.x) * 0.333333313465118408203125);
    vec3 mask = vec3(LIBRA_UNIFORM_MASK_BRIGHTNESS);
    if (whichMask < 0.333333313465118408203125)
    {
        mask.x = 1.0;
    }
    else
    {
        if (whichMask < 0.66666662693023681640625)
        {
            mask.y = 1.0;
        }
        else
        {
            mask.z = 1.0;
        }
    }
    colour *= mask;
    FragColor = vec4(colour, 1.0);
}
