#include <metal_stdlib>
using namespace metal;

struct VertexInput {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    uint  faceIndex;
};

struct VertexOutput {
    float4 position [[position]];
    float2 texCoord;
    float3 normal;
    uint   faceIndex [[flat]];
};

struct Uniforms {
    float4x4 mvpMatrix;
    float4x4 modelMatrix;
    float4   lightDir;
    float    crtWeight;
    float3   padding;
};

vertex VertexOutput cubeVertexShader(uint vertexID [[vertex_id]],
                                     constant VertexInput* vertices [[buffer(0)]],
                                     constant Uniforms& uniforms [[buffer(1)]]) {
    VertexInput in = vertices[vertexID];
    VertexOutput out;
    float3 pos = float3(in.px, in.py, in.pz);
    float3 norm = float3(in.nx, in.ny, in.nz);
    out.position = uniforms.mvpMatrix * float4(pos, 1.0);
    out.texCoord = float2(in.u, in.v);
    out.normal = (uniforms.modelMatrix * float4(norm, 0.0)).xyz;
    out.faceIndex = in.faceIndex;
    return out;
}

fragment float4 cubeFragmentShader(VertexOutput in [[stage_in]],
                                   texture2d<float> tex0 [[texture(0)]],
                                   texture2d<float> tex1 [[texture(1)]],
                                   texture2d<float> tex2 [[texture(2)]],
                                   texture2d<float> tex3 [[texture(3)]],
                                   texture2d<float> tex4 [[texture(4)]],
                                   texture2d<float> tex5 [[texture(5)]],
                                   constant Uniforms& uniforms [[buffer(1)]]) {
    constexpr sampler sam(address::clamp_to_edge, filter::linear);
    float4 color = float4(0.0);

    switch (in.faceIndex) {
        case 0: color = tex0.sample(sam, in.texCoord); break;
        case 1: color = tex1.sample(sam, in.texCoord); break;
        case 2: color = tex2.sample(sam, in.texCoord); break;
        case 3: color = tex3.sample(sam, in.texCoord); break;
        case 4: color = tex4.sample(sam, in.texCoord); break;
        case 5: color = tex5.sample(sam, in.texCoord); break;
        default: color = float4(1.0, 0.0, 1.0, 1.0); break;
    }

    // Directional Lighting
    float3 norm = normalize(in.normal);
    float diff = max(dot(norm, normalize(uniforms.lightDir.xyz)), 0.35);
    color.rgb *= diff;

    // Subtle CRT Scanline overlay
    if (uniforms.crtWeight > 0.0) {
        float scanline = sin(in.texCoord.y * 480.0 * 3.14159) * 0.5 + 0.5;
        color.rgb *= mix(1.0, scanline, uniforms.crtWeight);
    }

    return color;
}
