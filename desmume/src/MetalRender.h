#ifndef METAL_RENDER_H
#define METAL_RENDER_H

#include "render3D.h"

// Forward declare Objective-C types as opaque pointers for C++ compatibility
#ifdef __OBJC__
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLRenderPipelineState;
@protocol MTLDepthStencilState;
@protocol MTLBuffer;
@protocol MTLTexture;
@protocol MTLRenderPassDescriptor;
@protocol MTLRenderCommandEncoder;
@protocol MTLSamplerState;
@class MTLTextureDescriptor;
typedef id<MTLDevice> MTLDevicePtr;
typedef id<MTLCommandQueue> MTLCommandQueuePtr;
typedef id<MTLCommandBuffer> MTLCommandBufferPtr;
typedef id<MTLRenderPipelineState> MTLRenderPipelineStatePtr;
typedef id<MTLDepthStencilState> MTLDepthStencilStatePtr;
typedef id<MTLBuffer> MTLBufferPtr;
typedef id<MTLTexture> MTLTexturePtr;
typedef MTLRenderPassDescriptor *MTLRenderPassDescriptorPtr;
typedef id<MTLRenderCommandEncoder> MTLRenderCommandEncoderPtr;
typedef id<MTLSamplerState> MTLSamplerStatePtr;
typedef MTLTextureDescriptor *MTLTextureDescriptorPtr;
#else
typedef void *MTLDevicePtr;
typedef void *MTLCommandQueuePtr;
typedef void *MTLCommandBufferPtr;
typedef void *MTLRenderPipelineStatePtr;
typedef void *MTLDepthStencilStatePtr;
typedef void *MTLBufferPtr;
typedef void *MTLTexturePtr;
typedef void *MTLRenderPassDescriptorPtr;
typedef void *MTLRenderCommandEncoderPtr;
typedef void *MTLSamplerStatePtr;
typedef void *MTLTextureDescriptorPtr;
#endif

class MetalRenderColorOut : public Render3DColorOut {
protected:
  MTLDevicePtr _device;        // Reference to Metal device for operations
  MTLTexturePtr _colorTexture; // Reference to the render target texture

  // Double-buffered CPU-accessible memory for reading back framebuffer
  // This allows GPU to write to one buffer while CPU reads from the other
  Color4u8 *_masterBuffer32; // Single allocation for both buffers

  // These pointers point into _masterBuffer32, split into two halves
  Color4u8 *_readbackBuffer[2]; // Two buffers for async readback

  bool _needsColorConversion; // Whether we need BGR666_Rev conversion

  // Perform color format conversion if needed (RGBA8888 to BGR666_Rev)
  void _ConvertColorFormat(Color4u8 *buffer, size_t pixelCount);

public:
  MetalRenderColorOut(MTLDevicePtr device, size_t w, size_t h);
  virtual ~MetalRenderColorOut();

  virtual void Reset();

  // Called by emulator to bind buffer for reading
  virtual size_t BindRead32();
  virtual size_t UnbindRead32();

  // Called by renderer before/after rendering
  virtual size_t BindRenderer();
  virtual void UnbindRenderer(const size_t idxRead);

  virtual Render3DError SetSize(size_t w, size_t h);
  virtual const Color4u8 *GetFramebuffer32() const;

  virtual Render3DError FillZero();
  virtual Render3DError FillColor32(const Color4u8 *src,
                                    const bool isSrcNativeSize);

  // Override to track when color format conversion is needed
  virtual void SetColorFormat(NDSColorFormat theFormat);

  // Set the texture to read from (called by MetalRender)
  void SetColorTexture(MTLTexturePtr texture);
};

class MetalTexture : public Render3DTexture {
protected:
  MTLTexturePtr _texID; // The actual Metal texture object
  MTLDevicePtr _device; // Reference to Metal device for creating textures
  bool _isTexInited;    // Whether the Metal texture has been created
  u32 *_unpackBuffer;   // Buffer for unpacking texture data from NDS format

public:
  MetalTexture(TEXIMAGE_PARAM texAttributes, u32 palAttributes,
               MTLDevicePtr device);
  virtual ~MetalTexture();

  // Override the Load method to create Metal textures
  virtual void Load(void *targetBuffer);

  // Get the Metal texture object
  MTLTexturePtr GetTexID() const;

  // Check if the texture is initialized
  bool IsTexInited() const;

  u32 *GetUnpackBuffer() const;
};

class MetalRender : public Render3D {
public:
  MetalRender();
  ~MetalRender();

