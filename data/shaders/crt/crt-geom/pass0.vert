#version 330

uniform mat4 LIBRA_UNIFORM_MVP;
uniform vec4 LIBRA_UNIFORM_OutputSize;
uniform vec4 LIBRA_UNIFORM_SourceSize;

uniform uint LIBRA_UNIFORM_FrameCount;
uniform float LIBRA_UNIFORM_CRTgamma;
uniform float LIBRA_UNIFORM_monitorgamma;
uniform float LIBRA_UNIFORM_d;
uniform float LIBRA_UNIFORM_R;
uniform float LIBRA_UNIFORM_cornersize;
uniform float LIBRA_UNIFORM_cornersmooth;
uniform float LIBRA_UNIFORM_x_tilt;
uniform float LIBRA_UNIFORM_y_tilt;
uniform float LIBRA_UNIFORM_overscan_x;
uniform float LIBRA_UNIFORM_overscan_y;
uniform float LIBRA_UNIFORM_mask_type;
uniform float LIBRA_UNIFORM_DOTMASK;
uniform float LIBRA_UNIFORM_DOTMASK_brightboost;
uniform float LIBRA_UNIFORM_SHARPER;
uniform float LIBRA_UNIFORM_scanline_weight;
uniform float LIBRA_UNIFORM_CURVATURE;
uniform float LIBRA_UNIFORM_interlace_detect;
uniform float LIBRA_UNIFORM_lum;
uniform float LIBRA_UNIFORM_invert_aspect;
uniform float LIBRA_UNIFORM_vertical_scanlines;
uniform float LIBRA_UNIFORM_xsize;
uniform float LIBRA_UNIFORM_ysize;

out vec2 LIBRA_VARYING_1;
out vec2 LIBRA_VARYING_2;
layout(location = 0) in vec4 Position;
out vec2 LIBRA_VARYING_0;
layout(location = 1) in vec2 TexCoord;
out vec3 LIBRA_VARYING_3;
out vec2 LIBRA_VARYING_7;
out vec2 LIBRA_VARYING_4;
out vec2 LIBRA_VARYING_5;
vec2 height;
vec2 width;
vec4 SourceSize;
vec2 aspect;
vec2 overscan;
float mod_factor;

float intersect(vec2 xy)
{
    float A = dot(xy, xy) + (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d);
    float B = 2.0 * ((LIBRA_UNIFORM_R * (dot(xy, LIBRA_VARYING_1) - ((LIBRA_UNIFORM_d * LIBRA_VARYING_2.x) * LIBRA_VARYING_2.y))) - (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d));
    float C = (LIBRA_UNIFORM_d * LIBRA_UNIFORM_d) + ((((2.0 * LIBRA_UNIFORM_R) * LIBRA_UNIFORM_d) * LIBRA_VARYING_2.x) * LIBRA_VARYING_2.y);
    return ((-B) - sqrt((B * B) - ((4.0 * A) * C))) / (2.0 * A);
}

vec2 bkwtrans(vec2 xy)
{
    vec2 param = xy;
    float c = intersect(param);
    vec2 point = ((vec2(c) * xy) - (vec2(-LIBRA_UNIFORM_R, -LIBRA_UNIFORM_R) * LIBRA_VARYING_1)) / vec2(LIBRA_UNIFORM_R, LIBRA_UNIFORM_R);
    vec2 poc = point / LIBRA_VARYING_2;
    vec2 tang = LIBRA_VARYING_1 / LIBRA_VARYING_2;
    float A = dot(tang, tang) + 1.0;
    float B = (-2.0) * dot(poc, tang);
    float C = dot(poc, poc) - 1.0;
    float a = ((-B) + sqrt((B * B) - ((4.0 * A) * C))) / (2.0 * A);
    vec2 uv = (point - (LIBRA_VARYING_1 * a)) / LIBRA_VARYING_2;
    float r = max(abs(LIBRA_UNIFORM_R * acos(a)), 9.9999997473787516355514526367188e-06);
    return (uv * r) / vec2(sin(r / LIBRA_UNIFORM_R));
}

vec2 fwtrans(inout vec2 uv)
{
    float r = max(abs(sqrt(dot(uv, uv))), 9.9999997473787516355514526367188e-06);
    uv *= (sin(r / LIBRA_UNIFORM_R) / r);
    float x = 1.0 - cos(r / LIBRA_UNIFORM_R);
    float D = ((LIBRA_UNIFORM_d / LIBRA_UNIFORM_R) + ((x * LIBRA_VARYING_2.x) * LIBRA_VARYING_2.y)) + dot(uv, LIBRA_VARYING_1);
    return (((uv * LIBRA_VARYING_2) - (LIBRA_VARYING_1 * x)) * LIBRA_UNIFORM_d) / vec2(D);
}

