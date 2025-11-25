// MetalShaders.metal
#include <metal_stdlib>
using namespace metal;

// Vertex input structure - matches the converted MetalVertex format.
// Note: NDSVertex in the emulator uses s32 fixed-point values, but those
// are converted to float on the CPU before uploading to Metal.
// This structure receives the converted data:
//   - float4 position: X, Y, Z, W in clip space (NDSVertex.position / 4096.0)
//   - float2 texCoord: S, T texture coords in TEXEL units (divided by 16)
//   - uchar4 color: R, G, B, A (0-255, copied directly from NDSVertex.color)
struct VertexInput {
    float4 position [[attribute(0)]];  // Position (X, Y, Z, W)
    float2 texCoord [[attribute(1)]];  // Texture coordinates in TEXEL units
    uchar4 color    [[attribute(2)]];  // Vertex color (R, G, B, A)
    float invW      [[attribute(3)]];  // 4096.0/raw_w for color perspective correction
    float invWTC    [[attribute(4)]];  // 256.0/w_normalized for texture coord perspective correction
};

// Vertex output / Fragment input structure
struct VertexOutput {
    float4 position [[position]];      // Clip-space position (required)
    float2 texCoord [[center_no_perspective]];  // Force linear interpolation
    float4 color [[center_no_perspective]];     // Force linear interpolation
    float invW [[center_no_perspective]];       // Force linear interpolation
    float invWTC [[center_no_perspective]];     // Force linear interpolation
};

// Vertex shader
// Transforms vertices from model space to clip space
// and passes through color and texture coordinates
vertex VertexOutput vertexShader(VertexInput in [[stage_in]]) {
    VertexOutput out;
    
    // Pass through the position directly (NDC with w=1, no perspective division needed)
    out.position = in.position;
    
    // Manual perspective correction for texture coordinates
    // Input texCoord is in TEXEL units (already divided by 16)
    // invWTC = 256.0 / w_normalized
    // Store: texel_coord * invWTC for interpolation
    // After linear interpolation, fragment shader will recover texel_coord by dividing
    out.texCoord = in.texCoord * in.invWTC;
    out.invW = in.invW;      // 4096.0/raw_w for colors (unused for now)
    out.invWTC = in.invWTC;  // 256.0/w_normalized for texture coordinates
    
    // Convert vertex color from 0-63 range (5-bit) to 0.0-1.0 range
    // NDS uses 5-bit RGB (0-31, stored as 0-63), alpha is unused (polygon alpha used instead)
    out.color = float4(in.color) / 63.0;
    
    return out;
}

struct PolygonAttributes {
    uint polygonID;       // Polygon ID for edge marking (0-63)
    bool enableFog;       // Whether fog is enabled for this polygon
    float polyAlpha;      // Polygon alpha from attributes (0.0-1.0)
    float texScaleS;      // 1.0 / width for normalization
    float texScaleT;      // 1.0 / height for normalization
    uint wrapModeS;       // 0=clamp, 1=repeat, 2=mirror
    uint wrapModeT;       // 0=clamp, 1=repeat, 2=mirror
};

struct FragmentOutput {
    float4 color [[color(0)]];        // Main color buffer
    uint polyID [[color(1)]];         // Polygon ID (for edge marking)
    float fogEnable [[color(2)]];     // Fog enable flag (for fog)
};

