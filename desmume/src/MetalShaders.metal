// MetalShaders.metal
#include <metal_stdlib>
using namespace metal;

// Vertex input structure - should match NDSVertex from the emulator
// Recall: NDSVertex has:
//   - float position[4] (X, Y, Z, W)
//   - float texCoord[2] (S, T)
//   - u8 color[4] (R, G, B, A)
struct VertexInput {
    float4 position [[attribute(0)]];  // Position (X, Y, Z, W)
    float2 texCoord [[attribute(1)]];  // Texture coordinates (S, T)
    uchar4 color    [[attribute(2)]];  // Vertex color (R, G, B, A)
};

// Vertex output / Fragment input structure
struct VertexOutput {
    float4 position [[position]];      // Clip-space position (required)
    float2 texCoord;                   // Texture coordinates (interpolated)
    float4 color;                      // Vertex color (interpolated, normalized to 0-1)
};

// Vertex shader
// Transforms vertices from model space to clip space
// and passes through color and texture coordinates
vertex VertexOutput vertexShader(VertexInput in [[stage_in]]) {
    VertexOutput out;
    
    // Pass through the position directly
    // The NDS already provides positions in clip space (-1 to 1)
    out.position = in.position;
    
    // Pass through texture coordinates
    out.texCoord = in.texCoord;
    
    // Convert vertex color from 0-255 range to 0.0-1.0 range
    out.color = float4(in.color) / 255.0;
    
    return out;
}


// Fragment shader
// Determines the final color of each pixel
fragment float4 fragmentShaderTextured(
    VertexOutput in [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]]
) {
    // Sample the texture at the interpolated texture coordinate
    float4 texColor = colorTexture.sample(textureSampler, in.texCoord);
    
    // Modulate texture color with vertex color
    return texColor * in.color;
}