vec3 maxscale()
{
    vec2 param = (LIBRA_VARYING_1 * (-LIBRA_UNIFORM_R)) / vec2(1.0 + (((LIBRA_UNIFORM_R / LIBRA_UNIFORM_d) * LIBRA_VARYING_2.x) * LIBRA_VARYING_2.y));
    vec2 c = bkwtrans(param);
    vec2 a = vec2(0.5) * aspect;
    vec2 param_1 = vec2(-a.x, c.y);
    vec2 _371 = fwtrans(param_1);
    vec2 param_2 = vec2(c.x, -a.y);
    vec2 _380 = fwtrans(param_2);
    vec2 lo = vec2(_371.x, _380.y) / aspect;
    vec2 param_3 = vec2(a.x, c.y);
    vec2 _392 = fwtrans(param_3);
    vec2 param_4 = vec2(c.x, a.y);
    vec2 _400 = fwtrans(param_4);
    vec2 hi = vec2(_392.x, _400.y) / aspect;
    return vec3(((hi + lo) * aspect) * 0.5, max(hi.x - lo.x, hi.y - lo.y));
}

void main()
{
    vec2 _38;
    if (LIBRA_UNIFORM_ysize > 0.001000000047497451305389404296875)
    {
        _38 = vec2(LIBRA_UNIFORM_ysize, 1.0 / LIBRA_UNIFORM_ysize);
    }
    else
    {
        _38 = LIBRA_UNIFORM_SourceSize.yw;
    }
    height = _38;
    vec2 _65;
    if (LIBRA_UNIFORM_xsize > 0.001000000047497451305389404296875)
    {
        _65 = vec2(LIBRA_UNIFORM_xsize, 1.0 / LIBRA_UNIFORM_xsize);
    }
    else
    {
        _65 = LIBRA_UNIFORM_SourceSize.xz;
    }
    width = _65;
    SourceSize = vec4(width.x, height.x, width.y, height.y);
    float _100;
    if (LIBRA_UNIFORM_invert_aspect > 0.5)
    {
        _100 = 1.0;
    }
    else
    {
        _100 = 0.75;
    }
    aspect = vec2(_100);
    overscan = vec2(1.0099999904632568359375);
    gl_Position = LIBRA_UNIFORM_MVP * Position;
    LIBRA_VARYING_0 = TexCoord * vec2(1.000010013580322265625);
    LIBRA_VARYING_1 = sin(vec2(LIBRA_UNIFORM_x_tilt, LIBRA_UNIFORM_y_tilt));
    LIBRA_VARYING_2 = cos(vec2(LIBRA_UNIFORM_x_tilt, LIBRA_UNIFORM_y_tilt));
    LIBRA_VARYING_3 = maxscale();
    if (LIBRA_UNIFORM_vertical_scanlines < 0.5)
    {
        LIBRA_VARYING_7 = vec2(LIBRA_UNIFORM_SHARPER * SourceSize.x, SourceSize.y);
        LIBRA_VARYING_4 = vec2(1.0, clamp(floor(SourceSize.y / ((LIBRA_UNIFORM_interlace_detect > 0.5) ? 200.0 : 1000.0)), 1.0, 2.0));
        LIBRA_VARYING_5 = LIBRA_VARYING_4 / LIBRA_VARYING_7;
        mod_factor = ((LIBRA_VARYING_0.x * SourceSize.x) * LIBRA_UNIFORM_OutputSize.x) / SourceSize.x;
    }
    else
    {
        LIBRA_VARYING_7 = vec2(SourceSize.x, LIBRA_UNIFORM_SHARPER * SourceSize.y);
        LIBRA_VARYING_4 = vec2(clamp(floor(SourceSize.x / ((LIBRA_UNIFORM_interlace_detect > 0.5) ? 200.0 : 1000.0)), 1.0, 2.0), 1.0);
        LIBRA_VARYING_5 = LIBRA_VARYING_4 / LIBRA_VARYING_7;
        mod_factor = ((LIBRA_VARYING_0.y * SourceSize.y) * LIBRA_UNIFORM_OutputSize.y) / SourceSize.y;
    }
}
