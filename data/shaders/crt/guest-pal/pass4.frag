#version 330

uniform vec4 LIBRA_UNIFORM_SourceSize;
uniform mat4 LIBRA_UNIFORM_MVP;

uniform vec4 LIBRA_UNIFORM_OriginalSize;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_inter;
uniform float LIBRA_UNIFORM_interm;
uniform float LIBRA_UNIFORM_iscan;
uniform float LIBRA_UNIFORM_intres;
uniform float LIBRA_UNIFORM_iscans;
uniform float LIBRA_UNIFORM_hiscan;

uniform sampler2D LIBRA_TEXTURE_Source;

in vec2 LIBRA_VARYING_0;
layout(location = 0) out vec4 FragColor;

vec3 plant(vec3 tar, float r)
{
    float t = max(max(tar.x, tar.y), tar.z) + 9.9999997473787516355514526367188e-06;
    return (tar * r) / vec3(t);
}

void main()
{
    vec3 c1 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0).xyz;
    vec3 c2 = texture(LIBRA_TEXTURE_Source, LIBRA_VARYING_0 + vec2(0.0, LIBRA_UNIFORM_OriginalSize.w)).xyz;
    vec3 c = c1;
    float intera = 1.0;
    float m1 = max(max(c1.x, c1.y), c1.z);
    float m2 = max(max(c2.x, c2.y), c2.z);
    vec3 df = abs(c1 - c2);
    float d = max(max(df.x, df.y), df.z);
    if (LIBRA_UNIFORM_interm == 2.0)
    {
        d = mix(0.100000001490116119384765625 * d, 10.0 * d, step(m1 / (m2 + 9.9999997473787516355514526367188e-05), m2 / (m1 + 9.9999997473787516355514526367188e-05)));
    }
    float r = m1;
    float yres_div = 1.0;
    if (LIBRA_UNIFORM_intres > 1.25)
    {
        yres_div = LIBRA_UNIFORM_intres;
    }
    bool hscan = LIBRA_UNIFORM_hiscan > 0.5;
    bool _155 = LIBRA_UNIFORM_inter <= (LIBRA_UNIFORM_OriginalSize.y / yres_div);
    bool _161;
    if (_155)
    {
        _161 = LIBRA_UNIFORM_interm > 0.5;
    }
    else
    {
        _161 = _155;
    }
    bool _167;
    if (_161)
    {
        _167 = LIBRA_UNIFORM_intres != 1.0;
    }
    else
    {
        _167 = _161;
    }
    bool _173;
    if (_167)
    {
        _173 = LIBRA_UNIFORM_intres != 0.5;
    }
    else
    {
        _173 = _167;
    }
    if (_173 || hscan)
    {
        intera = 0.25;
        float line_no = floor(mod(LIBRA_UNIFORM_OriginalSize.y * LIBRA_VARYING_0.y, 2.0));
        float frame_no = floor(mod(float(LIBRA_UNIFORM_FrameCount), 2.0));
        float ii = abs(line_no - frame_no);
        bool _204 = LIBRA_UNIFORM_interm < 3.5;
        bool _212;
        if (!_204)
        {
            _212 = LIBRA_UNIFORM_interm > 5.5;
        }
        else
        {
            _212 = _204;
        }
        if (_212)
        {
            if (LIBRA_UNIFORM_interm == 6.0)
            {
                c = mix(c2, c1, vec3(ii));
            }
            else
            {
                vec3 param = mix(c2, c2 * c2, vec3(LIBRA_UNIFORM_iscans));
                float param_1 = max(max(c2.x, c2.y), c2.z);
                c2 = plant(param, param_1);
                r = max(m1 * ii, (1.0 - LIBRA_UNIFORM_iscan) * min(m1, m2));
                vec3 param_2 = mix(mix(c1, c2, vec3(min(mix(m1, 1.0 - m2, min(m1, 1.0 - m1)) / (d + 9.9999997473787516355514526367188e-06), 1.0))), c1, vec3(ii));
                float param_3 = r;
                c = plant(param_2, param_3);
                if (LIBRA_UNIFORM_interm == 3.0)
                {
                    c = mix(c2, c1, vec3(ii)) * (1.0 - (0.5 * LIBRA_UNIFORM_iscan));
                }
            }
        }
        if (LIBRA_UNIFORM_interm == 4.0)
        {
            vec3 param_4 = mix(c, c * c, vec3(0.5 * LIBRA_UNIFORM_iscans));
            float param_5 = max(max(c.x, c.y), c.z);
            c = plant(param_4, param_5) * (1.0 - (0.5 * LIBRA_UNIFORM_iscan));
        }
        if (LIBRA_UNIFORM_interm == 5.0)
        {
            c = mix(c2, c1, vec3(0.5));
            vec3 param_6 = mix(c, c * c, vec3(0.5 * LIBRA_UNIFORM_iscans));
            float param_7 = max(max(c.x, c.y), c.z);
            c = plant(param_6, param_7) * (1.0 - (0.5 * LIBRA_UNIFORM_iscan));
        }
        if (hscan)
        {
            c = c1;
        }
    }
    FragColor = vec4(c, intera);
}
