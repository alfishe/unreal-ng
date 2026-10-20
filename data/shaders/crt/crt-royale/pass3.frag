#version 330

uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
in vec2 LIBRA_VARYING_1;
layout(location = 0) out vec4 FragColor;
float ntsc_gamma;
float pal_gamma;
float crt_reference_gamma_high;
float crt_reference_gamma_low;
float lcd_reference_gamma;
float crt_office_gamma;
float lcd_office_gamma;
bool assume_opaque_alpha;
bool linearize_input;
bool gamma_encode_output;
bool gamma_aware_bilinear;
float blur3_std_dev;
float blur4_std_dev;
float blur5_std_dev;
float blur6_std_dev;
float blur7_std_dev;
float blur8_std_dev;
float blur9_std_dev;
float blur10_std_dev;
float blur11_std_dev;
float blur12_std_dev;
float blur17_std_dev;
float blur25_std_dev;
float blur31_std_dev;
float blur43_std_dev;
float error_blurring;

float get_pass_input_gamma()
{
    return 1.0;
}

vec4 decode_input(vec4 color)
{
    if (linearize_input)
    {
        if (assume_opaque_alpha)
        {
            return vec4(pow(color.xyz, vec3(get_pass_input_gamma())), 1.0);
        }
        else
        {
            return vec4(pow(color.xyz, vec3(get_pass_input_gamma())), color.w);
        }
    }
    else
    {
        return color;
    }
}

vec4 tex2D_linearize(sampler2D tex, vec2 tex_coords)
{
    vec4 param = texture(tex, tex_coords);
    return decode_input(param);
}

vec3 tex2Dblur9fast(sampler2D tex, vec2 tex_uv, vec2 dxdy, float sigma)
{
    float denom_inv = 0.5 / (sigma * sigma);
    float w0 = 1.0;
    float w1 = exp((-1.0) * denom_inv);
    float w2 = exp((-4.0) * denom_inv);
    float w3 = exp((-9.0) * denom_inv);
    float w4 = exp((-16.0) * denom_inv);
    float weight_sum_inv = 1.0 / (w0 + (2.0 * (((w1 + w2) + w3) + w4)));
    float w12 = w1 + w2;
    float w34 = w3 + w4;
    float w12_ratio = w2 / w12;
    float w34_ratio = w4 / w34;
    vec3 sum = vec3(0.0);
    vec2 param = tex_uv - (dxdy * (3.0 + w34_ratio));
    sum += (tex2D_linearize(tex, param).xyz * w34);
    vec2 param_1 = tex_uv - (dxdy * (1.0 + w12_ratio));
    sum += (tex2D_linearize(tex, param_1).xyz * w12);
    vec2 param_2 = tex_uv;
    sum += (tex2D_linearize(tex, param_2).xyz * w0);
    vec2 param_3 = tex_uv + (dxdy * (1.0 + w12_ratio));
    sum += (tex2D_linearize(tex, param_3).xyz * w12);
    vec2 param_4 = tex_uv + (dxdy * (3.0 + w34_ratio));
    sum += (tex2D_linearize(tex, param_4).xyz * w34);
    return sum * weight_sum_inv;
}

vec3 tex2Dblur9fast(sampler2D tex, vec2 tex_uv, vec2 dxdy)
{
    vec2 param = tex_uv;
    vec2 param_1 = dxdy;
    float param_2 = blur9_std_dev;
    return tex2Dblur9fast(tex, param, param_1, param_2);
}

float get_pass_output_gamma()
{
    return 1.0;
}

vec4 encode_output(vec4 color)
{
    if (gamma_encode_output)
    {
        if (assume_opaque_alpha)
        {
            return vec4(pow(color.xyz, vec3(1.0 / get_pass_output_gamma())), 1.0);
        }
        else
        {
            return vec4(pow(color.xyz, vec3(1.0 / get_pass_output_gamma())), color.w);
        }
    }
    else
    {
        return color;
    }
}

void main()
{
    ntsc_gamma = 2.2000000476837158203125;
    pal_gamma = 2.7999999523162841796875;
    crt_reference_gamma_high = 2.5;
    crt_reference_gamma_low = 2.349999904632568359375;
    lcd_reference_gamma = 2.5;
    crt_office_gamma = 2.2000000476837158203125;
    lcd_office_gamma = 2.2000000476837158203125;
    assume_opaque_alpha = false;
    linearize_input = false;
    gamma_encode_output = false;
    gamma_aware_bilinear = !linearize_input;
    blur3_std_dev = 0.626660168170928955078125;
    blur4_std_dev = 0.66171872615814208984375;
    blur5_std_dev = 0.984570324420928955078125;
    blur6_std_dev = 1.02626955509185791015625;
    blur7_std_dev = 1.3610351085662841796875;
    blur8_std_dev = 1.4080078601837158203125;
    blur9_std_dev = 1.75332033634185791015625;
    blur10_std_dev = 1.80478513240814208984375;
    blur11_std_dev = 2.1598632335662841796875;
    blur12_std_dev = 2.215234279632568359375;
    blur17_std_dev = 3.4553558826446533203125;
    blur25_std_dev = 5.3409576416015625;
    blur31_std_dev = 6.86488056182861328125;
    blur43_std_dev = 10.1852054595947265625;
    error_blurring = 0.5;
    vec2 param = LIBRA_VARYING_0;
    vec2 param_1 = LIBRA_VARYING_1;
    vec3 color = tex2Dblur9fast(LIBRA_TEXTURE_Source, param, param_1);
    vec4 param_2 = vec4(color, 1.0);
    FragColor = encode_output(param_2);
}
