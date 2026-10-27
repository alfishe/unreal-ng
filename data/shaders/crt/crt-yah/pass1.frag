#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_InputSize;
uniform vec4 LIBRA_UNIFORM_FinalViewportSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_FrameTimeDelta;
uniform float LIBRA_UNIFORM_OriginalFPS;
uniform float LIBRA_UNIFORM_GLOBAL_MASTER;
uniform float LIBRA_UNIFORM_SCREEN_RESOLUTION_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_ORIENTATION;
uniform float LIBRA_UNIFORM_SCREEN_SCALE;
uniform float LIBRA_UNIFORM_SCREEN_FREQUENCY;

uniform float LIBRA_UNIFORM_NTSC_PROFILE;
uniform float LIBRA_UNIFORM_NTSC_SAMPLES;
uniform float LIBRA_UNIFORM_NTSC_PHASE;
uniform float LIBRA_UNIFORM_NTSC_SCALE;
uniform float LIBRA_UNIFORM_NTSC_SHIFT;
uniform float LIBRA_UNIFORM_NTSC_JITTER;

uniform sampler2D LIBRA_TEXTURE_Source;

layout(location = 0) out vec4 FragColor;
in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_1;
flat in int LIBRA_VARYING_6;
in vec3 LIBRA_VARYING_7;
flat in uint LIBRA_VARYING_8;
in float LIBRA_VARYING_5;
in float LIBRA_VARYING_2;
in float LIBRA_VARYING_3;
in float LIBRA_VARYING_4;

bool is_portrait(vec2 size)
{
    return size.y > size.x;
}

int get_orientation(vec2 size)
{
    vec2 param = size;
    return int(is_portrait(param));
}

int get_orientation(vec2 size, int orientation)
{
    int _78;
    if (orientation > 0)
    {
        _78 = orientation - 1;
    }
    else
    {
        vec2 param = size;
        _78 = get_orientation(param);
    }
    return _78;
}

vec3 rgb_to_yiq(vec3 rgb)
{
    return rgb * mat3(vec3(0.29890000820159912109375, 0.58700001239776611328125, 0.114000000059604644775390625), vec3(0.595899999141693115234375, -0.2743999958038330078125, -0.3215999901294708251953125), vec3(0.21150000393390655517578125, -0.52289998531341552734375, 0.311399996280670166015625));
}

vec3 pass1(inout vec3 yiq, vec2 pixCoord, int phase, vec3 chromaProfile, uint frameCount, mat3 mix3x3)
{
    float chromaFrequency = chromaProfile.x;
    float chromaAmplitude = chromaProfile.y;
    float chromaShift = chromaProfile.z;
    float chromaPhase = ((chromaAmplitude * (mod(pixCoord.y, float(phase)) + mod(float(frameCount), 2.0))) * chromaShift) + (chromaFrequency * pixCoord.x);
    float i = cos(chromaPhase);
    float q = sin(chromaPhase);
    vec3 _146 = yiq;
    vec2 _148 = _146.yz * vec2(i, q);
    yiq.y = _148.x;
    yiq.z = _148.y;
    yiq *= mix3x3;
    vec3 _159 = yiq;
    vec2 _161 = _159.yz * vec2(i, q);
    yiq.y = _161.x;
    yiq.z = _161.y;
    return yiq;
}

vec3 pass1(sampler2D source, vec2 texCoord, vec2 pixCoord, int phase, vec3 chromaProfile, float jitter, uint frameCount, mat3 mix3x3)
{
    vec3 rgb = texture(source, texCoord).xyz;
    vec3 param = rgb;
    vec3 yiq = rgb_to_yiq(param);
    uint frame0 = 0u;
    uint frame1 = (jitter > 0.0) ? frameCount : 1u;
    vec3 param_1 = yiq;
    vec2 param_2 = pixCoord;
    int param_3 = phase;
    vec3 param_4 = chromaProfile;
    uint param_5 = frame0;
    mat3 param_6 = mix3x3;
    vec3 _199 = pass1(param_1, param_2, param_3, param_4, param_5, param_6);
    vec3 yiq0 = _199;
    vec3 param_7 = yiq;
    vec2 param_8 = pixCoord;
    int param_9 = phase;
    vec3 param_10 = chromaProfile;
    uint param_11 = frame1;
    mat3 param_12 = mix3x3;
    vec3 _213 = pass1(param_7, param_8, param_9, param_10, param_11, param_12);
    vec3 yiq1 = _213;
    if (jitter > 0.0)
    {
        yiq = mix(yiq0, yiq1, vec3(jitter));
    }
    else
    {
        yiq = mix(yiq0, (yiq0 + yiq1) * 0.5, vec3(abs(jitter)));
    }
    return yiq;
}

void main()
{
    if (LIBRA_UNIFORM_NTSC_PROFILE == 0.0)
    {
        FragColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
        return;
    }
    vec2 param = LIBRA_UNIFORM_SourceSize.xy;
    int param_1 = int(LIBRA_UNIFORM_SCREEN_ORIENTATION);
    if (get_orientation(param, param_1) != 0)
    {
        FragColor = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0);
        return;
    }
    vec2 param_2 = LIBRA_VARYING_0;
    vec2 param_3 = LIBRA_VARYING_1;
    int param_4 = LIBRA_VARYING_6;
    vec3 param_5 = LIBRA_VARYING_7;
    float param_6 = LIBRA_UNIFORM_NTSC_JITTER;
    uint param_7 = LIBRA_VARYING_8;
    mat3 param_8 = mat3(vec3(LIBRA_VARYING_5, LIBRA_VARYING_2, LIBRA_VARYING_2), vec3(LIBRA_VARYING_3, 2.0 * LIBRA_VARYING_4, 0.0), vec3(LIBRA_VARYING_3, 0.0, 2.0 * LIBRA_VARYING_4));
    vec3 yiq = pass1(LIBRA_TEXTURE_Source, param_2, param_3, param_4, param_5, param_6, param_7, param_8);
    FragColor = vec4(yiq, 1.0);
}
