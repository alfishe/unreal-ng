#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_TNTC;
uniform float LIBRA_UNIFORM_LS;
uniform float LIBRA_UNIFORM_LUTLOW;
uniform float LIBRA_UNIFORM_LUTBR;
uniform float LIBRA_UNIFORM_CP;
uniform float LIBRA_UNIFORM_CS;
uniform float LIBRA_UNIFORM_WP;
uniform float LIBRA_UNIFORM_wp_saturation;
uniform float LIBRA_UNIFORM_AS;
uniform float LIBRA_UNIFORM_agsat;
uniform float LIBRA_UNIFORM_BP;
uniform float LIBRA_UNIFORM_GPGXmscf;
uniform float LIBRA_UNIFORM_sega_fix;
uniform float LIBRA_UNIFORM_sms_blue;
uniform float LIBRA_UNIFORM_MD_Pal;
uniform float LIBRA_UNIFORM_IREc;
uniform float LIBRA_UNIFORM_pre_bb;
uniform float LIBRA_UNIFORM_contr;
uniform float LIBRA_UNIFORM_pre_gc;

uniform sampler2D LIBRA_TEXTURE_StockPass;
uniform sampler2D LIBRA_TEXTURE_AfterglowPass;
uniform sampler2D LIBRA_TEXTURE_SamplerLUT1;
uniform sampler2D LIBRA_TEXTURE_SamplerLUT2;
uniform sampler2D LIBRA_TEXTURE_SamplerLUT3;
uniform sampler2D LIBRA_TEXTURE_SamplerLUT4;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

float contrast_sigmoid_inv(inout float imgColor, inout float cont, float pivot)
{
    cont = pow(cont - 1.0, 3.0);
    float knee = 1.0 / (1.0 + exp(cont * pivot));
    float shldr = 1.0 / (1.0 + exp(cont * (pivot - 1.0)));
    imgColor = pivot - (log((1.0 / ((imgColor * (shldr - knee)) + knee)) - 1.0) / cont);
    return imgColor;
}

vec3 fix_lut(vec3 lutcolor, inout vec3 ref)
{
    float r = length(ref);
    float l = length(lutcolor);
    float m = max(max(ref.x, ref.y), ref.z);
    ref = normalize(lutcolor + vec3(1.0000000116860974230803549289703e-07)) * mix(r, l, pow(m, 1.25));
    return mix(lutcolor, ref, vec3(1.0));
}

vec3 plant(vec3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06;
    return (tar * r) / vec3(t);
}

float contrast(float x)
{
    return max(mix(x, smoothstep(0.0, 1.0, x), LIBRA_UNIFORM_contr), 0.0);
}

vec3 pgc(vec3 c)
{
    float mc = max(max(c.x, c.y), c.z);
    float mg = pow(mc, 1.0 / LIBRA_UNIFORM_pre_gc);
    return (c * mg) / vec3(mc + 9.9999999392252902907785028219223e-09);
}

float vignette(inout vec2 pos)
{
    vec2 b = (vec2(1.0) * vec2(1.0, LIBRA_UNIFORM_OriginalSize.x / LIBRA_UNIFORM_OriginalSize.y)) * 0.125;
    pos = clamp(pos, vec2(0.0), vec2(1.0));
    pos = abs((pos - vec2(0.5)) * 2.0);
    vec2 res = mix(vec2(0.0), vec2(1.0), smoothstep(vec2(1.0), vec2(1.0) - b, sqrt(pos)));
    res = pow(res, vec2(0.699999988079071044921875));
    return max(mix(1.0, sqrt(res.x * res.y), 0.0), 0.0);
}

