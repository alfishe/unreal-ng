#version 330

const mat3 _460[5] = mat3[](mat3(vec3(0.4584319889545440673828125, 0.3095490038394927978515625, 0.18247400224208831787109375), vec3(0.2567220032215118408203125, 0.668847978115081787109375, 0.074429996311664581298828125), vec3(0.0183370001614093780517578125, 0.12713600695133209228515625, 0.943584024906158447265625)), mat3(vec3(0.4608440101146697998046875, 0.3076129853725433349609375, 0.18190999329090118408203125), vec3(0.2443110048770904541015625, 0.676311016082763671875, 0.079378001391887664794921875), vec3(0.007122999988496303558349609375, 0.106900997459888458251953125, 0.9750339984893798828125)), mat3(vec3(0.4041509926319122314453125, 0.3548870086669921875, 0.19141800701618194580078125), vec3(0.20198400318622589111328125, 0.71452999114990234375, 0.083484999835491180419921875), vec3(0.0006070000235922634601593017578125, 0.0629599988460540771484375, 1.0254909992218017578125)), mat3(vec3(0.3317179977893829345703125, 0.4294759929180145263671875, 0.18926100432872772216796875), vec3(0.173633992671966552734375, 0.7380440235137939453125, 0.08832199871540069580078125), vec3(0.012958000414073467254638671875, 0.09194099903106689453125, 0.984158992767333984375)), mat3(vec3(0.588010013103485107421875, 0.179132997989654541015625, 0.1832239925861358642578125), vec3(0.289660990238189697265625, 0.605639994144439697265625, 0.10469900071620941162109375), vec3(0.0, 0.06824100017547607421875, 1.0208170413970947265625)));
const mat3 _508[4] = mat3[](mat3(vec3(0.412391006946563720703125, 0.3575839996337890625, 0.18048100173473358154296875), vec3(0.2126390039920806884765625, 0.715169012546539306640625, 0.07219199836254119873046875), vec3(0.019331000745296478271484375, 0.11919499933719635009765625, 0.950532019138336181640625)), mat3(vec3(0.4305540025234222412109375, 0.34154999256134033203125, 0.17835199832916259765625), vec3(0.222003996372222900390625, 0.706655025482177734375, 0.071341000497341156005859375), vec3(0.02018200047314167022705078125, 0.12955300509929656982421875, 0.939321994781494140625)), mat3(vec3(0.393521010875701904296875, 0.3652580082416534423828125, 0.1916770040988922119140625), vec3(0.21237599849700927734375, 0.701059997081756591796875, 0.086563996970653533935546875), vec3(0.01873899996280670166015625, 0.111933998763561248779296875, 0.958384990692138671875)), mat3(vec3(0.393521010875701904296875, 0.3652580082416534423828125, 0.1916770040988922119140625), vec3(0.21237599849700927734375, 0.701059997081756591796875, 0.086563996970653533935546875), vec3(0.01873899996280670166015625, 0.111933998763561248779296875, 0.958384990692138671875)));

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform uint LIBRA_UNIFORM_HDRMode;

uniform float LIBRA_UNIFORM_hcrt_colour_space;
uniform float LIBRA_UNIFORM_hcrt_lcd_resolution;
uniform float LIBRA_UNIFORM_hcrt_lcd_subpixel;
uniform float LIBRA_UNIFORM_hcrt_colour_system;
uniform float LIBRA_UNIFORM_hcrt_phosphor_set;
uniform float LIBRA_UNIFORM_hcrt_white_temperature_D65;
uniform float LIBRA_UNIFORM_hcrt_white_temperature_D93;
uniform float LIBRA_UNIFORM_hcrt_brightness;
uniform float LIBRA_UNIFORM_hcrt_contrast;
uniform float LIBRA_UNIFORM_hcrt_saturation;
uniform float LIBRA_UNIFORM_hcrt_gamma_in;
uniform float LIBRA_UNIFORM_hcrt_gamma_cutoff;
uniform float LIBRA_UNIFORM_hcrt_colour_boost_factor;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 WhiteBalance(float temperature, vec3 colour)
{
    mat3 _78;
    if (temperature < 6504.0)
    {
        _78 = mat3(vec3(0.0, -2902.195556640625, -8257.7998046875), vec3(0.0, 1669.580322265625, 2575.28271484375), vec3(1.0, 1.330267429351806640625, 1.899375438690185546875));
    }
    else
    {
        _78 = mat3(vec3(1745.04248046875, 1216.6168212890625, -8257.7998046875), vec3(-2666.347412109375, -2173.101318359375, 2575.28271484375), vec3(0.559953868389129638671875, 0.70381200313568115234375, 1.899375438690185546875));
    }
    mat3 m = _78;
    vec3 rgb_temperature = mix(clamp((m[0] / (vec3(clamp(temperature, 1000.0, 40000.0)) + m[1])) + m[2], vec3(0.0), vec3(1.0)), vec3(1.0), vec3(smoothstep(1000.0, 0.0, temperature)));
    vec3 result = colour * rgb_temperature;
    result *= (dot(colour, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)) / max(dot(result, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875)), 9.9999997473787516355514526367188e-06));
    return result;
}

vec3 XYZtoYxy(vec3 XYZ)
{
    float XYZrgb = (XYZ.x + XYZ.y) + XYZ.z;
    float _162;
    if (XYZrgb <= 0.0)
    {
        _162 = 0.3804999887943267822265625;
    }
    else
    {
        _162 = XYZ.x / XYZrgb;
    }
    float Yxyg = _162;
    float _174;
    if (XYZrgb <= 0.0)
    {
        _174 = 0.3768999874591827392578125;
    }
    else
    {
        _174 = XYZ.y / XYZrgb;
    }
    float Yxyb = _174;
    return vec3(XYZ.y, Yxyg, Yxyb);
}

