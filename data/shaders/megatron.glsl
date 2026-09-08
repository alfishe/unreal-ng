// Sony Megatron-style CRT shader
// Resolution-adaptive phosphor simulation with authentic CRT feel
// Cross-platform GLSL (OpenGL 2.1+ / GLSL 1.20)
//
// This shader is designed to scale with output resolution, simulating
// connecting a ZX Spectrum to a real CRT display.

#version 120

varying vec2 texCoord;
uniform sampler2D tex;
uniform vec2 texSize;       // Source texture size (352x288 typically)
uniform vec2 outputSize;    // Target output size

// Profile parameters (passed from CRTProfileParams)
uniform int maskType;       // 0=none, 1=aperture, 2=shadow, 3=slot
uniform float curvature;
uniform float cornerRadius;
uniform float scanlineWeight;
uniform float maskStrength;
uniform float maskDotPitch;
uniform float bloomStrength;
uniform float saturation;
uniform float brightness;
uniform float contrast;
uniform float gamma;

// Curvature with barrel distortion
vec2 curve(vec2 uv) {
    if (curvature < 0.001) return uv;
    uv = (uv - 0.5) * 2.0;
    float r2 = dot(uv, uv);
    uv *= 1.0 + curvature * r2;
    return uv * 0.5 + 0.5;
}

// Rounded corner mask with smooth falloff
float cornerMask(vec2 uv) {
    if (cornerRadius < 0.001) return 1.0;
    vec2 d = abs(uv - 0.5) * 2.0;
    float r = cornerRadius;
    vec2 corner = max(d - (1.0 - r), 0.0);
    return 1.0 - smoothstep(r - 0.02, r + 0.01, length(corner));
}

// Aperture grille (Trinitron-style vertical RGB stripes)
// Pitch is calculated from output resolution for authentic scaling
vec3 apertureGrille(vec2 pos, float pitch) {
    float stripe = mod(pos.x, pitch * 3.0) / pitch;
    vec3 mask;
    if (stripe < 1.0)       mask = vec3(1.0, 0.15, 0.15);
    else if (stripe < 2.0)  mask = vec3(0.15, 1.0, 0.15);
    else                    mask = vec3(0.15, 0.15, 1.0);

    // Soften mask edges for smoother look at various scales
    float edge = smoothstep(0.0, 0.2, mod(stripe, 1.0)) *
                 smoothstep(1.0, 0.8, mod(stripe, 1.0));
    mask = mix(vec3(0.3), mask, edge * 0.7 + 0.3);

    return mix(vec3(1.0), mask, maskStrength);
}

// Gaussian-weighted bloom for phosphor glow
vec3 bloom(vec2 uv, float radius) {
    vec3 sum = vec3(0.0);
    float total = 0.0;
    vec2 texel = 1.0 / texSize;

    // 5x5 gaussian kernel
    for (float x = -2.0; x <= 2.0; x += 1.0) {
        for (float y = -2.0; y <= 2.0; y += 1.0) {
            float d = length(vec2(x, y));
            float weight = exp(-d * d / (2.0 * radius));
            vec3 sample = texture2D(tex, uv + vec2(x, y) * texel * radius).rgb;
            // Weight bright pixels more for bloom
            float luma = dot(sample, vec3(0.299, 0.587, 0.114));
            weight *= 0.5 + luma * 0.5;
            sum += sample * weight;
            total += weight;
        }
    }
    return sum / total;
}

void main() {
    vec2 uv = curve(texCoord);

    // Clip outside curved screen area
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Sample source texture
    vec3 color = texture2D(tex, uv).rgb;

    // Apply phosphor bloom (key to Megatron look)
    if (bloomStrength > 0.001) {
        vec3 glow = bloom(uv, 2.5);
        // Additive bloom weighted toward bright areas
        color = color + glow * bloomStrength * 0.5;
    }

    // Linearize with display gamma
    color = pow(color, vec3(gamma));

    // Resolution-adaptive scanlines
    // Only apply if output is high enough resolution
    float pixelScale = outputSize.y / texSize.y;
    if (scanlineWeight > 0.001 && pixelScale > 1.5) {
        float scanY = uv.y * texSize.y;
        float scanline = 0.5 + 0.5 * cos(scanY * 3.14159 * 2.0);
        scanline = pow(scanline, 1.0 / pixelScale);  // Soften at lower scales
        color *= mix(1.0, scanline, scanlineWeight * min(1.0, pixelScale / 3.0));
    }

    // Phosphor mask (aperture grille for Megatron)
    vec2 pixelPos = uv * outputSize;
    float pitch = maskDotPitch;
    if (pitch < 1.0) {
        // Auto pitch: aim for ~640 pixels across a typical CRT
        pitch = max(1.0, outputSize.x / 640.0);
    }

    if (maskType == 1) {  // Aperture grille
        color *= apertureGrille(pixelPos, pitch);
    }

    // Color adjustments
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation);  // Saturation
    color = (color - 0.5) * contrast + 0.5;       // Contrast
    color *= brightness;                          // Brightness

    // De-gamma for output
    color = pow(max(color, 0.0), vec3(1.0 / 2.2));

    // Apply corner mask
    color *= cornerMask(uv);

    // Subtle vignette
    float vignette = 1.0 - pow(length(texCoord - 0.5) * 1.0, 2.5) * 0.2;
    color *= vignette;

    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
