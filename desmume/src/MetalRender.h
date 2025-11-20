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
#endif

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
  virtual void _ClearImageBaseLoop(const u16 *__restrict inColor16,
                                   const u16 *__restrict inDepth16,
                                   u16 *__restrict outColor16,
                                   u32 *__restrict outDepth24,
                                   u8 *__restrict outFog);
  template <bool ISCOLORBLANK, bool ISDEPTHBLANK>
  void _ClearImageScrolledLoop(const u8 xScroll, const u8 yScroll,
                               const u16 *__restrict inColor16,
                               const u16 *__restrict inDepth16,
                               u16 *__restrict outColor16,
                               u32 *__restrict outDepth24,
                               u8 *__restrict outFog);

  virtual Render3DError BeginRender(const GFX3D_State &renderState,
                                    const GFX3D_GeometryList &renderGList);
  virtual Render3DError RenderGeometry();
  virtual Render3DError PostprocessFramebuffer();
  virtual Render3DError EndRender();

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
  bool _enableAlphaBlending; // Whether alpha blending is enabled

  // The DS supports 4 wrap modes for each axis:
  // 0: Clamp to edge (no repeat)
  // 1: Repeat
  // 2: Mirrored repeat
  // 3: Flip
  MTLSamplerStatePtr _samplerStateClampNearest;  // Clamp, nearest filtering
  MTLSamplerStatePtr _samplerStateClampLinear;   // Clamp, linear filtering
  MTLSamplerStatePtr _samplerStateRepeatNearest; // Repeat, nearest filtering
  MTLSamplerStatePtr _samplerStateRepeatLinear;  // Repeat, linear filtering

  MetalTexture *GetLoadedTextureFromPolygon(const POLY &thePoly,
                                            bool enableTextureSampling);

  Render3DError InitializePipelineState();
  Render3DError InitializeDepthStencilState();
  Render3DError InitializeRenderTargets(size_t width, size_t height);
  Render3DError InitializeSamplerState();

  // Helper function to get the correct depth/stencil state for a polygon
  MTLDepthStencilStatePtr
  GetDepthStencilStateForPolygon(const POLY &thePoly, bool treatAsTranslucent);
};

#endif