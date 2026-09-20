#include <metal_stdlib>
using namespace metal;

// CRT effect parameters (matches Swift CRTParams struct)
struct CRTParams {
    float2 inputSize;      // Source texture dimensions
    float2 outputSize;     // Output drawable dimensions
    float scanlineWeight;  // 0 = none, 1 = full black lines
    float curvature;       // Screen curvature (0 = flat)
    float bloomStrength;   // Glow strength
    float brightness;      // 1.0 = normal
    float contrast;        // 1.0 = normal
    float saturation;      // 1.0 = normal
};

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

// Simple passthrough fragment shader (CRT disabled)
fragment float4 presentFragmentShader(VertexOutput in [[stage_in]],
                                       texture2d<float> colorTexture [[texture(0)]]) {
    constexpr sampler textureSampler(mag_filter::nearest, min_filter::nearest);
    return colorTexture.sample(textureSampler, in.texCoord);
}

// Apply barrel distortion for CRT curvature
float2 applyCurvature(float2 uv, float curvature) {
    if (curvature <= 0.0) return uv;

    // Center coordinates
    float2 centered = uv * 2.0 - 1.0;

    // Apply barrel distortion
    float r2 = dot(centered, centered);
    float distort = 1.0 + r2 * curvature;
    centered *= distort;

    return centered * 0.5 + 0.5;
}

// Calculate scanline darkening
float scanlineMask(float y, float inputHeight, float weight) {
    if (weight <= 0.0) return 1.0;

    // Calculate which source scanline we're on
    float scanline = y * inputHeight;
    float intpart = floor(scanline);
    float fracpart = scanline - intpart;

    // Smooth scanline using sine wave for natural falloff
    float mask = 1.0 - weight * (1.0 - sin(fracpart * 3.14159));
    return clamp(mask, 0.0, 1.0);
}

// Simple 3x3 blur for bloom approximation
float4 sampleBloom(texture2d<float> tex, sampler s, float2 uv, float2 texelSize, float radius) {
    float4 sum = float4(0.0);
    float totalWeight = 0.0;

    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2(x, y) * texelSize * radius;
            float weight = 1.0 / (1.0 + length(float2(x, y)));
            sum += tex.sample(s, uv + offset) * weight;
            totalWeight += weight;
        }
    }

    return sum / totalWeight;
}

// Adjust saturation
float3 adjustSaturation(float3 color, float saturation) {
    float luminance = dot(color, float3(0.299, 0.587, 0.114));
    return mix(float3(luminance), color, saturation);
}

// CRT effect fragment shader
fragment float4 presentCRTFragmentShader(VertexOutput in [[stage_in]],
                                          texture2d<float> colorTexture [[texture(0)]],
                                          constant CRTParams& params [[buffer(0)]]) {

    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
    constexpr sampler nearestSampler(mag_filter::nearest, min_filter::nearest, address::clamp_to_edge);

    float2 uv = in.texCoord;

    // Apply curvature distortion
    float2 curvedUV = applyCurvature(uv, params.curvature);

    // Check if we're outside the curved screen area
    if (curvedUV.x < 0.0 || curvedUV.x > 1.0 || curvedUV.y < 0.0 || curvedUV.y > 1.0) {
        return float4(0.0, 0.0, 0.0, 1.0);  // Black border
    }

    // Sample the main texture with nearest neighbor for sharp pixels
    float4 color = colorTexture.sample(nearestSampler, curvedUV);

    // Apply bloom (glow from bright pixels)
    if (params.bloomStrength > 0.0) {
        float2 texelSize = 1.0 / params.inputSize;
        float4 bloom = sampleBloom(colorTexture, textureSampler, curvedUV, texelSize, 2.0);
        color.rgb = mix(color.rgb, max(color.rgb, bloom.rgb), params.bloomStrength);
    }

    // Apply scanlines
    float scanline = scanlineMask(curvedUV.y, params.inputSize.y, params.scanlineWeight);
    color.rgb *= scanline;

    // Apply brightness and contrast
    color.rgb = (color.rgb - 0.5) * params.contrast + 0.5;
    color.rgb *= params.brightness;

    // Apply saturation
    color.rgb = adjustSaturation(color.rgb, params.saturation);

    // Clamp final color
    color.rgb = clamp(color.rgb, 0.0, 1.0);

    return color;
}

// Bilinear scaling shader (GPU-accelerated upscale, no CRT effects)
fragment float4 presentScaledFragmentShader(VertexOutput in [[stage_in]],
                                             texture2d<float> colorTexture [[texture(0)]]) {
    constexpr sampler textureSampler(mag_filter::linear, min_filter::linear, address::clamp_to_edge);
    return colorTexture.sample(textureSampler, in.texCoord);
}
