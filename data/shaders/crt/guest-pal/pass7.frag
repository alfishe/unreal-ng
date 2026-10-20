#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_PAL_FILT;
uniform float LIBRA_UNIFORM_pal_scale;
uniform float LIBRA_UNIFORM_pal_ring;
uniform float LIBRA_UNIFORM_pal_res;
uniform float LIBRA_UNIFORM_GCORR;
uniform float LIBRA_UNIFORM_RFNOISE;
uniform float LIBRA_UNIFORM_RFNOISE1;
uniform float LIBRA_UNIFORM_RFNOISE2;
uniform float LIBRA_UNIFORM_CHROMAF;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;
float RFNOISE;
float CON_TYPE[4];
float luma_weight_pal_25[25];

vec3 sample_yuv(vec3 dx)
{
    return vec3(texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + dx.xz).x + texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 - dx.xz).x, texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + dx.yz).yz + texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 - dx.yz).yz);
}

vec2 psrnd(vec2 TexCoord, float FrameCount)
{
    vec3 p = vec3(TexCoord * 16758.544921875, FrameCount);
    float x = fract(sin(dot(p, vec3(12.98980045318603515625, 78.233001708984375, 3.183000087738037109375))) * 43758.546875);
    float y = fract(sin(dot(p, vec3(25.9796009063720703125, 14.11299991607666015625, 11.270999908447265625))) * 96321.9140625);
    return vec2(x, y);
}

float smothstep(float e0, float e1, float x)
{
    return clamp((x - e0) / (e1 - e0), 0.0, 1.0);
}

vec3 Noise(inout vec2 TexCoord, float FrameCount)
{
    vec2 param = TexCoord;
    float param_1 = FrameCount;
    TexCoord = psrnd(param, param_1);
    vec3 p = vec3(TexCoord * 758.5452880859375, FrameCount);
    float res1 = fract(sin(dot(p, vec3(12.98983478546142578125, 78.2334136962890625, 0.16452999413013458251953125))) * 43758.546875);
    float res2 = fract(sin(dot(p, vec3(39.346790313720703125, 11.13523006439208984375, 83.155731201171875))) * 39459.32421875);
    float res3 = fract(sin(dot(p, vec3(73.15691375732421875, 52.23503875732421875, 9.15196990966796875))) * 60493.84765625);
    float lns = float(abs(res1 - 0.5) < (0.20000000298023223876953125 * RFNOISE));
    float cny = float(abs(res2 - 0.5) < (0.5 * RFNOISE));
    float cnz = float(abs(res3 - 0.5) < (0.5 * RFNOISE));
    float param_2 = 0.5 - (0.20000000298023223876953125 * RFNOISE);
    float param_3 = 0.5 + (0.20000000298023223876953125 * RFNOISE);
    float param_4 = res1;
    lns = ((lns * smothstep(param_2, param_3, param_4)) * 0.660000026226043701171875) - (0.3300000131130218505859375 * lns);
    float param_5 = 0.5 - (0.5 * RFNOISE);
    float param_6 = 0.5 + (0.5 * RFNOISE);
    float param_7 = res2;
    cny = (cny * smothstep(param_5, param_6, param_7)) - (0.5 * cny);
    float param_8 = 0.5 - (0.5 * RFNOISE);
    float param_9 = 0.5 + (0.5 * RFNOISE);
    float param_10 = res3;
    cnz = (cnz * smothstep(param_8, param_9, param_10)) - (0.5 * cnz);
    return vec3(lns, cny, cnz);
}

vec3 yuv2rgb(vec3 yuv)
{
    return yuv * mat3(vec3(1.0, 0.0, 1.13982999324798583984375), vec3(1.0, -0.3946500122547149658203125, -0.5806000232696533203125), vec3(1.0, 2.0321099758148193359375, 0.0));
}