  virtual Render3DError ApplyRenderingSettings(const GFX3D_State &renderState);

  virtual Render3DError Reset(); // Called when the emulator resets.

  virtual Render3DError
  RenderPowerOff(); // Called when the renderer needs to handle a power-off
                    // condition by clearing its framebuffers.

  virtual Render3DError
  Render(const GFX3D_State &renderState,
         const GFX3D_GeometryList
             &renderGList); // Called whenever the 3D renderer needs to render
                            // the geometry lists.

  virtual Render3DError
  RenderFinish(); // Called whenever 3D rendering needs to finish. This function
                  // should block the calling thread and only release the block
                  // when 3D rendering is finished. (Before reading the 3D
                  // layer, be sure to always call this function.)

  virtual Render3DError RenderFlush(
      bool willFlushBuffer32,
      bool willFlushBuffer16); // Called whenever the emulator needs the flushed
                               // results of the 3D renderer. Before calling
                               // this, the 3D renderer must be finished using
                               // RenderFinish() or confirmed already finished
                               // using GetRenderNeedsFinish().

  virtual Render3DError
  VramReconfigureSignal(); // Called when the emulator reconfigures its VRAM.
                           // You may need to invalidate your texture cache.

  virtual Render3DError SetFramebufferSize(
      size_t w,
      size_t h); // Called whenever the output framebuffer size changes.

  virtual NDSColorFormat RequestColorFormat(
      NDSColorFormat
          colorFormat); // Called whenever the output framebuffer color format
                        // changes. The framebuffer output by the 3D renderer is
                        // expected to match the requested format. If the
                        // internal color format of the 3D renderer doesn't
                        // natively match the requested format, then a
                        // colorspace conversion will be required in order to
                        // match. The only exception to this rule is if the
                        // requested output format is RGBA5551. In this
                        // particular case, the 3D renderer is expected to
                        // output a framebuffer in RGBA6665 color format. Again,
                        // if the internal color format does not match this,
                        // then a colorspace conversion will be required for
                        // RGBA6665.

  virtual NDSColorFormat
  GetColorFormat() const; // The output color format of the 3D renderer.

  virtual Render3DError FillZero();
  virtual Render3DError FillColor32(const Color4u8 *__restrict src,
                                    const bool isSrcNativeSize);

  virtual ClipperMode GetPreferredPolygonClippingMode() const;

protected:
  virtual Render3DError BeginRender(const GFX3D_State &renderState,
                                    const GFX3D_GeometryList &renderGList);
  virtual Render3DError RenderGeometry();
  virtual Render3DError PostprocessFramebuffer();
  virtual Render3DError EndRender();

  virtual Render3DError ClearUsingImage(const u16 *__restrict colorBuffer,
                                        const u32 *__restrict depthBuffer,
                                        const u8 *__restrict fogBuffer,
                                        const u8 opaquePolyID);

  virtual Render3DError
  ClearUsingValues(const Color4u8 &clearColor6665,
                   const FragmentAttributes &clearAttributes);

  virtual Render3DError SetupTexture(const POLY &thePoly,
                                     size_t polyRenderIndex);
  virtual Render3DError SetupViewport(const GFX3D_Viewport viewport);

private:
  MTLDevicePtr _device;                     // Metal device
  MTLCommandQueuePtr _commandQueue;         // Metal command queue
  MTLCommandBufferPtr _commandBuffer;       // Metal command buffer
  MTLRenderPipelineStatePtr _pipelineState; // Pipeline state for rendering
  MTLRenderCommandEncoderPtr _renderCommandEncoder; // Render command encoder
  // Depth/stencil states for different rendering modes
  MTLDepthStencilStatePtr _depthStencilStateOpaque; // Normal opaque polygons
  MTLDepthStencilStatePtr _depthStencilStateDepthEqual; // Depth-equal test mode
  MTLDepthStencilStatePtr
      _depthStencilStateTranslucent; // Translucent, no depth write
  MTLDepthStencilStatePtr
      _depthStencilStateTranslucentDepthWrite; // Translucent with depth write
  MTLDepthStencilStatePtr
      _depthStencilStateShadowPass1; // Shadow volume mask generation
  MTLDepthStencilStatePtr
      _depthStencilStateShadowPass2;                // Shadow polygon ID check
  MTLBufferPtr _vertexBuffer;                       // Vertex buffer
  MTLBufferPtr _indexBuffer;                        // Index buffer
  MTLTexturePtr _colorTexture;                      // Render target
  MTLTexturePtr _depthTexture;                      // Depth buffer
  MTLRenderPassDescriptorPtr _renderPassDescriptor; // Render pass descriptor
  bool _enableAlphaBlending;           // Whether alpha blending is enabled
  bool _enableAntialiasing;            // Whether antialiasing is enabled
  MetalRenderColorOut *_metalColorOut; // Color output object

