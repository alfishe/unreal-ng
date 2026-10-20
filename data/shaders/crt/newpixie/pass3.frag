#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_use_frame;
uniform float LIBRA_UNIFORM_curvature;
uniform float LIBRA_UNIFORM_wiggle_toggle;
uniform float LIBRA_UNIFORM_scanroll;
uniform float LIBRA_UNIFORM_vignette;
uniform float LIBRA_UNIFORM_ghosting;

uniform sampler2D LIBRA_TEXTURE_accum1;
uniform sampler2D LIBRA_TEXTURE_blur2;
uniform sampler2D LIBRA_TEXTURE_frametexture;
uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec2 curve(inout vec2 uv)
{
    uv -= vec2(0.5);
    uv *= vec2(0.925000011920928955078125, 1.0950000286102294921875);
    uv *= LIBRA_UNIFORM_curvature;
    uv.x *= (1.0 + pow(abs(uv.y) / 4.0, 2.0));
    uv.y *= (1.0 + pow(abs(uv.x) / 3.0, 2.0));
    uv /= vec2(LIBRA_UNIFORM_curvature);
    uv += vec2(0.5);
    uv = (uv * 0.920000016689300537109375) + vec2(0.039999999105930328369140625);
    return uv;
}

vec3 tsample(sampler2D samp, inout vec2 tc, float offs, vec2 resolution)
{
    tc = (tc * vec2(1.02499997615814208984375, 0.920000016689300537109375)) + vec2(-0.012500000186264514923095703125, 0.039999999105930328369140625);
    vec3 s = pow(abs(texture(samp, vec2(tc.x, 1.0 - tc.y)).xyz), vec3(2.2000000476837158203125));
    return s * vec3(1.25);
}

vec3 filmic(vec3 LinearColor)
{
    vec3 x = max(vec3(0.0), LinearColor - vec3(0.0040000001899898052215576171875));
    return (x * ((x * 6.19999980926513671875) + vec3(0.5))) / ((x * ((x * 6.19999980926513671875) + vec3(1.7000000476837158203125))) + vec3(0.0599999986588954925537109375));
}

float rand(vec2 co)
{
    return fract(sin(dot(co, vec2(12.98980045318603515625, 78.233001708984375))) * 43758.546875);
}