fragment FragmentOutput fragmentShaderTextured(
    VertexOutput in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]],
    constant PolygonAttributes& polyAttr [[buffer(0)]]  // Per-polygon attributes
) {
    FragmentOutput out;
    
    // Manual perspective correction to match soft rasterizer
    // Vertex shader stored: texel_coord * invWTC (where invWTC = 256.0/w_normalized)
    // Metal linearly interpolated this value across the polygon
    // Divide by interpolated invWTC to recover perspective-correct texel coordinate
    float invWTC_safe = max(in.invWTC, 0.0001f);
    float2 texCoordTexels = in.texCoord / invWTC_safe;
    
    // CRITICAL: Truncate to integer in TEXEL space (matching soft rasterizer's (s32) cast)
    int2 intTexCoord = int2(texCoordTexels);
    
    // Calculate texture sizes for wrapping
    int texSizeS = int(1.0 / polyAttr.texScaleS + 0.5);
    int texSizeT = int(1.0 / polyAttr.texScaleT + 0.5);
    int texMaskS = texSizeS - 1;  // For power-of-2: 16→15, 32→31, 64→63
    int texMaskT = texSizeT - 1;
    
    // Apply texture wrapping ON INTEGER TEXEL VALUES
    // This exactly matches the soft rasterizer's bitwise operations
    
    // S axis wrapping
    if (polyAttr.wrapModeS == 1u) {
        // Repeat: val &= sizemask
        intTexCoord.x = intTexCoord.x & texMaskS;
    } else if (polyAttr.wrapModeS == 2u) {
        // Mirror/Flip: val &= ((size << 1) - 1); if (val >= size) val = (size << 1) - val - 1;
        intTexCoord.x = intTexCoord.x & ((texSizeS << 1) - 1);
        if (intTexCoord.x >= texSizeS) {
            intTexCoord.x = (texSizeS << 1) - intTexCoord.x - 1;
        }
    } else {
        // Clamp: if (val < 0) val = 0; if (val > size-1) val = size-1;
        intTexCoord.x = clamp(intTexCoord.x, 0, texMaskS);
    }
    
    // T axis wrapping
    if (polyAttr.wrapModeT == 1u) {
        // Repeat: val &= sizemask
        intTexCoord.y = intTexCoord.y & texMaskT;
    } else if (polyAttr.wrapModeT == 2u) {
        // Mirror/Flip: val &= ((size << 1) - 1); if (val >= size) val = (size << 1) - val - 1;
        intTexCoord.y = intTexCoord.y & ((texSizeT << 1) - 1);
        if (intTexCoord.y >= texSizeT) {
            intTexCoord.y = (texSizeT << 1) - intTexCoord.y - 1;
        }
    } else {
        // Clamp: if (val < 0) val = 0; if (val > size-1) val = size-1;
        intTexCoord.y = clamp(intTexCoord.y, 0, texMaskT);
    }
    
    // Convert wrapped integer texel coordinates to normalized [0, 1] for Metal's sampler
    // Add 0.5 to sample from texel centers (standard GPU texture sampling)
    float2 normalizedTexCoord = float2((float(intTexCoord.x) + 0.5) * polyAttr.texScaleS,
                                       (float(intTexCoord.y) + 0.5) * polyAttr.texScaleT);
    
    // Calculate color
    // Nintendo DS uses 5-bit RGB colors (0-31 stored as 0-63) and polygon alpha (not vertex alpha)
    float4 texColor = colorTexture.sample(textureSampler, normalizedTexCoord);
    float3 vertexColorRGB = in.color.rgb;
    float4 finalColor = texColor * float4(vertexColorRGB, polyAttr.polyAlpha);
    
    // Discard fragments with very low alpha to prevent black artifacts
    // This matches OpenGL's behavior: discard if alpha < 0.001
    if (finalColor.a < 0.001) {
        discard_fragment();
    }
    
    out.color = finalColor;
    
    // Output polygon ID for edge marking
    out.polyID = polyAttr.polygonID;
    
    // Output fog enable flag (1.0 = enabled, 0.0 = disabled)
    out.fogEnable = polyAttr.enableFog ? 1.0 : 0.0;
    
    return out;
}

// =============================================================
// POST PROCESSING SHADERS
// =============================================================

// Render states structure - must match the MetalRender RenderStates struct EXACTLY
// C++ uses uint32_t for bools to ensure 4-byte alignment, so Metal must use uint
struct RenderStates {
    uint enableAntialiasing;   // uint32_t in C++
    uint enableFogAlphaOnly;   // uint32_t in C++   
    int clearPolyID;           // 4 bytes
    float clearDepth;          // 4 bytes
    float alphaTestRef;        // 4 bytes
    float fogOffset;           // 4 bytes - Integer value [0, 32767] stored as float
    float fogStep;             // 4 bytes - Integer value [0, 32767] stored as float
    float pad_0;               // 4 bytes - Alignment padding
    float4 fogColor;           // 16 bytes
    float4 edgeColor[8];       // 128 bytes (8 * 16)
    float4 toonColor[32];      // 512 bytes (32 * 16)
};

// Vertex structure for fullscreen quad
struct PostprocessVertexInput {
    float2 position [[attribute(0)]];  // NDC position (-1 to 1)
    float2 texCoord [[attribute(1)]];  // Texture coordinate (0 to 1)
};

struct PostprocessVertexOutput {
    float4 position [[position]];
    float2 texCoord;
};