void main()
{
    vec4 imgColor = texture(LIBRA_TEXTURE_StockPass, LIBRA_VARYING_0);
    vec4 aftglow = texture(LIBRA_TEXTURE_AfterglowPass, LIBRA_VARYING_0);
    float w = 1.0 - aftglow.w;
    float l = length(aftglow.xyz);
    vec4 _251 = aftglow;
    vec3 _264 = (normalize(pow(_251.xyz + vec3(0.00999999977648258209228515625), vec3(LIBRA_UNIFORM_agsat))) * (LIBRA_UNIFORM_AS * w)) * l;
    aftglow.x = _264.x;
    aftglow.y = _264.y;
    aftglow.z = _264.z;
    float IRED = 0.0;
    if (LIBRA_UNIFORM_IREc == 1.0)
    {
        IRED = -15.5;
    }
    if (LIBRA_UNIFORM_IREc == 2.0)
    {
        IRED = -11.5;
    }
    float bp = (w * (LIBRA_UNIFORM_BP + IRED)) / 255.0;
    if (LIBRA_UNIFORM_GPGXmscf == 1.0)
    {
        if (imgColor.x < 0.5)
        {
            if (imgColor.x > 0.25)
            {
                imgColor.x = 0.3333333432674407958984375;
            }
        }
        if (imgColor.y < 0.5)
        {
            if (imgColor.y > 0.25)
            {
                imgColor.y = 0.3333333432674407958984375;
            }
        }
        if (imgColor.z < 0.5)
        {
            if (imgColor.z > 0.25)
            {
                imgColor.z = 0.3333333432674407958984375;
            }
        }
        if (imgColor.x < 0.75)
        {
            if (imgColor.x > 0.5)
            {
                imgColor.x = 0.666666686534881591796875;
            }
        }
        if (imgColor.y < 0.75)
        {
            if (imgColor.y > 0.5)
            {
                imgColor.y = 0.666666686534881591796875;
            }
        }
        if (imgColor.z < 0.75)
        {
            if (imgColor.z > 0.5)
            {
                imgColor.z = 0.666666686534881591796875;
            }
        }
    }
    if (LIBRA_UNIFORM_GPGXmscf == 2.0)
    {
        if (imgColor.x < 0.5)
        {
            if (imgColor.x > 0.25)
            {
                imgColor.x = 0.3124183118343353271484375;
            }
        }
        if (imgColor.y < 0.5)
        {
            if (imgColor.y > 0.25)
            {
                imgColor.y = 0.3124183118343353271484375;
            }
        }
        if (imgColor.z < 0.5)
        {
            if (imgColor.z > 0.25)
            {
                imgColor.z = 0.3124183118343353271484375;
            }
        }
        if (imgColor.x < 0.75)
        {
            if (imgColor.x > 0.5)
            {
                imgColor.x = 0.624836623668670654296875;
            }
        }
        if (imgColor.y < 0.75)
        {
            if (imgColor.y > 0.5)
            {
                imgColor.y = 0.624836623668670654296875;
            }
        }
        if (imgColor.z < 0.75)
        {
            if (imgColor.z > 0.5)
            {
                imgColor.z = 0.624836623668670654296875;
            }
        }
        if (imgColor.x > 0.75)
        {
            if (imgColor.x > 0.75)
            {
                imgColor.x = 0.93725490570068359375;
            }
        }
        if (imgColor.y > 0.75)
        {
            if (imgColor.y > 0.75)
            {
                imgColor.y = 0.93725490570068359375;
            }
        }
        if (imgColor.z > 0.75)
        {
            if (imgColor.z > 0.75)
            {
                imgColor.z = 0.93725490570068359375;
            }
        }
    }
    if (LIBRA_UNIFORM_GPGXmscf == 3.0)
    {
        if (imgColor.x < 0.5)
        {
            if (imgColor.x > 0.25)
            {
                imgColor.x = 0.2745098173618316650390625;
            }
        }
        if (imgColor.y < 0.5)
        {
            if (imgColor.y > 0.25)
            {
                imgColor.y = 0.2745098173618316650390625;
            }
        }
        if (imgColor.z < 0.5)
        {
            if (imgColor.z > 0.25)
            {
                imgColor.z = 0.2745098173618316650390625;
            }
        }
        if (imgColor.x < 0.75)
        {
            if (imgColor.x > 0.5)
            {
                imgColor.x = 0.549019634723663330078125;
            }
        }
        if (imgColor.y < 0.75)
        {
            if (imgColor.y > 0.5)
            {
                imgColor.y = 0.549019634723663330078125;
            }
        }
        if (imgColor.z < 0.75)
        {
            if (imgColor.z > 0.5)
            {
                imgColor.z = 0.549019634723663330078125;
            }
        }
        if (imgColor.x > 0.75)
        {
            if (imgColor.x > 0.75)
            {
                imgColor.x = 0.823529422283172607421875;
            }
        }
        if (imgColor.y > 0.75)
        {
            if (imgColor.y > 0.75)
            {
                imgColor.y = 0.823529422283172607421875;
            }
        }
        if (imgColor.z > 0.75)
        {
            if (imgColor.z > 0.75)
            {
                imgColor.z = 0.823529422283172607421875;
            }
        }
    }
    if (LIBRA_UNIFORM_GPGXmscf == 4.0)
    {
        if (imgColor.x < 0.5)
        {
            if (imgColor.x > 0.25)
            {
                imgColor.x = 0.21568627655506134033203125;
            }
        }
        if (imgColor.y < 0.5)
        {
            if (imgColor.y > 0.25)
            {
                imgColor.y = 0.21568627655506134033203125;
            }
        }
        if (imgColor.z < 0.5)
        {
            if (imgColor.z > 0.25)
            {
                imgColor.z = 0.21568627655506134033203125;
            }
        }
        if (imgColor.x < 0.75)
        {
            if (imgColor.x > 0.5)
            {
                imgColor.x = 0.4313725531101226806640625;
            }
        }
        if (imgColor.y < 0.75)
        {
            if (imgColor.y > 0.5)
            {
                imgColor.y = 0.4313725531101226806640625;
            }
        }
        if (imgColor.z < 0.75)
        {
            if (imgColor.z > 0.5)
            {
                imgColor.z = 0.4313725531101226806640625;
            }
        }
        if (imgColor.x > 0.75)
        {
            if (imgColor.x > 0.75)
            {
                imgColor.x = 0.64705884456634521484375;
            }
        }
        if (imgColor.y > 0.75)
        {
            if (imgColor.y > 0.75)
            {
                imgColor.y = 0.64705884456634521484375;
            }
        }
        if (imgColor.z > 0.75)
        {
            if (imgColor.z > 0.75)
            {
                imgColor.z = 0.64705884456634521484375;
            }
        }
    }
    vec4 _694 = imgColor;
    vec3 _697 = min(_694.xyz, vec3(1.0));
    imgColor.x = _697.x;
    imgColor.y = _697.y;
    imgColor.z = _697.z;
    if (LIBRA_UNIFORM_sms_blue == 1.0)
    {
        if (LIBRA_UNIFORM_GPGXmscf == 0.0)
        {
            if (imgColor.z < 0.5)
            {
                if (imgColor.z > 0.25)
                {
                    imgColor.z = 0.4092499911785125732421875;
                }
            }
        }
        else
        {
            if (LIBRA_UNIFORM_GPGXmscf == 1.0)
            {
                if (imgColor.z < 0.5)
                {
                    if (imgColor.z > 0.25)
                    {
                        imgColor.z = 0.4092499911785125732421875;
                    }
                }
            }
            else
            {
                if (LIBRA_UNIFORM_GPGXmscf == 2.0)
                {
                    if (imgColor.z < 0.5)
                    {
                        if (imgColor.z > 0.25)
                        {
                            imgColor.z = 0.385176479816436767578125;
                        }
                    }
                }
                else
                {
                    if (LIBRA_UNIFORM_GPGXmscf == 3.0)
                    {
                        if (imgColor.z < 0.5)
                        {
                            if (imgColor.z > 0.25)
                            {
                                imgColor.z = 0.337029397487640380859375;
                            }
                        }
                    }
                    else
                    {
                        if (LIBRA_UNIFORM_GPGXmscf == 4.0)
                        {
                            if (imgColor.z < 0.4000000059604644775390625)
                            {
                                if (imgColor.z > 0.20000000298023223876953125)
                                {
                                    imgColor.z = 0.264808833599090576171875;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    vec4 _800 = imgColor;
    vec3 _803 = min(_800.xyz, vec3(1.0));
    imgColor.x = _803.x;
    imgColor.y = _803.y;
    imgColor.z = _803.z;
    if (LIBRA_UNIFORM_sega_fix == 1.0)
    {
        vec4 _816 = imgColor;
        vec3 _819 = _816.xyz * 1.066945552825927734375;
        imgColor.x = _819.x;
        imgColor.y = _819.y;
        imgColor.z = _819.z;
    }
    if (LIBRA_UNIFORM_sega_fix == 2.0)
    {
        vec4 _831 = imgColor;
        vec3 _834 = _831.xyz * 1.1135370731353759765625;
        imgColor.x = _834.x;
        imgColor.y = _834.y;
        imgColor.z = _834.z;
    }
    if (LIBRA_UNIFORM_sega_fix == 3.0)
    {
        vec4 _846 = imgColor;
        vec3 _849 = _846.xyz * 1.16438353061676025390625;
        imgColor.x = _849.x;
        imgColor.y = _849.y;
        imgColor.z = _849.z;
    }
    vec4 _856 = imgColor;
    vec3 _859 = min(_856.xyz, vec3(1.0));
    imgColor.x = _859.x;
    imgColor.y = _859.y;
    imgColor.z = _859.z;
    if (LIBRA_UNIFORM_MD_Pal > 0.5)
    {
        float param = imgColor.x;
        float param_1 = 2.5784199237823486328125;
        float param_2 = 0.5206739902496337890625;
        float _879 = contrast_sigmoid_inv(param, param_1, param_2);
        imgColor.x = _879;
        float param_3 = imgColor.y;
        float param_4 = 2.5784199237823486328125;
        float param_5 = 0.5206739902496337890625;
        float _886 = contrast_sigmoid_inv(param_3, param_4, param_5);
        imgColor.y = _886;
        float param_6 = imgColor.z;
        float param_7 = 2.5784199237823486328125;
        float param_8 = 0.5206739902496337890625;
        float _893 = contrast_sigmoid_inv(param_6, param_7, param_8);
        imgColor.z = _893;
    }
    vec4 _895 = imgColor;
    vec3 _898 = min(_895.xyz, vec3(1.0));
    imgColor.x = _898.x;
    imgColor.y = _898.y;
    imgColor.z = _898.z;
    vec3 color = imgColor.xyz;
    if (int(LIBRA_UNIFORM_TNTC) == 0)
    {
        color = imgColor.xyz;
    }
    else
    {
        float lutlow = 0.01960784383118152618408203125;
        float invLS = 1.0 / LIBRA_UNIFORM_LS;
        vec3 lut_ref = imgColor.xyz + ((vec3(1.0) - pow(imgColor.xyz, vec3(0.333000004291534423828125))) * lutlow);
        float lutb = lut_ref.z * (1.0 - (0.5 * invLS));
        vec3 _946 = lut_ref;
        vec2 _954 = (_946.xy * (1.0 - invLS)) + vec2(0.5 * invLS);
        lut_ref.x = _954.x;
        lut_ref.y = _954.y;
        float tile1 = ceil(lutb * (LIBRA_UNIFORM_LS - 1.0));
        float tile0 = max(tile1 - 1.0, 0.0);
        float f = fract(lutb * (LIBRA_UNIFORM_LS - 1.0));
        if (f == 0.0)
        {
            f = 1.0;
        }
        vec2 coord0 = vec2(tile0 + lut_ref.x, lut_ref.y) * vec2(invLS, 1.0);
        vec2 coord1 = vec2(tile1 + lut_ref.x, lut_ref.y) * vec2(invLS, 1.0);
        vec4 color1;
        vec4 color2;
        vec4 res;
        if (int(LIBRA_UNIFORM_TNTC) == 1)
        {
            color1 = texture(LIBRA_TEXTURE_SamplerLUT1, coord0);
            color2 = texture(LIBRA_TEXTURE_SamplerLUT1, coord1);
            res = mix(color1, color2, vec4(f));
        }
        else
        {
            if (int(LIBRA_UNIFORM_TNTC) == 2)
            {
                color1 = texture(LIBRA_TEXTURE_SamplerLUT2, coord0);
                color2 = texture(LIBRA_TEXTURE_SamplerLUT2, coord1);
                res = mix(color1, color2, vec4(f));
            }
            else
            {
                if (int(LIBRA_UNIFORM_TNTC) == 3)
                {
                    color1 = texture(LIBRA_TEXTURE_SamplerLUT3, coord0);
                    color2 = texture(LIBRA_TEXTURE_SamplerLUT3, coord1);
                    res = mix(color1, color2, vec4(f));
                }
                else
                {
                    if (int(LIBRA_UNIFORM_TNTC) == 4)
                    {
                        color1 = texture(LIBRA_TEXTURE_SamplerLUT4, coord0);
                        color2 = texture(LIBRA_TEXTURE_SamplerLUT4, coord1);
                        res = mix(color1, color2, vec4(f));
                    }
                }
            }
        }
        vec3 param_9 = res.xyz;
        vec3 param_10 = imgColor.xyz;
        vec3 _1089 = fix_lut(param_9, param_10);
        res.x = _1089.x;
        res.y = _1089.y;
        res.z = _1089.z;
        color = mix(imgColor.xyz, res.xyz, vec3(min(LIBRA_UNIFORM_TNTC, 1.0)));
    }
    vec3 c = clamp(color, vec3(0.0), vec3(1.0));
    float p;
    mat3 m_out;
    if (LIBRA_UNIFORM_CS == 0.0)
    {
        p = 2.2000000476837158203125;
        m_out = mat3(vec3(3.2409698963165283203125, -0.9692440032958984375, 0.0556299984455108642578125), vec3(-1.53738296031951904296875, 1.87596797943115234375, -0.20397700369358062744140625), vec3(-0.4986110031604766845703125, 0.0415549986064434051513671875, 1.056972026824951171875));
    }
    else
    {
        if (LIBRA_UNIFORM_CS == 1.0)
        {
            p = 2.2000000476837158203125;
            m_out = mat3(vec3(2.7917230129241943359375, -0.89476597309112548828125, 0.0416780002415180206298828125), vec3(-1.17316496372222900390625, 1.81558597087860107421875, -0.13088600337505340576171875), vec3(-0.4409730136394500732421875, 0.0320000015199184417724609375, 1.0020339488983154296875));
        }
        else
        {
            if (LIBRA_UNIFORM_CS == 2.0)
            {
                p = 2.599999904632568359375;
                m_out = mat3(vec3(2.97342205047607421875, -0.8676049709320068359375, 0.0450309999287128448486328125), vec3(-1.11043298244476318359375, 1.84375703334808349609375, -0.09569700062274932861328125), vec3(-0.4802469909191131591796875, 0.02474300004541873931884765625, 1.2012150287628173828125));
            }
            else
            {
                if (LIBRA_UNIFORM_CS == 3.0)
                {
                    p = 2.2000000476837158203125;
                    m_out = mat3(vec3(2.0415880680084228515625, -0.9692440032958984375, 0.013443999923765659332275390625), vec3(-0.5650069713592529296875, 1.87596797943115234375, -0.11835999786853790283203125), vec3(-0.3447310030460357666015625, 0.0415549986064434051513671875, 1.01517498493194580078125));
                }
                else
                {
                    if (LIBRA_UNIFORM_CS == 4.0)
                    {
                        p = 2.400000095367431640625;
                        m_out = mat3(vec3(1.71665096282958984375, -0.666683971881866455078125, 0.01764000020921230316162109375), vec3(-0.355670988559722900390625, 1.61648094654083251953125, -0.0427710004150867462158203125), vec3(-0.253365993499755859375, 0.01576899923384189605712890625, 0.94210302829742431640625));
                    }
                    else
                    {
                        if (LIBRA_UNIFORM_CS == 5.0)
                        {
                            p = 2.2000000476837158203125;
                            m_out = mat3(vec3(2.4935090541839599609375, -0.829473018646240234375, 0.0358511991798877716064453125), vec3(-0.93138802051544189453125, 1.762629985809326171875, -0.07618390023708343505859375), vec3(-0.40271198749542236328125, 0.0236239992082118988037109375, 0.9570295810699462890625));
                        }
                    }
                }
            }
        }
    }
    color = pow(c, vec3(p));
    mat3 m_in = mat3(vec3(0.412391006946563720703125, 0.2126390039920806884765625, 0.019331000745296478271484375), vec3(0.3575839996337890625, 0.715169012546539306640625, 0.11919499933719635009765625), vec3(0.18048100173473358154296875, 0.07219199836254119873046875, 0.950532019138336181640625));
    if (LIBRA_UNIFORM_CP == 0.0)
    {
        m_in = mat3(vec3(0.412391006946563720703125, 0.2126390039920806884765625, 0.019331000745296478271484375), vec3(0.3575839996337890625, 0.715169012546539306640625, 0.11919499933719635009765625), vec3(0.18048100173473358154296875, 0.07219199836254119873046875, 0.950532019138336181640625));
    }
    else
    {
        if (LIBRA_UNIFORM_CP == 1.0)
        {
            m_in = mat3(vec3(0.4305540025234222412109375, 0.222003996372222900390625, 0.02018200047314167022705078125), vec3(0.34154999256134033203125, 0.706655025482177734375, 0.12955300509929656982421875), vec3(0.17835199832916259765625, 0.071341000497341156005859375, 0.939321994781494140625));
        }
        else
        {
            if (LIBRA_UNIFORM_CP == 2.0)
            {
                m_in = mat3(vec3(0.3966859877109527587890625, 0.21029900014400482177734375, 0.006130999885499477386474609375), vec3(0.3725039958953857421875, 0.71376597881317138671875, 0.1153559982776641845703125), vec3(0.18126599490642547607421875, 0.075935997068881988525390625, 0.9675710201263427734375));
            }
            else
            {
                if (LIBRA_UNIFORM_CP == 3.0)
                {
                    m_in = mat3(vec3(0.393521010875701904296875, 0.21237599849700927734375, 0.01873899996280670166015625), vec3(0.3652580082416534423828125, 0.701059997081756591796875, 0.111933998763561248779296875), vec3(0.1916770040988922119140625, 0.086563996970653533935546875, 0.958384990692138671875));
                }
                else
                {
                    if (LIBRA_UNIFORM_CP == 4.0)
                    {
                        m_in = mat3(vec3(0.392257988452911376953125, 0.20940999686717987060546875, 0.016061000525951385498046875), vec3(0.3511349856853485107421875, 0.72567999362945556640625, 0.093635998666286468505859375), vec3(0.1666029989719390869140625, 0.064910002052783966064453125, 0.850323975086212158203125));
                    }
                    else
                    {
                        if (LIBRA_UNIFORM_CP == 5.0)
                        {
                            m_in = mat3(vec3(0.37792301177978515625, 0.19567899405956268310546875, 0.01051400043070316314697265625), vec3(0.31736600399017333984375, 0.72231900691986083984375, 0.097825996577739715576171875), vec3(0.20773799717426300048828125, 0.08200199902057647705078125, 1.07695996761322021484375));
                        }
                    }
                }
            }
        }
    }
    color = m_in * color;
    color = m_out * color;
    color = clamp(color, vec3(0.0), vec3(1.0));
    color = pow(color, vec3(1.0 / p));
    if (LIBRA_UNIFORM_CP == (-1.0))
    {
        color = c;
    }
    vec3 param_11 = pow(color, vec3(LIBRA_UNIFORM_wp_saturation));
    float param_12 = max(max(color.x, color.y), color.z);
    vec3 scolor1 = plant(param_11, param_12);
    float luma = dot(color, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625));
    vec3 scolor2 = mix(vec3(luma), color, vec3(LIBRA_UNIFORM_wp_saturation));
    bvec3 _1409 = bvec3(LIBRA_UNIFORM_wp_saturation > 1.0);
    color = vec3(_1409.x ? scolor1.x : scolor2.x, _1409.y ? scolor1.y : scolor2.y, _1409.z ? scolor1.z : scolor2.z);
    float param_13 = max(max(color.x, color.y), color.z);
    vec3 param_14 = color;
    float param_15 = contrast(param_13);
    color = plant(param_14, param_15);
    p = 2.2000000476837158203125;
    color = clamp(color, vec3(0.0), vec3(1.0));
    color = pow(color, vec3(p));
    vec3 warmer = mat3(vec3(0.485033929347991943359375, 0.2500956058502197265625, 0.0227359645068645477294921875), vec3(0.348895728588104248046875, 0.69779145717620849609375, 0.11629857122898101806640625), vec3(0.13028235733509063720703125, 0.052112944424152374267578125, 0.68615376949310302734375)) * color;
    warmer = mat3(vec3(3.2409698963165283203125, -0.9692440032958984375, 0.0556299984455108642578125), vec3(-1.53738296031951904296875, 1.87596797943115234375, -0.20397700369358062744140625), vec3(-0.4986110031604766845703125, 0.0415549986064434051513671875, 1.056972026824951171875)) * warmer;
    vec3 cooler = mat3(vec3(0.341275393962860107421875, 0.175970137119293212890625, 0.01599728502333164215087890625), vec3(0.364617049694061279296875, 0.72923409938812255859375, 0.121539019048213958740234375), vec3(0.2369894087314605712890625, 0.094795763492584228515625, 1.24814426898956298828125)) * color;
    cooler = mat3(vec3(3.2409698963165283203125, -0.9692440032958984375, 0.0556299984455108642578125), vec3(-1.53738296031951904296875, 1.87596797943115234375, -0.20397700369358062744140625), vec3(-0.4986110031604766845703125, 0.0415549986064434051513671875, 1.056972026824951171875)) * cooler;
    float m = abs(LIBRA_UNIFORM_WP) / 100.0;
    bvec3 _1482 = bvec3(LIBRA_UNIFORM_WP < 0.0);
    vec3 comp = vec3(_1482.x ? cooler.x : warmer.x, _1482.y ? cooler.y : warmer.y, _1482.z ? cooler.z : warmer.z);
    color = mix(color, comp, vec3(m));
    color = pow(max(color, vec3(0.0)), vec3(1.0 / p));
    vec3 param_16 = color;
    color = pgc(param_16);
    if ((LIBRA_UNIFORM_BP + IRED) > (-0.5))
    {
        color = (color + aftglow.xyz) + vec3(bp);
    }
    else
    {
        color = (max(color + vec3((LIBRA_UNIFORM_BP + IRED) / 255.0), vec3(0.0)) / vec3(1.0 + (((LIBRA_UNIFORM_BP + IRED) / 255.0) * step((-(LIBRA_UNIFORM_BP + IRED)) / 255.0, max(max(color.x, color.y), color.z))))) + aftglow.xyz;
    }
    color = min(color * LIBRA_UNIFORM_pre_bb, vec3(1.0));
    vec2 param_17 = LIBRA_VARYING_0;
    float _1564 = vignette(param_17);
    FragColor = vec4(color, _1564);
}
