#include <metal_stdlib>
using namespace metal;

struct VertexOutput {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOutput presentVertexShader(uint vertexID [[vertex_id]],
                                        constant float2* positions [[buffer(0)]],
                                        constant float2* texCoords [[buffer(1)]]) {
    VertexOutput out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.texCoord = texCoords[vertexID];
    return out;
}

fragment float4 presentFragmentShader(VertexOutput in [[stage_in]],
                                       texture2d<float> colorTexture [[texture(0)]]) {
    constexpr sampler textureSampler(mag_filter::nearest, min_filter::nearest);
    return colorTexture.sample(textureSampler, in.texCoord);
}