void main()
{
    float time = mod(float(LIBRA_UNIFORM_FrameCount), 849.0) * 36.0;
    vec2 uv = LIBRA_VARYING_0;
    vec2 param = uv;
    vec2 _181 = curve(param);
    vec2 curved_uv = mix(_181, uv, vec2(0.4000000059604644775390625));
    float scale = -0.101000003516674041748046875;
    vec2 scuv = ((curved_uv * (1.0 - scale)) + vec2(scale / 2.0)) + vec2(0.0030000000260770320892333984375, -0.001000000047497451305389404296875);
    uv = scuv;
    float x = (((LIBRA_UNIFORM_wiggle_toggle * sin((0.100000001490116119384765625 * time) + (curved_uv.y * 13.0))) * sin((0.23000000417232513427734375 * time) + (curved_uv.y * 19.0))) * sin((0.300000011920928955078125 + (0.10999999940395355224609375 * time)) + (curved_uv.y * 23.0))) * 0.00120000005699694156646728515625;
    float o = sin((LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy).y * 1.5) / LIBRA_UNIFORM_OutputSize.x;
    x += (o * 0.25);
    time = mod(float(LIBRA_UNIFORM_FrameCount), 640.0) * 1.0;
    vec2 param_1 = vec2((x + scuv.x) + 0.000899999984540045261383056640625, scuv.y + 0.000899999984540045261383056640625);
    float param_2 = LIBRA_UNIFORM_OutputSize.y / 800.0;
    vec2 param_3 = LIBRA_UNIFORM_OutputSize.xy;
    vec3 _288 = tsample(LIBRA_TEXTURE_accum1, param_1, param_2, param_3);
    vec3 col;
    col.x = _288.x + 0.0199999995529651641845703125;
    vec2 param_4 = vec2((x + scuv.x) + 0.0, scuv.y - 0.0010999999940395355224609375);
    float param_5 = LIBRA_UNIFORM_OutputSize.y / 800.0;
    vec2 param_6 = LIBRA_UNIFORM_OutputSize.xy;
    vec3 _312 = tsample(LIBRA_TEXTURE_accum1, param_4, param_5, param_6);
    col.y = _312.y + 0.0199999995529651641845703125;
    vec2 param_7 = vec2((x + scuv.x) - 0.00150000001303851604461669921875, scuv.y + 0.0);
    float param_8 = LIBRA_UNIFORM_OutputSize.y / 800.0;
    vec2 param_9 = LIBRA_UNIFORM_OutputSize.xy;
    vec3 _335 = tsample(LIBRA_TEXTURE_accum1, param_7, param_8, param_9);
    col.z = _335.z + 0.0199999995529651641845703125;
    float i = clamp(((col.x * 0.2989999949932098388671875) + (col.y * 0.58700001239776611328125)) + (col.z * 0.114000000059604644775390625), 0.0, 1.0);
    i = pow(1.0 - pow(i, 2.0), 1.0);
    i = ((1.0 - i) * 0.85000002384185791015625) + 0.1500000059604644775390625;
    float ghs = 0.1500000059604644775390625 * LIBRA_UNIFORM_ghosting;
    vec2 param_10 = ((vec2(x - 0.01400000043213367462158203125, -0.02700000070035457611083984375) * 0.85000002384185791015625) + (vec2(0.3499999940395355224609375 * sin((0.14285714924335479736328125 + (15.0 * curved_uv.y)) + (0.89999997615814208984375 * time)), 0.3499999940395355224609375 * sin((0.2857142984867095947265625 + (10.0 * curved_uv.y)) + (1.37000000476837158203125 * time))) * 0.007000000216066837310791015625)) + vec2(scuv.x + 0.001000000047497451305389404296875, scuv.y + 0.001000000047497451305389404296875);
    float param_11 = 5.5 + (1.2999999523162841796875 * sin((0.3333333432674407958984375 + (31.0 * curved_uv.x)) + (1.7000000476837158203125 * time)));
    vec2 param_12 = LIBRA_UNIFORM_OutputSize.xy;
    vec3 _437 = tsample(LIBRA_TEXTURE_blur2, param_10, param_11, param_12);
    vec3 r = _437 * vec3(0.5, 0.25, 0.25);
    vec2 param_13 = ((vec2(x - 0.01899999938905239105224609375, -0.0199999995529651641845703125) * 0.85000002384185791015625) + (vec2(0.3499999940395355224609375 * cos((0.111111111938953399658203125 + (15.0 * curved_uv.y)) + (0.5 * time)), 0.3499999940395355224609375 * sin((0.22222222387790679931640625 + (10.0 * curved_uv.y)) + (1.5 * time))) * 0.007000000216066837310791015625)) + vec2(scuv.x + 0.0, scuv.y - 0.00200000009499490261077880859375);
    float param_14 = 5.400000095367431640625 + (1.2999999523162841796875 * sin((1.0 + (71.0 * curved_uv.x)) + (1.89999997615814208984375 * time)));
    vec2 param_15 = LIBRA_UNIFORM_OutputSize.xy;
    vec3 _498 = tsample(LIBRA_TEXTURE_blur2, param_13, param_14, param_15);
    vec3 g = _498 * vec3(0.25, 0.5, 0.25);
    vec2 param_16 = ((vec2(x - 0.0170000009238719940185546875, -0.0030000000260770320892333984375) * 0.85000002384185791015625) + (vec2(0.3499999940395355224609375 * sin((0.666666686534881591796875 + (15.0 * curved_uv.y)) + (0.699999988079071044921875 * time)), 0.3499999940395355224609375 * cos((0.666666686534881591796875 + (10.0 * curved_uv.y)) + (1.62999999523162841796875 * time))) * 0.007000000216066837310791015625)) + vec2(scuv.x - 0.00200000009499490261077880859375, scuv.y + 0.0);
    float param_17 = 5.30000019073486328125 + (1.2999999523162841796875 * sin((0.4285714328289031982421875 + (91.0 * curved_uv.x)) + (1.64999997615814208984375 * time)));
    vec2 param_18 = LIBRA_UNIFORM_OutputSize.xy;
    vec3 _560 = tsample(LIBRA_TEXTURE_blur2, param_16, param_17, param_18);
    vec3 b = _560 * vec3(0.25, 0.25, 0.5);
    col += ((vec3(ghs * 0.7009999752044677734375) * pow(clamp(vec3(3.0) * r, vec3(0.0), vec3(1.0)), vec3(2.0))) * vec3(i));
    col += ((vec3(ghs * 0.41299998760223388671875) * pow(clamp(vec3(3.0) * g, vec3(0.0), vec3(1.0)), vec3(2.0))) * vec3(i));
    col += ((vec3(ghs * 0.885999977588653564453125) * pow(clamp(vec3(3.0) * b, vec3(0.0), vec3(1.0)), vec3(2.0))) * vec3(i));
    col *= vec3(0.949999988079071044921875, 1.0499999523162841796875, 0.949999988079071044921875);
    col = clamp(((col * 1.2999999523162841796875) + ((col * 0.75) * col)) + (((((col * 1.25) * col) * col) * col) * col), vec3(0.0), vec3(10.0));
    float vig = (1.0 - (0.9900000095367431640625 * LIBRA_UNIFORM_vignette)) + ((((16.0 * curved_uv.x) * curved_uv.y) * (1.0 - curved_uv.x)) * (1.0 - curved_uv.y));
    vig = 1.2999999523162841796875 * pow(vig, 0.5);
    col *= vig;
    time *= LIBRA_UNIFORM_scanroll;
    float scans = clamp(0.3499999940395355224609375 + (0.180000007152557373046875 * sin((6.0 * time) - ((curved_uv.y * LIBRA_UNIFORM_OutputSize.y) * 1.5))), 0.0, 1.0);
    float s = pow(scans, 0.89999997615814208984375);
    col *= vec3(s);
    col *= (1.0 - (0.23000000417232513427734375 * clamp(mod((LIBRA_VARYING_0 * LIBRA_UNIFORM_OutputSize.xy).x, 3.0) / 2.0, 0.0, 1.0)));
    vec3 param_19 = col;
    col = filmic(param_19);
    vec2 seed = curved_uv * LIBRA_UNIFORM_OutputSize.xy;
    vec2 param_20 = seed + vec2(time);
    vec2 param_21 = seed + vec2(time * 2.0);
    vec2 param_22 = seed + vec2(time * 3.0);
    col -= (pow(vec3(rand(param_20), rand(param_21), rand(param_22)), vec3(1.5)) * 0.014999999664723873138427734375);
    col *= (1.0 - (0.0040000001899898052215576171875 * ((sin((50.0 * time) + (curved_uv.y * 2.0)) * 0.5) + 0.5)));
    uv = curved_uv;
    vec2 fscale = vec2(0.026000000536441802978515625, -0.017999999225139617919921875);
    uv = vec2(uv.x, 1.0 - uv.y);
    vec4 f = texture(LIBRA_TEXTURE_frametexture, LIBRA_VARYING_0);
    vec4 _771 = f;
    vec3 _775 = mix(_771.xyz, vec3(0.5), vec3(0.5));
    f.x = _775.x;
    f.y = _775.y;
    f.z = _775.z;
    float fvig = clamp((-0.0) + ((((512.0 * uv.x) * uv.y) * (1.0 - uv.x)) * (1.0 - uv.y)), 0.20000000298023223876953125, 0.800000011920928955078125);
    col = mix(col, mix(max(col, vec3(0.0)), pow(abs(f.xyz), vec3(1.39999997615814208984375)) * fvig, vec3(f.w * f.w)), vec3(LIBRA_UNIFORM_use_frame));
    FragColor = vec4(col, 1.0);
}