void main()
{
    RFNOISE = (LIBRA_UNIFORM_RFNOISE * LIBRA_UNIFORM_RFNOISE) + 9.9999997473787516355514526367188e-06;
    CON_TYPE = float[](288.0, 320.0, 410.0, 512.0);
    luma_weight_pal_25 = float[](-0.0164999999105930328369140625, -0.02021799981594085693359375, -0.02500000037252902984619140625, -0.02946900017559528350830078125, -0.03350000083446502685546875, -0.03506200015544891357421875, -0.0384999997913837432861328125, -0.0475619994103908538818359375, -0.061999998986721038818359375, -0.084468998014926910400390625, -0.10999999940395355224609375, -0.14171899855136871337890625, -0.15800000727176666259765625, -0.14403100311756134033203125, -0.082500003278255462646484375, 0.063469000160694122314453125, 0.25, 0.4601559937000274658203125, 0.64999997615814208984375, 0.74937498569488525390625, 0.819999992847442626953125, 0.915000021457672119140625, 0.9900000095367431640625, 0.99500000476837158203125, 1.0);
    float chroma_weight_pal_25[25] = float[](-0.0010999999940395355224609375, -0.00164999999105930328369140625, -0.0024999999441206455230712890625, -0.00370000000111758708953857421875, -0.0052000000141561031341552734375, -0.0068999999202787876129150390625, -0.008299999870359897613525390625, -0.0089999996125698089599609375, -0.008100000210106372833251953125, -0.00559999980032444000244140625, -0.002199999988079071044921875, 0.0003000000142492353916168212890625, 0.00230000005103647708892822265625, 0.0052000000141561031341552734375, 0.0089999996125698089599609375, 0.0236000008881092071533203125, 0.0489999987185001373291015625, 0.075199998915195465087890625, 0.1520999968051910400390625, 0.3397000133991241455078125, 0.4478000104427337646484375, 0.67599999904632568359375, 0.892000019550323486328125, 0.984000027179718017578125, 1.0);
    bool _352 = LIBRA_UNIFORM_CHROMAF > 1.5;
    bool _360;
    if (_352)
    {
        _360 = LIBRA_UNIFORM_PAL_FILT < 2.5;
    }
    else
    {
        _360 = _352;
    }
    if (_360)
    {
        chroma_weight_pal_25 = float[](0.0039800000376999378204345703125, 0.0008200000156648457050323486328125, -0.0052000000141561031341552734375, -0.008949999697506427764892578125, -0.0067500001750886440277099609375, 0.00120000005699694156646728515625, 0.007849999703466892242431640625, 0.00980000011622905731201171875, 0.004900000058114528656005859375, -0.003800000064074993133544921875, -0.010950000025331974029541015625, -0.00760000012814998626708984375, 0.005900000222027301788330078125, 0.016300000250339508056640625, 0.02009999938309192657470703125, -0.0227000005543231964111328125, -0.0164999999105930328369140625, 0.034000001847743988037109375, 0.078000001609325408935546875, 0.1480000019073486328125, 0.2899999916553497314453125, 0.4320000112056732177734375, 0.652000010013580322265625, 0.839999973773956298828125, 1.0);
    }
    float yres = CON_TYPE[int(LIBRA_UNIFORM_PAL_FILT) - 1];
    float _399;
    if (LIBRA_UNIFORM_pal_res < 1.5)
    {
        _399 = yres;
    }
    else
    {
        _399 = LIBRA_UNIFORM_OriginalSize.x;
    }
    float hres = _399;
    float cres = yres / 320.0;
    float res = LIBRA_UNIFORM_pal_scale;
    float one_x = (0.125 / hres) / res;
    float one_y = ((0.25 / hres) / res) / cres;
    vec3 signal = vec3(0.0);
    float offset = 0.0;
    vec3 wsum = vec3(0.0);
    vec3 sums = wsum;
    vec3 tmp = wsum;
    vec3 dx = vec3(one_x, one_y, 0.0);
    float mit = 1.0;
    float iloop = 24.0;
    vec3 dx1 = dx;
    for (int i = 0; i < 24; i++)
    {
        offset = float(i);
        vec2 _462 = dx.xy * (offset - iloop);
        dx1.x = _462.x;
        dx1.y = _462.y;
        vec3 param = dx1;
        sums = sample_yuv(param);
        tmp = vec3(luma_weight_pal_25[i], vec2(chroma_weight_pal_25[i]));
        wsum += tmp;
        signal += (sums * tmp);
    }
    tmp = vec3(luma_weight_pal_25[24], chroma_weight_pal_25[24], chroma_weight_pal_25[24]);
    wsum = (wsum + wsum) + tmp;
    signal += (texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz * tmp);
    signal /= wsum;
    if (LIBRA_UNIFORM_pal_ring > 0.0500000007450580596923828125)
    {
        vec2 dx_1 = vec2((8.0 * one_x) / min(res, 1.0), 0.0);
        float a = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 - (dx_1 * 2.0)).w;
        float b = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 - dx_1).w;
        float c = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + (dx_1 * 2.0)).w;
        float d = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + dx_1).w;
        float e = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).w;
        signal.x = mix(signal.x, clamp(signal.x, min(min(min(a, b), min(c, d)), e), max(max(max(a, b), max(c, d)), e)), LIBRA_UNIFORM_pal_ring);
    }
    signal.x = pow(max(signal.x, 0.0), 1.0 / LIBRA_UNIFORM_GCORR);
    if ((LIBRA_UNIFORM_RFNOISE1 + LIBRA_UNIFORM_RFNOISE2) > 0.004999999888241291046142578125)
    {
        vec2 param_1 = LIBRA_VARYING_0;
        float param_2 = float(LIBRA_UNIFORM_FrameCount);
        vec3 _622 = Noise(param_1, param_2);
        vec3 ns = _622;
        float Y = mix(0.375, 1.0, pow(signal.x, 0.20000000298023223876953125));
        signal.x = clamp(signal.x + (LIBRA_UNIFORM_RFNOISE1 * ns.x), 0.0, 1.0);
        signal.y = clamp(signal.y + (((Y * LIBRA_UNIFORM_RFNOISE2) * (0.324999988079071044921875 + (1.3250000476837158203125 * abs(signal.y)))) * ns.y), -0.449999988079071044921875, 0.449999988079071044921875);
        signal.z = clamp(signal.z + (((Y * LIBRA_UNIFORM_RFNOISE2) * (0.324999988079071044921875 + (1.3250000476837158203125 * abs(signal.z)))) * ns.z), -0.62000000476837158203125, 0.62000000476837158203125);
    }
    vec3 param_3 = signal;
    FragColor = vec4(clamp(yuv2rgb(param_3), vec3(0.0), vec3(1.0)), 1.0);
}