float Bezier(float t0, vec4 control_points)
{
    vec4 t = vec4(1.0, t0, t0 * t0, (t0 * t0) * t0);
    return dot(t, control_points * mat4(vec4(1.0, 0.0, 0.0, 0.0), vec4(-3.0, 3.0, 0.0, 0.0), vec4(3.0, -6.0, 3.0, 0.0), vec4(-1.0, 3.0, -3.0, 1.0)));
}

float Brightness(float luminance)
{
    if (LIBRA_UNIFORM_hcrt_brightness >= 0.0)
    {
        return Bezier(luminance, mix(vec4(0.0, 0.3333333432674407958984375, 0.666666686534881591796875, 1.0), vec4(0.0, 1.0, 1.0, 1.0), vec4(LIBRA_UNIFORM_hcrt_brightness)));
    }
    else
    {
        return Bezier(luminance, mix(vec4(0.0, 0.3333333432674407958984375, 0.666666686534881591796875, 1.0), vec4(0.0, 0.0, 0.0, 1.0), vec4(abs(LIBRA_UNIFORM_hcrt_brightness))));
    }
}

float Contrast(float luminance)
{
    if (LIBRA_UNIFORM_hcrt_contrast >= 0.0)
    {
        return Bezier(luminance, mix(vec4(0.0, 0.3333333432674407958984375, 0.666666686534881591796875, 1.0), vec4(0.0, 0.0, 1.0, 1.0), vec4(LIBRA_UNIFORM_hcrt_contrast)));
    }
    else
    {
        return Bezier(luminance, mix(vec4(0.0, 0.3333333432674407958984375, 0.666666686534881591796875, 1.0), vec4(0.0, 1.0, 0.0, 1.0), vec4(abs(LIBRA_UNIFORM_hcrt_contrast))));
    }
}

vec3 YxytoXYZ(vec3 Yxy)
{
    float Xs = Yxy.x * (Yxy.y / Yxy.z);
    float Xsz = (Yxy.x <= 0.0) ? 0.0 : 1.0;
    vec3 XYZ = vec3(Xsz) * vec3(Xs, Yxy.x, ((Xs / Yxy.y) - Xs) - Yxy.x);
    return XYZ;
}

vec3 Saturation(vec3 colour)
{
    float luma = dot(colour, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
    float saturation = 0.5 + (LIBRA_UNIFORM_hcrt_saturation * 0.5);
    return mix(vec3(luma), colour, vec3(saturation) * 2.0);
}

vec3 BrightnessContrastSaturation(vec3 xyz)
{
    vec3 Yxy = XYZtoYxy(xyz);
    float Y_gamma = clamp(pow(Yxy.x, 0.4166666567325592041015625), 0.0, 1.0);
    float Y_brightness = Brightness(Y_gamma);
    float Y_contrast = Contrast(Y_brightness);
    vec3 contrast_linear = vec3(pow(Y_contrast, 2.400000095367431640625), Yxy.y, Yxy.z);
    vec3 xyz_graded = YxytoXYZ(contrast_linear);
    vec3 rec709_linear = xyz_graded * mat3(vec3(3.2409698963165283203125, -1.53738296031951904296875, -0.4986110031604766845703125), vec3(-0.9692440032958984375, 1.87596797943115234375, 0.0415549986064434051513671875), vec3(0.0556299984455108642578125, -0.20397700369358062744140625, 1.056972026824951171875));
    vec3 saturated = Saturation(rec709_linear);
    return saturated;
}

vec3 GamutBoost(vec3 colour_709)
{
    float luma = dot(colour_709, vec3(0.2125999927520751953125, 0.715200006961822509765625, 0.072200000286102294921875));
    return mix(vec3(luma), colour_709, vec3(LIBRA_UNIFORM_hcrt_colour_boost_factor));
}

vec3 ColourGrade(vec3 colour)
{
    uint colour_system = uint(LIBRA_UNIFORM_hcrt_colour_system);
    uint phosphor_set = uint(LIBRA_UNIFORM_hcrt_phosphor_set);
    float temperature[4] = float[](LIBRA_UNIFORM_hcrt_white_temperature_D65, LIBRA_UNIFORM_hcrt_white_temperature_D65, LIBRA_UNIFORM_hcrt_white_temperature_D65, LIBRA_UNIFORM_hcrt_white_temperature_D93);
    float param = temperature[colour_system];
    vec3 param_1 = colour;
    vec3 white_point = WhiteBalance(param, param_1);
    vec3 linear = pow(white_point, vec3(LIBRA_UNIFORM_hcrt_gamma_in));
    mat3 _391;
    if (phosphor_set > 0u)
    {
        _391 = _460[phosphor_set - 1u];
    }
    else
    {
        _391 = _508[colour_system];
    }
    mat3 source_to_XYZ = _391;
    vec3 xyz = linear * source_to_XYZ;
    vec3 graded_colour = BrightnessContrastSaturation(xyz);
    vec3 param_2 = graded_colour;
    vec3 boosted_colour = GamutBoost(param_2);
    return max(boosted_colour, vec3(0.0));
}

void main()
{
    vec3 source = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 colour = ColourGrade(source);
    FragColor = vec4(colour, 1.0);
}