// Simple pass-through vertex shader for fullscreen quad
vertex PostprocessVertexOutput postprocessVertex(
    PostprocessVertexInput in [[stage_in]]
) {
    PostprocessVertexOutput out;
    out.position = float4(in.position, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

// ============================================================================
// EDGE MARKING SHADER
// ============================================================================
// Edge marking detects polygon boundaries by comparing polygon IDs
// in a cross pattern (center + 4 neighbors: right, up, left, down)

fragment float4 edgeMarkFragment(
    PostprocessVertexOutput in [[stage_in]],
    texture2d<float> depthTexture [[texture(0)]],     // Depth buffer
    texture2d<uint> polyIDTexture [[texture(1)]],     // Polygon ID buffer
    constant RenderStates& state [[buffer(0)]]
) {
    // Get integer coordinates for texel fetches
    int2 coord = int2(in.position.xy);
    
    // Sample polygon ID at center and 4 neighbors (cross pattern)
    // Each polygon has an ID (0-63) which we read from the stencil/ID buffer
    uint polyID[5];
    polyID[0] = polyIDTexture.read(uint2(coord + int2( 0,  0))).r;  // Center
    polyID[1] = polyIDTexture.read(uint2(coord + int2( 1,  0))).r;  // Right
    polyID[2] = polyIDTexture.read(uint2(coord + int2( 0,  1))).r;  // Up
    polyID[3] = polyIDTexture.read(uint2(coord + int2(-1,  0))).r;  // Left
    polyID[4] = polyIDTexture.read(uint2(coord + int2( 0, -1))).r;  // Down
    
    // Sample depth at same locations
    float depth[5];
    depth[0] = depthTexture.read(uint2(coord + int2( 0,  0))).r;
    depth[1] = depthTexture.read(uint2(coord + int2( 1,  0))).r;
    depth[2] = depthTexture.read(uint2(coord + int2( 0,  1))).r;
    depth[3] = depthTexture.read(uint2(coord + int2(-1,  0))).r;
    depth[4] = depthTexture.read(uint2(coord + int2( 0, -1))).r;
    
    // No edge by default
    float4 edgeColor = float4(0.0);
    
    // Check each neighbor against the center
    // If polygon IDs differ AND depth is valid, we have an edge
    for (int i = 1; i < 5; i++) {
        if (polyID[0] != polyID[i]) {
            // Edge detected! Use the neighbor's polygon ID to pick the edge color
            // The DS uses 3-bit edge color selection: polygon ID bits 3-5
            uint edgeColorIndex = (polyID[i] >> 3) & 0x7;
            
            // Blend edge color based on depth difference
            // Larger depth differences = more visible edges
            float depthDiff = abs(depth[0] - depth[i]);
            float edgeStrength = min(depthDiff * 8.0, 1.0);
            
            // Accumulate edge color (allows multiple edges to blend)
            edgeColor = max(edgeColor, state.edgeColor[edgeColorIndex] * edgeStrength);
        }
    }
    
    return edgeColor;
}

// ============================================================================
// FOG SHADER
// ============================================================================
// Fog blends the rendered color toward the fog color based on depth
// using a density lookup table

fragment float4 fogFragment(
    PostprocessVertexOutput in [[stage_in]],
    texture2d<float> depthTexture [[texture(0)]],          // Depth buffer
    texture2d<float> fogAttributesTexture [[texture(1)]], // Per-pixel fog enable
    texture1d<float> fogDensityTable [[texture(2)]],      // Fog density LUT
    constant RenderStates& state [[buffer(0)]]
) {
    // Read depth and fog attributes for this pixel
    float depth = depthTexture.sample(sampler(min_filter::nearest), in.texCoord).r;
    float4 fogAttribs = fogAttributesTexture.sample(sampler(min_filter::nearest), in.texCoord);
    
    // Check if fog is enabled for this pixel (per-polygon setting)
    // Stored in red channel, > 0.999 means fog enabled
    bool polyEnableFog = (fogAttribs.r > 0.999);
    
    if (!polyEnableFog) {
        // Fog disabled for this polygon, output transparent (no fog blend)
        return float4(0.0);
    }
    
    // Calculate fog density using the NDS fog formula
    // depth is normalized [0, 1], fogOffset and fogStep are in range [0, 32767]
    // We need to scale depth to match the fog parameters
    
    int fogIndex;
    float depthScaled = depth * 32768.0;  // Scale to [0, 32768]
    
    if (state.fogStep == 0.0) {
        // Special case: fog step is 0, use simple threshold
        fogIndex = (depthScaled <= state.fogOffset) ? 0 : 31;
    } else if (depthScaled < state.fogOffset) {
        // Before fog starts, use first density value
        fogIndex = 0;
    } else {
        // Map depth to density table index (0-31)
        // This formula matches the NDS hardware fog calculation
        fogIndex = int((depthScaled - state.fogOffset) / state.fogStep);
        fogIndex = clamp(fogIndex, 0, 31);
    }
    
    // Look up fog density from the 32-entry table
    // Density is normalized [0, 1] where 1 = full fog
    // Cast to uint explicitly to resolve ambiguity
    float fogDensity = fogDensityTable.read(uint(fogIndex)).r;
    
    // Return fog weight as RGBA
    // This will be blended with the scene using special blend modes
    // Alpha controls fog intensity, RGB is fog color
    float4 fogWeight = state.fogColor * fogDensity;
    fogWeight.a = fogDensity;
    
    return fogWeight;
}