  // The DS supports 4 wrap modes for each axis:
  // 0: Clamp to edge (no repeat)
  // 1: Repeat
  // 2: Mirrored repeat
  // 3: Flip
  MTLSamplerStatePtr _samplerStateClampNearest;  // Clamp, nearest filtering
  MTLSamplerStatePtr _samplerStateClampLinear;   // Clamp, linear filtering
  MTLSamplerStatePtr _samplerStateRepeatNearest; // Repeat, nearest filtering
  MTLSamplerStatePtr _samplerStateRepeatLinear;  // Repeat, linear filtering
  MTLSamplerStatePtr _samplerStateMirrorNearest; // Mirror, nearest filtering
  MTLSamplerStatePtr _samplerStateMirrorLinear;  // Mirror, linear filtering

  // Postprocessing pipeline states and textures
  MTLRenderPipelineStatePtr _pipelineStateEdgeMark; // Pipeline for edge marking
  MTLRenderPipelineStatePtr _pipelineStateFog; // Pipeline for fog rendering

  // Postprocessing textures
  MTLTexturePtr _polygonIDTexture; // Stores the polygon id for edge detection
  MTLTexturePtr _fogAttributesTexture; // Stores per pixel fog enabled
  MTLTexturePtr _fogDensityTexture; // 1D texture with fog density lookup table

  MTLBufferPtr _postprocessVertexBuffer; // Fullscreen quad vertex buffer
  MTLBufferPtr _renderStatesBuffer;      // Uniform buffer for  fog/edge colors

  // Cached render states for postprocessing
  struct RenderStates {
    bool enableAntialiasing;
    bool enableFogAlphaOnly;
    int clearPolyID;
    float clearDepth;
    float alphaTestRef;
    float fogOffset; // Integer value [0, 32767] stored as float
    float fogStep;   // Integer value [0, 32767] stored as float
    float pad_0;     // Alignment padding
    float fogColor[4];
    float edgeColor[8][4];  // 8 edge colors, each RGBA
    float toonColor[32][4]; // Not used in postprocess, but kept for alignment
  } _currentRenderStates;

  Render3DError InitializePostprocessPipelines();
  Render3DError CreateFullscreenQuad();

  MetalTexture *GetLoadedTextureFromPolygon(const POLY &thePoly,
                                            bool enableTextureSampling);

  Render3DError InitializePipelineState();
  Render3DError InitializeDepthStencilState();
  Render3DError InitializeRenderTargets(size_t width, size_t height);
  Render3DError InitializeSamplerState();

  // Helper function to get the correct depth/stencil state for a polygon
  MTLDepthStencilStatePtr
  GetDepthStencilStateForPolygon(const POLY &thePoly, bool treatAsTranslucent);

  // Helper function to determine Metal cull mode from polygon attributes
  // Returns the appropriate MTLCullMode based on the polygon's surface culling
  // mode
  // Return type is unsigned long to match MTLCullMode enum (C++ compatibility)
  unsigned long GetCullModeForPolygon(const POLY &thePoly) const;

  // Helper function to determine front-face winding order
  // The DS hardware determines facing based on polygon winding, and we need to
  // configure Metal to match
  // Return type is unsigned long to match MTLWinding enum (C++ compatibility)
  unsigned long GetWindingOrderForPolygon(const CPoly &cPoly) const;
};

// GPU3DInterface for Metal renderer
extern GPU3DInterface gpu3DMetal;

// C-style opaque factory functions for creating/destroying MetalRender
// instances
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a new MetalRender instance as an opaque pointer.
 *
 * @return Opaque pointer to MetalRender instance, or NULL on failure
 *
 * Caller is responsible for destroying the instance with
 * MetalRendererDestroyOpaque().
 */
void *MetalRendererCreateOpaque(void);

/**
 * Destroy a MetalRender instance created by MetalRendererCreateOpaque().
 *
 * @param renderer Opaque pointer to MetalRender instance (can be NULL)
 */
void MetalRendererDestroyOpaque(void *renderer);

#ifdef __cplusplus
}
#endif

#endif