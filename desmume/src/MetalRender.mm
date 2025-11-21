#include <Metal/Metal.h>
#include <cstddef>

#include "GPU.h"
#include "GPU_Operations.h"
#include "MetalRender.h"
#include "common.h"
#include "render3D.h"
#include "types.h"
#include "utils/colorspacehandler/colorspacehandler.h"

#ifdef __OBJC__
#import "frontend/cocoa/userinterface/MacMetalDisplayView.h"
#endif

// Global shared Metal data
static MetalDisplayViewSharedData *SharedMetalData = nil;

// Function pointer implementations
bool (*metalrender_init)() = nullptr;
void (*metalrender_deinit)() = nullptr;
bool (*metalrender_beginMetal)() = nullptr;
void (*metalrender_endMetal)() = nullptr;

// Shared resource injection
extern "C" void metal_setSharedResources(void *sharedData)
{
    @autoreleasepool {
        if (SharedMetalData != nil)
        {
            [(id)SharedMetalData release];
            SharedMetalData = nil;
        }
        
        if (sharedData != nullptr)
        {
            SharedMetalData = (MetalDisplayViewSharedData *)sharedData;
            [(id)SharedMetalData retain];
            
            printf("Metal 3D Renderer: Received shared Metal resources\n");
            printf("  Device: %s\n", [[[SharedMetalData device] name] UTF8String]);
        }
    }
}

// Helper functions
extern "C" MTLDevicePtr metal_getSharedDevice()
{
    return [SharedMetalData device];
}

extern "C" MTLCommandQueuePtr metal_getSharedCommandQueue()
{
    return [SharedMetalData commandQueue];
}


// The emulator seems to call the renderer in this sequence:
// Render(renderState, renderGList)
//   -> BeginRender // setup for this frame
//   -> ClearFramebuffer // clear the framebuffer, calls:
//      -> ClearUsingValues // clear the framebuffer
//      -> ClearUsingImage // clear the framebuffer using an image sequence, do
//            not implement this yet.
//   -> RenderGeometry // draw the polygons
//   -> PostprocessFramebuffer // Apply any effects, such as fog, edge marking,
//   -> EndRender // cleanup
//
// Then the following:
// RenderFinish
// RenderFlush

// Here are a few structures that are used by the renderer:
// GFX3D_GeometryList
//   * NDSVertex *rawVtxList - Vertex data such as position, texture
//       coordinates, and color
//   * POLY *rawPolyList - Polygon attributes
//   * CPoly *clippedPolyList - Clipped polygons (what to actually draw)
//   * size_t rawVertCount - Total vertices
//   * size_t clippedPolyCount - Total polygons to draw
//   * size_t clippedPolyOpaqueCount - Opaque polygons (draw first)
//
// POLY
//   * POLYGON_ATTR attribute - Blending, culling, lights, fog
//   * TEXIMAGE_PARAM texParam - Texture format, size, addressing
//   * u32 texPalette - Palette address
//   * GFX3D_Viewport viewport - Scissor rectangle
//   * ... material properties, vertex indices, etc.
//
// NDSVertex
//   * float position[4] - X, Y, Z, W
//   * float texCoord[2] - S, T
//   * u8 color[4] - R, G, B, A

// Metal vertex structure that matches the vertex descriptor.
// NDSVertex uses s32 fixed-point values for position and texCoord,
// but Metal expects float values. This structure is used for the
// converted vertex data uploaded to the Metal vertex buffer.
struct MetalVertex {
  float position[4]; // X, Y, Z, W in clip space (converted from s32 / 4096.0)
  float texCoord[2]; // S, T texture coords in TEXEL units (divided by 16)
  u8 color[4];       // R, G, B, A (0-255, copied as-is)
  float invW;        // 4096.0/raw_w for color perspective correction
  float invWTC;      // 256.0/w_normalized for texture coordinate perspective correction
};

MetalRender::MetalRender()
    : _device(nil), _commandQueue(nil), _commandBuffer(nil),
      _pipelineState(nil), _renderCommandEncoder(nil),
      _depthStencilStateOpaque(nil), _depthStencilStateDepthEqual(nil),
      _depthStencilStateTranslucent(nil),
      _depthStencilStateTranslucentDepthWrite(nil),
      _depthStencilStateShadowPass1(nil), _depthStencilStateShadowPass2(nil),
      _vertexBuffer{nil, nil}, _vertexBufferIndex(0), _indexBuffer(nil), _colorTexture(nil),
      _depthTexture(nil), _renderPassDescriptor(nil),
      _enableAlphaBlending(false), _enableAntialiasing(false),
      _metalColorOut(nullptr), _samplerStateClampNearest(nil),
      _samplerStateClampLinear(nil), _samplerStateRepeatNearest(nil),
      _samplerStateRepeatLinear(nil), _samplerStateMirrorNearest(nil),
      _samplerStateMirrorLinear(nil), _renderGList(nullptr) {
  // Constructor only initializes members to safe defaults.
  // Actual initialization is done in InitResources() - this follows the
  // two-phase initialization pattern used by OpenGL renderers in this codebase.
  _deviceInfo.renderID = RENDERID_METAL;
  _deviceInfo.renderName = "Metal";
}

Render3DError MetalRender::InitResources() {
  // Two-phase initialization: This method does all the real setup.
  // Returns error code on failure (no exceptions).
  // The factory function (MetalRendererCreate) calls this after construction.
  
  // Get shared Metal resources
  _device = metal_getSharedDevice();
  if (_device == nil) {
    printf("Metal 3D: ERROR - No shared Metal device available.\n");
    return RENDER3DERROR_NOERR + 1;  // Generic error
  }
  [_device retain];

  _commandQueue = metal_getSharedCommandQueue();
  if (_commandQueue == nil) {
    printf("Metal 3D: ERROR - No shared command queue available.\n");
    return RENDER3DERROR_NOERR + 1;
  }
  [_commandQueue retain];
  
  printf("Metal 3D: Using shared device: %s\n", [[_device name] UTF8String]);

  Render3DError error;
  
  error = InitializePipelineState();
  if (error != RENDER3DERROR_NOERR) {
    printf("Metal 3D: ERROR - Failed to initialize pipeline state.\n");
    return error;
  }

  error = InitializeDepthStencilState();
  if (error != RENDER3DERROR_NOERR) {
    printf("Metal 3D: ERROR - Failed to initialize depth/stencil state.\n");
    return error;
  }

  error = InitializeSamplerState();
  if (error != RENDER3DERROR_NOERR) {
    printf("Metal 3D: ERROR - Failed to initialize sampler state.\n");
    return error;
  }

  error = InitializeRenderTargets(GPU_FRAMEBUFFER_NATIVE_WIDTH,
                                  GPU_FRAMEBUFFER_NATIVE_HEIGHT);
  if (error != RENDER3DERROR_NOERR) {
    printf("Metal 3D: ERROR - Failed to initialize render targets.\n");
    return error;
  }

  // Initialize the color output object
  _metalColorOut = new MetalRenderColorOut(
      _device, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT);
  _metalColorOut->SetRenderer(this);
  _colorOut = _metalColorOut;
  _metalColorOut->SetColorTexture(_colorTexture);

  error = InitializePostprocessPipelines();
  if (error != RENDER3DERROR_NOERR) {
    printf("Metal 3D: ERROR - Failed to initialize postprocessing pipelines.\n");
    return error;
  }

  error = CreateFullscreenQuad();
  if (error != RENDER3DERROR_NOERR) {
    printf("Metal 3D: ERROR - Failed to create fullscreen quad.\n");
    return error;
  }

  _renderStatesBuffer =
      [_device newBufferWithLength:sizeof(RenderStates)
                           options:MTLResourceStorageModeShared];
  if (_renderStatesBuffer == nil) {
    printf("Metal 3D: ERROR - Failed to allocate render states buffer.\n");
    return RENDER3DERROR_NOERR + 1;
  }

  return RENDER3DERROR_NOERR;
}

MetalRender::~MetalRender() {
  // Release objects that were explicitly retained in InitResources()
  [_commandQueue release];
  _commandQueue = nil;
  [_device release];
  _device = nil;
  
  // These are either autoreleased or nil (if InitResources failed partway)
  _commandBuffer = nil;
  _pipelineState = nil;
  _renderCommandEncoder = nil;
  _depthStencilStateOpaque = nil;
  _depthStencilStateDepthEqual = nil;
  _depthStencilStateTranslucent = nil;
  _depthStencilStateTranslucentDepthWrite = nil;
  _depthStencilStateShadowPass1 = nil;
  _depthStencilStateShadowPass2 = nil;
  _vertexBuffer[0] = nil;
  _vertexBuffer[1] = nil;
  _vertexBufferIndex = 0;
  _indexBuffer = nil;
  _colorTexture = nil;
  _depthTexture = nil;
  _renderPassDescriptor = nil;
  _samplerStateClampNearest = nil;
  _samplerStateClampLinear = nil;
  _samplerStateRepeatNearest = nil;
  _samplerStateRepeatLinear = nil;
  _samplerStateMirrorNearest = nil;
  _samplerStateMirrorLinear = nil;
  _dummyWhiteTexture = nil;

  // Clean up postprocessing resources
  _pipelineStateEdgeMark = nil;
  _pipelineStateFog = nil;
  _polygonIDTexture = nil;
  _fogAttributesTexture = nil;
  _fogDensityTexture = nil;
  _postprocessVertexBuffer = nil;
  _renderStatesBuffer = nil;

  // Clean up the color output object
  if (_metalColorOut != nullptr) {
    delete _metalColorOut;
    _metalColorOut = nullptr;
    _colorOut = nullptr;
  }
}

Render3DError
MetalRender::ApplyRenderingSettings(const GFX3D_State &renderState) {
  // Call the base class implementation to handle common settings
  Render3DError error = this->Render3D::ApplyRenderingSettings(renderState);
  if (error != RENDER3DERROR_NOERR) {
    return error;
  }

  // Cache Metal-specific rendering settings
  _enableAlphaBlending = (renderState.DISP3DCNT.EnableAlphaBlending != 0);
  _enableAntialiasing = (renderState.DISP3DCNT.EnableAntialiasing != 0);

  // Note: _enableTextureSampling, _enableEdgeMark, and _enableFog are already
  // set by the base class ApplyRenderingSettings() implementation

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::Reset() {
  // Call the base class implementation to reset the common settings
  Render3DError error = this->Render3D::Reset();
  if (error != RENDER3DERROR_NOERR) {
    return error;
  }

  // Reset Metal-specific rendering settings
  _enableAlphaBlending = false;
  _enableAntialiasing = false;

  texCache.Reset();
  if (_metalColorOut != nullptr) {
    _metalColorOut->Reset();
  }
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::RenderPowerOff() {
  // First, call the base class implementation to handle common power-off logic
  // This will set _isPoweredOn = false and call _colorOut->FillZero()
  Render3DError error = this->Render3D::RenderPowerOff();
  if (error != RENDER3DERROR_NOERR) {
    return error;
  }

  // Wait for any pending Metal GPU commands to complete before clearing
  if (_commandBuffer != nil) {
    [_commandBuffer commit];
    [_commandBuffer waitUntilCompleted];
    _commandBuffer = nil;
  }

  // Clear Metal-specific render state
  // When powered off, we need to ensure all render targets are in a clean state
  if (_renderPassDescriptor != nil && _colorTexture != nil) {
    @autoreleasepool {
      // Create a temporary command buffer to clear all render targets
      id<MTLCommandBuffer> clearCommandBuffer = [_commandQueue commandBuffer];
      if (clearCommandBuffer != nil) {
        // Set up clear operations for all attachments
        _renderPassDescriptor.colorAttachments[0].loadAction =
            MTLLoadActionClear;
        _renderPassDescriptor.colorAttachments[0].clearColor =
            MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

        _renderPassDescriptor.colorAttachments[1].loadAction =
            MTLLoadActionClear;
        _renderPassDescriptor.colorAttachments[1].clearColor =
            MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

        _renderPassDescriptor.colorAttachments[2].loadAction =
            MTLLoadActionClear;
        _renderPassDescriptor.colorAttachments[2].clearColor =
            MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

        if (_renderPassDescriptor.depthAttachment.texture != nil) {
          _renderPassDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
          _renderPassDescriptor.depthAttachment.clearDepth = 0.0;
        }

        if (_renderPassDescriptor.stencilAttachment.texture != nil) {
          _renderPassDescriptor.stencilAttachment.loadAction =
              MTLLoadActionClear;
          _renderPassDescriptor.stencilAttachment.clearStencil = 0;
        }

        // Create a render command encoder to execute the clear
        id<MTLRenderCommandEncoder> clearEncoder = [clearCommandBuffer
            renderCommandEncoderWithDescriptor:_renderPassDescriptor];
        if (clearEncoder != nil) {
          [clearEncoder endEncoding];
        }

        // Commit and wait for the clear operation to complete
        [clearCommandBuffer commit];
        [clearCommandBuffer waitUntilCompleted];
      }
    }
  }

  // Reset Metal-specific rendering flags
  _enableAlphaBlending = false;
  _enableAntialiasing = false;

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::Render(const GFX3D_State &renderState,
                                  const GFX3D_GeometryList &renderGList) {
  // The base class Render() orchestrates the rendering pipeline:
  // 1. Sets up clear color and attributes
  // 2. Calls BeginRender() to prepare Metal state
  // 3. Calls ClearFramebuffer() which calls ClearUsingImage or ClearUsingValues
  // 4. Calls RenderGeometry() to draw all polygons
  // 5. Calls PostprocessFramebuffer() for edge marking and fog
  // 6. Calls EndRender() to finalize and commit
  return this->Render3D::Render(renderState, renderGList);
}

Render3DError MetalRender::RenderFinish() {
  // Check if there's actually rendering that needs to finish
  // The base class manages this flag
  if (!this->_renderNeedsFinish) {
    return RENDER3DERROR_NOERR;
  }

  // Ensure all GPU commands have completed before proceeding
  // This is critical as RenderFinish() must block until all rendering is done
  if (_commandBuffer != nil) {
    // If there's still an active command buffer, commit it and wait
    [_commandBuffer commit];
    [_commandBuffer waitUntilCompleted];
    _commandBuffer = nil;
  }

  // Wait for the command queue to complete all pending work
  // This ensures any previously submitted command buffers are also finished
  if (_commandQueue != nil) {
    // Create a temporary command buffer to act as a fence
    @autoreleasepool {
      id<MTLCommandBuffer> fenceBuffer = [_commandQueue commandBuffer];
      if (fenceBuffer != nil) {
        [fenceBuffer commit];
        [fenceBuffer waitUntilCompleted];
      }
    }
  }

  // Call the base class implementation to set the flush flags
  // This will set _renderNeedsFlushMain and _renderNeedsFlush16 to true
  Render3DError error = this->Render3D::RenderFinish();
  if (error != RENDER3DERROR_NOERR) {
    return error;
  }

  // Evict any expired textures from the cache now that rendering is complete
  texCache.Evict();

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::RenderFlush(bool willFlushBuffer32,
                                       bool willFlushBuffer16) {
  // Call the base class to flush the color output, this will call the
  // _metalColorOut->BindRead32() and _metalColorOut->BindRead16() if the
  // willFlushBuffer32 or willFlushBuffer16 is true
  return this->Render3D::RenderFlush(willFlushBuffer32, willFlushBuffer16);
}

Render3DError MetalRender::VramReconfigureSignal() {
  // When VRAM is reconfigured, the texture cache must be invalidated
  // because the texture data may have changed or moved
  return this->Render3D::VramReconfigureSignal();
}

Render3DError MetalRender::SetFramebufferSize(size_t w, size_t h) {
  // Update render targets by recreating the _colorTexture
  Render3DError error = InitializeRenderTargets(w, h);
  if (error != RENDER3DERROR_NOERR) {
    return error;
  }

  // Update the colorout buffer size
  if (_metalColorOut != nullptr) {
    error = _metalColorOut->SetSize(w, h);
    if (error != RENDER3DERROR_NOERR) {
      return error;
    }

    _metalColorOut->SetColorTexture(_colorTexture);
  }

  return RENDER3DERROR_NOERR;
}

NDSColorFormat MetalRender::RequestColorFormat(NDSColorFormat colorFormat) {
  // Metal renderer supports BGR666_Rev natively through format conversion
  // Accept the requested format from the system
  this->_outputFormat = (colorFormat == NDSColorFormat_BGR555_Rev)
                            ? NDSColorFormat_BGR666_Rev
                            : colorFormat;
  
  // Propagate the color format to the color output object
  // This calls the base class SetColorFormat() which sets this->_format
  if (_colorOut != nullptr) {
    _colorOut->SetColorFormat(this->_outputFormat);
  }
  
  return this->_outputFormat;
}

NDSColorFormat MetalRender::GetColorFormat() const {
  // Return the current output color format
  return this->_outputFormat;
}

Render3DError MetalRender::FillZero() {
  if (_metalColorOut == nullptr) {
    return RENDER3DERROR_NOERR;
  }

  return _metalColorOut->FillZero();
}

Render3DError MetalRender::FillColor32(const Color4u8 *__restrict src,
                                       const bool isSrcNativeSize) {
  if (_metalColorOut == nullptr) {
    return RENDER3DERROR_NOERR;
  }

  return _metalColorOut->FillColor32(src, isSrcNativeSize);
}

ClipperMode MetalRender::GetPreferredPolygonClippingMode() const {
  return ClipperMode::ClipperMode_Full;
}

Render3DError MetalRender::InitializePipelineState() {
  @autoreleasepool {
    NSError *error = nil;
    
    // Track if we own the library (need to release it)
    bool shouldReleaseLibrary = false;
    
    // IMPORTANT: Load from shared library instead of creating new one
    id<MTLLibrary> defaultLibrary = nil;
    
    if (SharedMetalData != nil)
    {
        // Use the shared library from the display system (borrowed reference)
        defaultLibrary = [SharedMetalData defaultLibrary];
        shouldReleaseLibrary = false;
    }
    else
    {
        // Fallback: load our own library (owned reference, +1 retain count)
        defaultLibrary = [_device newDefaultLibrary];
        shouldReleaseLibrary = true;
    }

    if (defaultLibrary == nil) {
      printf("Metal: Failed to load Metal library\n");
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Get the vertex function from the library (+1 retain count)
    id<MTLFunction> vertexFunction =
        [defaultLibrary newFunctionWithName:@"vertexShader"];
    if (vertexFunction == nil) {
      printf("Metal 3D: ERROR - vertexShader not found in library!\n");
      if (shouldReleaseLibrary) {
        [defaultLibrary release];
      }
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Get the fragment function from the library (+1 retain count)
    id<MTLFunction> fragmentFunction =
        [defaultLibrary newFunctionWithName:@"fragmentShaderTextured"];
    if (fragmentFunction == nil) {
      printf("Metal 3D: ERROR - fragmentShaderTextured not found in library!\n");
      [vertexFunction release];
      if (shouldReleaseLibrary) {
        [defaultLibrary release];
      }
      return RENDER3DERROR_INVALID_BINDING;
    }
    // Create the vertex descriptor, describes the layout of the vertex data
    MTLVertexDescriptor *vertexDescriptor = [MTLVertexDescriptor new];

  // Position (X, Y, Z, W)
  vertexDescriptor.attributes[0].format = MTLVertexFormatFloat4;
  vertexDescriptor.attributes[0].bufferIndex = 0;
  vertexDescriptor.attributes[0].offset = 0;

  // Texture coordinates (S, T)
  vertexDescriptor.attributes[1].format = MTLVertexFormatFloat2;
  vertexDescriptor.attributes[1].offset = 16; // sizeof(float) * 4 = 16
  vertexDescriptor.attributes[1].bufferIndex = 0;

  // Vertex color (R, G, B, A)
  vertexDescriptor.attributes[2].format = MTLVertexFormatUChar4;
  vertexDescriptor.attributes[2].offset =
      24; // sizeof(float) * 4 + sizeof(float) * 2 = 24
  vertexDescriptor.attributes[2].bufferIndex = 0;

  // 4096.0/w for color perspective correction
  vertexDescriptor.attributes[3].format = MTLVertexFormatFloat;
  vertexDescriptor.attributes[3].offset =
      28; // sizeof(float) * 4 + sizeof(float) * 2 + sizeof(u8) * 4 = 28
  vertexDescriptor.attributes[3].bufferIndex = 0;

  // 256.0/w for texture coordinate perspective correction
  vertexDescriptor.attributes[4].format = MTLVertexFormatFloat;
  vertexDescriptor.attributes[4].offset =
      32; // sizeof(float) * 4 + sizeof(float) * 2 + sizeof(u8) * 4 + sizeof(float) = 32
  vertexDescriptor.attributes[4].bufferIndex = 0;

  // Layout the vertex data
  // Use MetalVertex stride, not NDSVertex, because we convert the data
  vertexDescriptor.layouts[0].stride = sizeof(MetalVertex);
  vertexDescriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
  vertexDescriptor.layouts[0].stepRate = 1;

  // Create the pipeline descriptor
  MTLRenderPipelineDescriptor *pipelineDesc = [MTLRenderPipelineDescriptor new];
  pipelineDesc.label = @"DeSmuMe 3D Render Pipeline";
  pipelineDesc.vertexFunction = vertexFunction;
  pipelineDesc.fragmentFunction = fragmentFunction;
  pipelineDesc.vertexDescriptor = vertexDescriptor;

  // Configure color attachments
  pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;

  // enable alpha blending for translucent polygons
  pipelineDesc.colorAttachments[0].blendingEnabled = YES;
  pipelineDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
  pipelineDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
  pipelineDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
  pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor =
      MTLBlendFactorSourceAlpha;
  pipelineDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
  pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;

  // Configure the depth/stencil state
  // Use combined depth-stencil format to match the framebuffer texture
  pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
  pipelineDesc.stencilAttachmentPixelFormat =
      MTLPixelFormatDepth32Float_Stencil8;

  // Configure additional color attachments for postprocessing
  pipelineDesc.colorAttachments[1].pixelFormat =
      MTLPixelFormatR8Uint; // Polygon ID
  pipelineDesc.colorAttachments[2].pixelFormat =
      MTLPixelFormatR8Unorm; // Fog enable flag

    // create the pipeline state
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                             error:&error];
    if (_pipelineState == nil) {
      if (error != nil) {
        NSLog(@"Error creating pipeline state: %@", error.localizedDescription);
      }
      [vertexFunction release];
      [fragmentFunction release];
      if (shouldReleaseLibrary) {
        [defaultLibrary release];
      }
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Release temporary objects (descriptors auto-released by pool)
    [vertexFunction release];
    [fragmentFunction release];
    if (shouldReleaseLibrary) {
      [defaultLibrary release];
    }

    return RENDER3DERROR_NOERR;
  } // @autoreleasepool
}

Render3DError MetalRender::InitializeDepthStencilState() {
  @autoreleasepool {
    // The Nintendo DS needs different depth/stencil configurations for:
    // - Opaque polygons (depth write ON, depth test LESS)
    // - Translucent polygons (depth write OFF, depth test LESS)
    // - Depth-equal polygons (depth test EQUAL)
    // - Shadow polygons (special stencil operations)

    // create depth stencil descriptor for opaque polygons
    MTLDepthStencilDescriptor *opaqueDesc = [MTLDepthStencilDescriptor new];
  opaqueDesc.depthCompareFunction = MTLCompareFunctionLess;
  opaqueDesc.depthWriteEnabled = YES;

  // Stencil writes the polygon ID to the stencil buffer for each drawn
  // fragment for edge marking and AA.
  MTLStencilDescriptor *opaqueStencil = [MTLStencilDescriptor new];
  opaqueStencil.stencilCompareFunction = MTLCompareFunctionAlways;
  opaqueStencil.stencilFailureOperation = MTLStencilOperationKeep;
  opaqueStencil.depthFailureOperation = MTLStencilOperationKeep;
  opaqueStencil.depthStencilPassOperation = MTLStencilOperationReplace;
  opaqueStencil.readMask = 0x3F;
  opaqueStencil.writeMask = 0x3F;

  opaqueDesc.frontFaceStencil = opaqueStencil;
  opaqueDesc.backFaceStencil = opaqueStencil;

  _depthStencilStateOpaque =
      [_device newDepthStencilStateWithDescriptor:opaqueDesc];
  if (_depthStencilStateOpaque == nil) {
    NSLog(@"Error: Failed to create opaque depth/stencil state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // opague poligons with depth_equal_test enabled

  MTLDepthStencilDescriptor *depthEqualDesc = [MTLDepthStencilDescriptor new];
  depthEqualDesc.depthCompareFunction = MTLCompareFunctionEqual;
  depthEqualDesc.depthWriteEnabled = YES;
  depthEqualDesc.frontFaceStencil = opaqueStencil;
  depthEqualDesc.backFaceStencil = opaqueStencil;

  _depthStencilStateDepthEqual =
      [_device newDepthStencilStateWithDescriptor:depthEqualDesc];
  if (_depthStencilStateDepthEqual == nil) {
    NSLog(@"Error: Failed to create depth equal depth/stencil state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  MTLStencilDescriptor *translucentStencil = [MTLStencilDescriptor new];
  translucentStencil.stencilCompareFunction = MTLCompareFunctionAlways;
  translucentStencil.stencilFailureOperation = MTLStencilOperationKeep;
  translucentStencil.depthFailureOperation = MTLStencilOperationKeep;
  translucentStencil.depthStencilPassOperation = MTLStencilOperationReplace;
  translucentStencil.readMask = 0x7F;
  translucentStencil.writeMask = 0x7F;

  // translucent polygons with alpha < 31 AND translucent depth write disabled
  MTLDepthStencilDescriptor *translucentDesc = [MTLDepthStencilDescriptor new];
  translucentDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
  translucentDesc.depthWriteEnabled = NO;
  translucentDesc.frontFaceStencil = translucentStencil;
  translucentDesc.backFaceStencil = translucentStencil;

  _depthStencilStateTranslucent =
      [_device newDepthStencilStateWithDescriptor:translucentDesc];
  if (_depthStencilStateTranslucent == nil) {
    NSLog(@"Error: Failed to create translucent depth/stencil state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // translucent polygons with alpha < 31 AND translucent depth write enabled
  MTLDepthStencilDescriptor *translucentDepthWriteDesc =
      [MTLDepthStencilDescriptor new];
  translucentDepthWriteDesc.depthCompareFunction = MTLCompareFunctionLessEqual;
  translucentDepthWriteDesc.depthWriteEnabled = YES;
  translucentDepthWriteDesc.frontFaceStencil = translucentStencil;
  translucentDepthWriteDesc.backFaceStencil = translucentStencil;

  _depthStencilStateTranslucentDepthWrite =
      [_device newDepthStencilStateWithDescriptor:translucentDepthWriteDesc];
  if (_depthStencilStateTranslucentDepthWrite == nil) {
    NSLog(@"Error: Failed to create translucent depth write depth/stencil "
          @"state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // shadow polygons with depth test equal and stencil test not equal
  MTLStencilDescriptor *shadowPass1Stencil =
      [[MTLStencilDescriptor alloc] init];
  shadowPass1Stencil.stencilCompareFunction = MTLCompareFunctionAlways;
  shadowPass1Stencil.stencilFailureOperation = MTLStencilOperationKeep;
  shadowPass1Stencil.depthFailureOperation =
      MTLStencilOperationReplace; // Write on depth FAIL
  shadowPass1Stencil.depthStencilPassOperation = MTLStencilOperationKeep;
  shadowPass1Stencil.readMask = 0x80;  // Only bit 7 (shadow mask)
  shadowPass1Stencil.writeMask = 0x80; // Only bit 7 (shadow mask)

  MTLDepthStencilDescriptor *shadowPass1Desc = [MTLDepthStencilDescriptor new];
  shadowPass1Desc.depthCompareFunction = MTLCompareFunctionLess;
  shadowPass1Desc.depthWriteEnabled = NO;
  shadowPass1Desc.frontFaceStencil = shadowPass1Stencil;
  shadowPass1Desc.backFaceStencil = shadowPass1Stencil;

  _depthStencilStateShadowPass1 =
      [_device newDepthStencilStateWithDescriptor:shadowPass1Desc];
  if (_depthStencilStateShadowPass1 == nil) {
    NSLog(@"Error: Failed to create shadow pass 1 depth/stencil state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // shadow polygons, pass 2. When PolygonID != 0, compare stencil buffer bits
  // 0-5 (0x3F) with this polygon's ID. If this stencil test fails, remove the
  // fragment from the shadow volume mask by clearing bit 7.
  MTLStencilDescriptor *shadowPass2Stencil =
      [[MTLStencilDescriptor alloc] init];
  shadowPass2Stencil.stencilCompareFunction =
      MTLCompareFunctionNotEqual; // Check poly ID
  shadowPass2Stencil.stencilFailureOperation =
      MTLStencilOperationZero; // Clear bit 7 on fail
  shadowPass2Stencil.depthFailureOperation = MTLStencilOperationKeep;
  shadowPass2Stencil.depthStencilPassOperation = MTLStencilOperationKeep;
  shadowPass2Stencil.readMask = 0x3F;  // Read polygon ID bits (0-5)
  shadowPass2Stencil.writeMask = 0x80; // Write shadow mask bit (7)

  MTLDepthStencilDescriptor *shadowPass2Desc = [MTLDepthStencilDescriptor new];
  shadowPass2Desc.depthCompareFunction = MTLCompareFunctionLess;
  shadowPass2Desc.depthWriteEnabled = NO;
  shadowPass2Desc.frontFaceStencil = shadowPass2Stencil;
  shadowPass2Desc.backFaceStencil = shadowPass2Stencil;

    _depthStencilStateShadowPass2 =
        [_device newDepthStencilStateWithDescriptor:shadowPass2Desc];
    if (_depthStencilStateShadowPass2 == nil) {
      NSLog(@"Error: Failed to create shadow pass 2 depth/stencil state");
      return RENDER3DERROR_INVALID_BINDING;
    }

    return RENDER3DERROR_NOERR;
  } // @autoreleasepool
}

Render3DError MetalRender::BeginRender(const GFX3D_State &renderState,
                                       const GFX3D_GeometryList &renderGList) {
  // Store the geometry list
  _clippedPolyCount = renderGList.clippedPolyCount;
  _clippedPolyOpaqueCount = renderGList.clippedPolyOpaqueCount;
  _clippedPolyList = (CPoly *)renderGList.clippedPolyList;
  _rawPolyList = (POLY *)renderGList.rawPolyList;
  _renderGList = &renderGList; // Store for debugging

  // Store the render state
  _enableAlphaBlending =
      (renderState.DISP3DCNT.EnableAlphaBlending) ? true : false;

  // If there isn't anything to render, return early
  if (_clippedPolyCount == 0) {
    // Reset the bound color output index to prevent stale state
    this->_lastBoundColorOut = RENDER3D_RESOURCE_INDEX_NONE;
    return RENDER3DERROR_NOERR;
  }

  // Calculate vertex and index counts for CLIPPED vertices
  // We use cPoly.vtx[] which contains viewport-transformed screen-space vertices (16.16 fixed-point pixels)
  size_t totalVertexCount = 0;
  size_t totalIndexCount = 0;
  for (size_t i = 0; i < _clippedPolyCount; i++) {
    const CPoly &cPoly = _clippedPolyList[i];
    const POLY &rawPoly = _rawPolyList[cPoly.index];
    // CRITICAL: Use cPoly.type (clipped vertex count) not rawPoly.type (original count)
    // After clipping, the polygon may have a different number of vertices
    const size_t polyType = cPoly.type;
    
    totalVertexCount += polyType;
    
    // Calculate index count based on how we tessellate the polygon
    if (!GFX3D_IsPolyWireframe(rawPoly) && polyType == 4 &&
        (rawPoly.vtxFormat == GFX3D_QUADS ||
         rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
      // Quads with exactly 4 vertices -> 2 triangles = 6 indices
      totalIndexCount += 6;
    } else if (polyType >= 3) {
      // Triangle fan tessellation: (n-2) triangles = (n-2) * 3 indices
      totalIndexCount += (polyType - 2) * 3;
    }
  }
  
  size_t vertexBufferSize = totalVertexCount * sizeof(MetalVertex);
  size_t indexBufferSize = totalIndexCount * sizeof(uint16_t);

  // Create or update buffers
  _vertexBufferIndex = 1 - _vertexBufferIndex;
  if (_vertexBuffer[_vertexBufferIndex] == nil || vertexBufferSize > [_vertexBuffer[_vertexBufferIndex] length]) {
    _vertexBuffer[_vertexBufferIndex] = [_device newBufferWithLength:vertexBufferSize
                                         options:MTLResourceStorageModeShared];
    if (_vertexBuffer[_vertexBufferIndex] == nil) {
      return RENDER3DERROR_INVALID_BUFFER;
    }
  }

  // Convert CLIPPED vertices from screen-space pixels to NDC
  // cPoly.vtx[] contains 16.16 fixed-point screen pixels after viewport transformation
  MetalVertex *metalVertices = (MetalVertex *)[_vertexBuffer[_vertexBufferIndex] contents];
  size_t vertexOffset = 0;
  
  for (size_t i = 0; i < _clippedPolyCount; i++) {
    const CPoly &cPoly = _clippedPolyList[i];
    const POLY &rawPoly = _rawPolyList[cPoly.index];
    
    // CRITICAL: Use cPoly.type (clipped vertex count) not rawPoly.type (original count)
    const size_t polyType = cPoly.type;
    
    // Note: We no longer normalize texture coordinates at the vertex level
    // since we're keeping them in raw s32 scale to match the soft rasterizer
    const NDSTextureFormat packFormat = (NDSTextureFormat)rawPoly.texParam.PackedFormat;
    const bool hasTexture = (packFormat != TEXMODE_NONE);
    
    for (size_t j = 0; j < polyType; j++) {
      const NDSVertex &vtx = cPoly.vtx[j];

      // Convert from 16.16 fixed-point screen pixels to floating-point pixels
      float pixel_x = (float)vtx.position.x / 65536.0f;
      float pixel_y = (float)vtx.position.y / 65536.0f;
      float pixel_z = (float)vtx.position.z / 2147483648.0f; // 0.31 fixed-point to [0, 1]
      
      // Convert from screen pixels to NDC [-1, 1]
      // NDS screen is 256x192, with origin at top-left
      float ndc_x = (pixel_x / 256.0f) * 2.0f - 1.0f;
      float ndc_y = 1.0f - (pixel_y / 192.0f) * 2.0f; // Flip Y
      
      // Store as NDC with w=1 (positions are already perspective-divided)
      metalVertices[vertexOffset].position[0] = ndc_x;
      metalVertices[vertexOffset].position[1] = ndc_y;
      metalVertices[vertexOffset].position[2] = pixel_z;
      metalVertices[vertexOffset].position[3] = 1.0f;
      
      // Get the original clip-space w value (preserved by NDS after perspective division)
      // w is in 20.12 fixed-point format
      float raw_w = (float)vtx.position.w;
      float w_normalized = raw_w / 4096.0f;
      float invW = (raw_w > 0.0f) ? (4096.0f / raw_w) : 1.0f;  // 4096.0/raw_w for colors
      
      // invWTC = 256.0 / w_normalized for texture coordinate perspective correction
      // This gives larger values (~0.7) for better floating-point precision
      float invWTC = (w_normalized > 0.0f) ? (256.0f / w_normalized) : 1.0f;
      
      // Store texture coordinates as TEXELS (divide by 16)
      // This is the original approach that keeps values in a reasonable range
      float texCoordS = (float)vtx.texCoord.s / 16.0f;  // Convert to texel coordinates
      float texCoordT = (float)vtx.texCoord.t / 16.0f;  // Convert to texel coordinates
      
      if (hasTexture) {
        // Store texel coordinates (already divided by 16)
        // Perspective correction happens in vertex shader: texCoord * invWTC
        // Fragment shader will recover texel coords directly by dividing by invWTC
        metalVertices[vertexOffset].texCoord[0] = texCoordS;
        metalVertices[vertexOffset].texCoord[1] = texCoordT;
      } else {
        // For untextured polygons, use (0, 0) to sample from dummy white texture
        metalVertices[vertexOffset].texCoord[0] = 0.0f;
        metalVertices[vertexOffset].texCoord[1] = 0.0f;
      }

      // Copy vertex color
      metalVertices[vertexOffset].color[0] = vtx.color.r;
      metalVertices[vertexOffset].color[1] = vtx.color.g;
      metalVertices[vertexOffset].color[2] = vtx.color.b;
      metalVertices[vertexOffset].color[3] = vtx.color.a;
      
      // Store the calculated invW values
      metalVertices[vertexOffset].invW = invW;      // 4096.0/w for colors
      metalVertices[vertexOffset].invWTC = invWTC;  // 256.0/w for texture coordinates
      
      vertexOffset++;
    }
  }

  // Create index buffer
  if (_indexBuffer == nil || indexBufferSize > [_indexBuffer length]) {
    _indexBuffer = [_device newBufferWithLength:indexBufferSize
                                        options:MTLResourceStorageModeShared];
    if (_indexBuffer == nil) {
      return RENDER3DERROR_INVALID_BUFFER;
    }
  }

  // Build index buffer for clipped vertices
  u16 *indexPtr = (u16 *)[_indexBuffer contents];
  size_t indexCount = 0;
  vertexOffset = 0;

  for (size_t i = 0; i < _clippedPolyCount; i++) {
    const CPoly &cPoly = _clippedPolyList[i];
    const POLY &rawPoly = _rawPolyList[cPoly.index];
    // CRITICAL: Use cPoly.type (clipped vertex count) not rawPoly.type (original count)
    const size_t polyType = cPoly.type;

    // Convert quads to triangles
    // Note: After clipping, a quad might still have 4 vertices
    if (!GFX3D_IsPolyWireframe(rawPoly) && polyType == 4 &&
        (rawPoly.vtxFormat == GFX3D_QUADS ||
         rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
      indexPtr[indexCount++] = vertexOffset + 0;
      indexPtr[indexCount++] = vertexOffset + 1;
      indexPtr[indexCount++] = vertexOffset + 2;
      indexPtr[indexCount++] = vertexOffset + 0;
      indexPtr[indexCount++] = vertexOffset + 2;
      indexPtr[indexCount++] = vertexOffset + 3;
    } else {
      // For triangles, or polygons with more/less vertices after clipping
      // Use triangle fan tessellation for polygons with 4+ vertices
      if (polyType >= 3) {
        for (size_t j = 1; j < polyType - 1; j++) {
          indexPtr[indexCount++] = vertexOffset + 0;
          indexPtr[indexCount++] = vertexOffset + j;
          indexPtr[indexCount++] = vertexOffset + j + 1;
        }
      }
    }
    
    vertexOffset += polyType;
    
    // Store texture
    _textureList[i] =
        this->GetLoadedTextureFromPolygon(rawPoly, _enableTextureSampling);
  }

  // Create the command buffer for this frame
  _commandBuffer = [_commandQueue commandBuffer];
  if (_commandBuffer == nil) {
    return RENDER3DERROR_INVALID_BUFFER;
  }
  // Set the label for debugging
  _commandBuffer.label = @"DeSmuMe 3D Render Command Buffer";

  // Bind the color output for this render
  this->_lastBoundColorOut = _metalColorOut->BindRenderer();

  // Update the color out with our current render target
  if (_metalColorOut != nullptr && _colorTexture != nil) {
    _metalColorOut->SetColorTexture(_colorTexture);
  }

  // capture rendering states for postprocessing
  _enableFog =
      renderState.DISP3DCNT.EnableFog && (renderGList.clippedPolyCount > 0);
  _enableEdgeMark = renderState.DISP3DCNT.EnableEdgeMarking &&
                    (renderGList.clippedPolyCount > 0);

  if (_enableFog || _enableEdgeMark) {
    // Update the render states buffer with the current render states
    RenderStates *states = (RenderStates *)[_renderStatesBuffer contents];

    // Initialize all fields (using uint32_t for bools to match Metal shader
    // alignment)
    states->enableAntialiasing = _enableAntialiasing ? 1 : 0;
    states->enableFogAlphaOnly =
        0; // TODO: Set based on actual fog alpha-only setting
    states->clearPolyID = this->_clearAttributes.opaquePolyID;
    states->clearDepth = (float)this->_clearAttributes.depth / 16777215.0f;
    states->alphaTestRef =
        0.0f; // Alpha test reference (not currently used by NDS)

    // Fog parameters (keep as integer values, not normalized)
    // The shader will use these directly in the fog density calculation
    states->fogOffset = (float)(renderState.fogOffset & 0x7FFF);
    states->fogStep = (float)(0x0400 >> renderState.fogShift);

    // Fog color is a 5-bit value, so we need to divide by 31
    states->fogColor[0] = (float)(renderState.fogColor & 0x1F) / 31.0f;
    states->fogColor[1] = (float)((renderState.fogColor >> 5) & 0x1F) / 31.0f;
    states->fogColor[2] = (float)((renderState.fogColor >> 10) & 0x1F) / 31.0f;
    states->fogColor[3] = (float)((renderState.fogColor >> 15) & 0x1F) / 31.0f;

    // Copy fog color to cached render states for use in
    // PostprocessFramebuffer
    _currentRenderStates.fogColor[0] = states->fogColor[0];
    _currentRenderStates.fogColor[1] = states->fogColor[1];
    _currentRenderStates.fogColor[2] = states->fogColor[2];
    _currentRenderStates.fogColor[3] = states->fogColor[3];

    // Edge colors (8 colors, each a 5-bit RGB value)
    for (size_t i = 0; i < 8; i++) {
      const u16 edgeColor = renderState.edgeMarkColorTable[i];
      states->edgeColor[i][0] = (float)(edgeColor & 0x1F) / 31.0f;
      states->edgeColor[i][1] = (float)((edgeColor >> 5) & 0x1F) / 31.0f;
      states->edgeColor[i][2] = (float)((edgeColor >> 10) & 0x1F) / 31.0f;
      states->edgeColor[i][3] = 1.0f; // full alpha
    }

    // Upload fog density table to 1D texture
    if (_enableFog) {
      // Create 1D texture for fog denisty if not already created
      if (_fogDensityTexture == nil) {
        MTLTextureDescriptor *desc = [MTLTextureDescriptor new];
        desc.textureType = MTLTextureType1D;
        desc.pixelFormat = MTLPixelFormatR8Unorm;
        desc.width = 32;
        desc.height = 1;
        desc.mipmapLevelCount = 1;
        desc.usage = MTLTextureUsageShaderRead;
        _fogDensityTexture = [_device newTextureWithDescriptor:desc];
        if (_fogDensityTexture == nil) {
          NSLog(@"Error: Failed to create fog density texture");
          return RENDER3DERROR_INVALID_BUFFER;
        }
      }

      // Convert fog density table to normalized byte values
      u8 fogDensityBytes[32];
      for (int i = 0; i < 32; i++) {
        // NDS stores fog density as 7-bit values, 127 = full fog
        u8 density = renderState.fogDensityTable[i];
        // Treat 127 as 128 (full fog)
        fogDensityBytes[i] = (density >= 127) ? 255 : (density * 2);
      }

      // Upload to texture
      [_fogDensityTexture replaceRegion:MTLRegionMake1D(0, 32)
                            mipmapLevel:0
                              withBytes:fogDensityBytes
                            bytesPerRow:32];
    }
  }

  return RENDER3DERROR_NOERR;
}

MTLDepthStencilStatePtr
MetalRender::GetDepthStencilStateForPolygon(const POLY &thePoly,
                                            bool treatAsTranslucent) {
  // shadow polgons, mode == POLYGON_MODE_SHADOW
  if (thePoly.attribute.Mode == POLYGON_MODE_SHADOW) {
    if (thePoly.attribute.PolygonID == 0) {
      return _depthStencilStateShadowPass1;
    }
    return _depthStencilStateShadowPass2;
  }

  // translucent polygons (alpha < 31)
  if (treatAsTranslucent) {
    if (thePoly.attribute.TranslucentDepthWrite_Enable) {
      return _depthStencilStateTranslucentDepthWrite;
    }
    return _depthStencilStateTranslucent;
  }

  // depth equal test polygons, from less to equal
  // this is used for decals, coplanar polygons, etc.
  if (thePoly.attribute.DepthEqualTest_Enable) {
    return _depthStencilStateDepthEqual;
  }

  // normal opaque polygons
  return _depthStencilStateOpaque;
}

unsigned long MetalRender::GetCullModeForPolygon(const POLY &thePoly) const {
  // The Nintendo DS uses a 2-bit surface culling mode in bits 6-7:
  // SurfaceCullingMode values:
  //   0 = Cull front and back (polygon invisible, should never reach renderer)
  //   1 = Cull front (only back surface visible)
  //   2 = Cull back (only front surface visible)
  //   3 = No culling (both surfaces visible)
  //
  // Metal's MTLCullMode:
  //   MTLCullModeNone  = 0 (no culling)
  //   MTLCullModeFront = 1 (cull front-facing primitives)
  //   MTLCullModeBack  = 2 (cull back-facing primitives)

  const u8 cullingMode = thePoly.attribute.SurfaceCullingMode;

  switch (cullingMode) {
  case 0:
    // Cull front and back - this polygon should have been filtered out earlier,
    // but if it reaches here, cull everything (use front culling as fallback)
    return (unsigned long)MTLCullModeFront;

  case 1:
    // Cull front surface (BackSurface=0, FrontSurface=1 means hide front)
    return (unsigned long)MTLCullModeFront;

  case 2:
    // Cull back surface (BackSurface=1, FrontSurface=0 means hide back)
    return (unsigned long)MTLCullModeBack;

  case 3:
  default:
    // No culling (both surfaces visible)
    return (unsigned long)MTLCullModeNone;
  }
}

unsigned long MetalRender::GetWindingOrderForPolygon(const CPoly &cPoly) const {
  // The DS determines polygon facing based on the winding order of vertices in
  // screen space. The CPoly structure contains a pre-calculated
  // isPolyBackFacing flag that tells us whether this polygon is back-facing
  // according to DS hardware.
  //
  // Metal's winding order:
  // - MTLWindingClockwise: Front-facing if vertices are clockwise
  // - MTLWindingCounterClockwise: Front-facing if vertices are
  // counter-clockwise
  //
  // The DS considers a polygon front-facing if its vertices are ordered
  // counter-clockwise in screen space. If isPolyBackFacing is true, the
  // vertices are clockwise (back-facing). Since we want Metal to match the DS's
  // front/back determination, we set the winding order to counter-clockwise.

  // Always use counter-clockwise as front-facing to match DS hardware behavior
  return (unsigned long)MTLWindingCounterClockwise;
}

Render3DError MetalRender::RenderGeometry() {

  struct PolygonAttributes {
    uint polygonID;
    bool enableFog;
    float polyAlpha;
    float texScaleS; // 1.0 / width for normalization
    float texScaleT; // 1.0 / height for normalization
    uint wrapModeS;  // 0=clamp, 1=repeat, 2=mirror
    uint wrapModeT;  // 0=clamp, 1=repeat, 2=mirror
  };

  // Exit if there are no polygons to render
  if (_clippedPolyCount == 0) {
    return RENDER3DERROR_NOERR;
  }

  // create a render command encoder
  _renderCommandEncoder =
      [_commandBuffer renderCommandEncoderWithDescriptor:_renderPassDescriptor];
  if (_renderCommandEncoder == nil) {
    return RENDER3DERROR_INVALID_BUFFER;
  }
  _renderCommandEncoder.label = @"DeSmuMe 3D Render Command Encoder";

  // bind the pipeline state (shaders, blend state, etc.)
  [_renderCommandEncoder setRenderPipelineState:_pipelineState];

  // Set the front-face winding order for all polygons
  // The Nintendo DS uses counter-clockwise winding for front-facing polygons
  [_renderCommandEncoder setFrontFacingWinding:MTLWindingCounterClockwise];

  // set the viewport (doing full screen, for now)
  MTLViewport viewport;
  viewport.originX = 0.0;
  viewport.originY = 0.0;
  viewport.width = GPU_FRAMEBUFFER_NATIVE_WIDTH;
  viewport.height = GPU_FRAMEBUFFER_NATIVE_HEIGHT;
  viewport.znear = 0.0;
  viewport.zfar = 1.0;
  [_renderCommandEncoder setViewport:viewport];

  // bind the vertex buffer and index buffer
  [_renderCommandEncoder setVertexBuffer:_vertexBuffer[_vertexBufferIndex] offset:0 atIndex:0];

  // The emulator separates polygons into two groups:
  // 1. Opaque polygons (0 to _clippedPolyOpaqueCount-1)
  // 2. Translucent polygons (_clippedPolyOpaqueCount to
  // _clippedPolyCount-1)

  size_t indexOffset = 0; // Track where we are in the index buffer

  // draw the opaque polygons first
  if (_clippedPolyOpaqueCount > 0) {
    for (size_t i = 0; i < _clippedPolyOpaqueCount; i++) {
      const CPoly &cPoly = _clippedPolyList[i];
      const POLY &rawPoly = _rawPolyList[cPoly.index];

      const bool treatAsTranslucent = (rawPoly.attribute.Alpha < 31);

      // select the correct depth/stencil state
      MTLDepthStencilStatePtr depthStencilState =
          GetDepthStencilStateForPolygon(rawPoly, treatAsTranslucent);
      [_renderCommandEncoder setDepthStencilState:depthStencilState];

      // Set the stencil reference value (Polygon ID for stencil operations)
      // Polygon ID is stored in bits 24-29 of the polygon attributes
      [_renderCommandEncoder
          setStencilReferenceValue:rawPoly.attribute.PolygonID];

      // Set the cull mode for this polygon based on its surface culling
      // attributes This enables proper front/back face culling as specified by
      // the DS hardware
      MTLCullMode cullMode = (MTLCullMode)GetCullModeForPolygon(rawPoly);
      [_renderCommandEncoder setCullMode:cullMode];

      // set up the texture for this polygon
      this->SetupTexture(rawPoly, i);

      // set up the viewport for this polygon
      this->SetupViewport(rawPoly.viewport);

      // determine the number of indices to draw for this polygon
      // CRITICAL: Use cPoly.type (clipped vertex count) not rawPoly.type
      const size_t polyType = cPoly.type;
      size_t indexCount = 0;

      // Calculate index count based on how we built the index buffer
      if (!GFX3D_IsPolyWireframe(rawPoly) && polyType == 4 &&
          (rawPoly.vtxFormat == GFX3D_QUADS ||
           rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
        // Quads with 4 vertices after clipping -> 2 triangles = 6 indices
        indexCount = 6;
      } else if (polyType >= 3) {
        // Triangle fan tessellation: (polyType - 2) triangles = (polyType - 2) * 3 indices
        indexCount = (polyType - 2) * 3;
      }

      PolygonAttributes polyAttr;
      polyAttr.polygonID = rawPoly.attribute.PolygonID;
      polyAttr.enableFog = rawPoly.attribute.Fog_Enable ? 1 : 0;
      polyAttr.polyAlpha = (float)rawPoly.attribute.Alpha / 31.0f; // Convert 5-bit alpha (0-31) to 0.0-1.0
      
      // Get texture size for matching soft rasterizer formula
      const NDSTextureFormat packFormat = (NDSTextureFormat)rawPoly.texParam.PackedFormat;
      const bool hasTexture = (packFormat != TEXMODE_NONE);
      if (hasTexture) {
        const u32 texWidth = 8 << rawPoly.texParam.SizeShiftS;
        const u32 texHeight = 8 << rawPoly.texParam.SizeShiftT;
        if (texWidth > 0 && texHeight > 0) {
          // Store 1.0/width and 1.0/height for normalization
          polyAttr.texScaleS = 1.0f / (float)texWidth;
          polyAttr.texScaleT = 1.0f / (float)texHeight;
          
          // Determine wrap modes independently for S and T axes
          // 0=clamp, 1=repeat, 2=mirror
          const bool repeatS = rawPoly.texParam.RepeatS_Enable;
          const bool repeatT = rawPoly.texParam.RepeatT_Enable;
          const bool mirrorS = rawPoly.texParam.MirroredRepeatS_Enable;
          const bool mirrorT = rawPoly.texParam.MirroredRepeatT_Enable;
          
          if (repeatS && mirrorS) {
            polyAttr.wrapModeS = 2; // Mirror
          } else if (repeatS) {
            polyAttr.wrapModeS = 1; // Repeat
          } else {
            polyAttr.wrapModeS = 0; // Clamp
          }
          
          if (repeatT && mirrorT) {
            polyAttr.wrapModeT = 2; // Mirror
          } else if (repeatT) {
            polyAttr.wrapModeT = 1; // Repeat
          } else {
            polyAttr.wrapModeT = 0; // Clamp
          }
        } else {
          polyAttr.texScaleS = 1.0f;
          polyAttr.texScaleT = 1.0f;
          polyAttr.wrapModeS = 0;
          polyAttr.wrapModeT = 0;
        }
      } else {
        polyAttr.texScaleS = 1.0f; // Not used for untextured polygons
        polyAttr.texScaleT = 1.0f;
        polyAttr.wrapModeS = 0;
        polyAttr.wrapModeT = 0;
      }

      [_renderCommandEncoder setFragmentBytes:&polyAttr
                                       length:sizeof(PolygonAttributes)
                                      atIndex:0];

      // draw the polygon
      [_renderCommandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:indexCount
                                         indexType:MTLIndexTypeUInt16
                                       indexBuffer:_indexBuffer
                                 indexBufferOffset:indexOffset * sizeof(u16)];

      // increment the index offset
      indexOffset += indexCount;
    }
  }

  // draw the translucent polygons
  if (_clippedPolyOpaqueCount < _clippedPolyCount) {
    
    for (size_t i = _clippedPolyOpaqueCount; i < _clippedPolyCount; i++) {
      const CPoly &cPoly = _clippedPolyList[i];
      const POLY &rawPoly = _rawPolyList[cPoly.index];

      const bool treatAsTranslucent = (rawPoly.attribute.Alpha < 31);

      // select the correct depth/stencil state
      MTLDepthStencilStatePtr depthStencilState =
          GetDepthStencilStateForPolygon(rawPoly, treatAsTranslucent);
      [_renderCommandEncoder setDepthStencilState:depthStencilState];

      // Set the stencil reference value (Polygon ID for stencil operations)
      // Polygon ID is stored in bits 24-29 of the polygon attributes
      u8 stencilReferenceValue = rawPoly.attribute.PolygonID | 0x40;
      [_renderCommandEncoder setStencilReferenceValue:stencilReferenceValue];

      // Set the cull mode for this polygon based on its surface culling
      // attributes This enables proper front/back face culling as specified by
      // the DS hardware
      MTLCullMode cullMode = (MTLCullMode)GetCullModeForPolygon(rawPoly);
      [_renderCommandEncoder setCullMode:cullMode];

      // set up the texture for this polygon
      this->SetupTexture(rawPoly, i);

      // set up the viewport for this polygon
      this->SetupViewport(rawPoly.viewport);

      // determine the number of indices to draw for this polygon
      // CRITICAL: Use cPoly.type (clipped vertex count) not rawPoly.type
      const size_t polyType = cPoly.type;
      size_t indexCount = 0;

      // Calculate index count based on how we built the index buffer
      if (!GFX3D_IsPolyWireframe(rawPoly) && polyType == 4 &&
          (rawPoly.vtxFormat == GFX3D_QUADS ||
           rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
        // Quads with 4 vertices after clipping -> 2 triangles = 6 indices
        indexCount = 6;
      } else if (polyType >= 3) {
        // Triangle fan tessellation: (polyType - 2) triangles = (polyType - 2) * 3 indices
        indexCount = (polyType - 2) * 3;
      }

      PolygonAttributes polyAttr;
      polyAttr.polygonID = rawPoly.attribute.PolygonID;
      polyAttr.enableFog = rawPoly.attribute.Fog_Enable ? 1 : 0;
      polyAttr.polyAlpha = (float)rawPoly.attribute.Alpha / 31.0f; // Convert 5-bit alpha (0-31) to 0.0-1.0
      
      // Get texture size for matching soft rasterizer formula
      const NDSTextureFormat packFormat = (NDSTextureFormat)rawPoly.texParam.PackedFormat;
      const bool hasTexture = (packFormat != TEXMODE_NONE);
      if (hasTexture) {
        const u32 texWidth = 8 << rawPoly.texParam.SizeShiftS;
        const u32 texHeight = 8 << rawPoly.texParam.SizeShiftT;
        if (texWidth > 0 && texHeight > 0) {
          // Store 1.0/width and 1.0/height for normalization
          polyAttr.texScaleS = 1.0f / (float)texWidth;
          polyAttr.texScaleT = 1.0f / (float)texHeight;
          
          // Determine wrap modes independently for S and T axes
          // 0=clamp, 1=repeat, 2=mirror
          const bool repeatS = rawPoly.texParam.RepeatS_Enable;
          const bool repeatT = rawPoly.texParam.RepeatT_Enable;
          const bool mirrorS = rawPoly.texParam.MirroredRepeatS_Enable;
          const bool mirrorT = rawPoly.texParam.MirroredRepeatT_Enable;
          
          if (repeatS && mirrorS) {
            polyAttr.wrapModeS = 2; // Mirror
          } else if (repeatS) {
            polyAttr.wrapModeS = 1; // Repeat
          } else {
            polyAttr.wrapModeS = 0; // Clamp
          }
          
          if (repeatT && mirrorT) {
            polyAttr.wrapModeT = 2; // Mirror
          } else if (repeatT) {
            polyAttr.wrapModeT = 1; // Repeat
          } else {
            polyAttr.wrapModeT = 0; // Clamp
          }
        } else {
          polyAttr.texScaleS = 1.0f;
          polyAttr.texScaleT = 1.0f;
          polyAttr.wrapModeS = 0;
          polyAttr.wrapModeT = 0;
        }
      } else {
        polyAttr.texScaleS = 1.0f; // Not used for untextured polygons
        polyAttr.texScaleT = 1.0f;
        polyAttr.wrapModeS = 0;
        polyAttr.wrapModeT = 0;
      }

      [_renderCommandEncoder setFragmentBytes:&polyAttr
                                       length:sizeof(PolygonAttributes)
                                      atIndex:0];

      // draw the polygon
      if (indexCount > 0) {
        [_renderCommandEncoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                          indexCount:indexCount
                                           indexType:MTLIndexTypeUInt16
                                         indexBuffer:_indexBuffer
                                   indexBufferOffset:indexOffset * sizeof(u16)];
      }

      // increment the index offset
      indexOffset += indexCount;
    }
  }

  // finish recording the render command encoder
  [_renderCommandEncoder endEncoding];

  _renderCommandEncoder = nil;

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::InitializeRenderTargets(size_t width,
                                                   size_t height) {
  // Create color texture (RGBAB8)
  MTLTextureDescriptor *colorTexDescriptor = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:width
                                  height:height
                               mipmapped:NO];
  colorTexDescriptor.usage = MTLTextureUsageRenderTarget;
  colorTexDescriptor.storageMode = MTLStorageModeShared;
  _colorTexture = [_device newTextureWithDescriptor:colorTexDescriptor];
  if (_colorTexture == nil) {
    NSLog(@"Error: Failed to create color texture");
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Create depth texture (Depth32Float)
  MTLTextureDescriptor *depthTexDescriptor = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                   width:width
                                  height:height
                               mipmapped:NO];
  // Need both RenderTarget and ShaderRead since postprocessing shaders read
  // depth
  depthTexDescriptor.usage =
      MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  depthTexDescriptor.storageMode = MTLStorageModeShared;
  _depthTexture = [_device newTextureWithDescriptor:depthTexDescriptor];
  if (_depthTexture == nil) {
    NSLog(@"Error: Failed to create depth texture");
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Create render pass descriptor
  _renderPassDescriptor = [MTLRenderPassDescriptor renderPassDescriptor];

  // configure color attachment
  _renderPassDescriptor.colorAttachments[0].texture = _colorTexture;
  _renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
  _renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
  _renderPassDescriptor.colorAttachments[0].clearColor =
      MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

  // configure depth attachment
  _renderPassDescriptor.depthAttachment.texture = _depthTexture;
  _renderPassDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
  _renderPassDescriptor.depthAttachment.storeAction = MTLStoreActionStore;
  _renderPassDescriptor.depthAttachment.clearDepth = 1.0;

  // configure stencil attachment (shares texture with depth)
  _renderPassDescriptor.stencilAttachment.texture = _depthTexture;
  _renderPassDescriptor.stencilAttachment.loadAction = MTLLoadActionClear;
  _renderPassDescriptor.stencilAttachment.storeAction = MTLStoreActionStore;
  _renderPassDescriptor.stencilAttachment.clearStencil = 0;

  // Create polygon ID texture for edge detection
  // Uses r8uint format to store polygon IDs (0-63)
  MTLTextureDescriptor *polyIDDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Uint
                                   width:width
                                  height:height
                               mipmapped:NO];
  polyIDDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  polyIDDesc.storageMode = MTLStorageModePrivate;
  _polygonIDTexture = [_device newTextureWithDescriptor:polyIDDesc];

  // Create fog attributes texture
  // Use R8Unorm to store per-pixel fog enable flags
  MTLTextureDescriptor *fogAttrDesc = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                   width:width
                                  height:height
                               mipmapped:NO];
  fogAttrDesc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
  fogAttrDesc.storageMode = MTLStorageModePrivate;
  _fogAttributesTexture = [_device newTextureWithDescriptor:fogAttrDesc];

  // Configure additional color attachments for postprocessing in render pass
  // descriptor
  _renderPassDescriptor.colorAttachments[1].texture = _polygonIDTexture;
  _renderPassDescriptor.colorAttachments[1].loadAction = MTLLoadActionClear;
  _renderPassDescriptor.colorAttachments[1].storeAction = MTLStoreActionStore;
  _renderPassDescriptor.colorAttachments[1].clearColor =
      MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

  _renderPassDescriptor.colorAttachments[2].texture = _fogAttributesTexture;
  _renderPassDescriptor.colorAttachments[2].loadAction = MTLLoadActionClear;
  _renderPassDescriptor.colorAttachments[2].storeAction = MTLStoreActionStore;
  _renderPassDescriptor.colorAttachments[2].clearColor =
      MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::InitializeSamplerState() {
  @autoreleasepool {
    // Create sampler descriptors for different texture wrapping and filtering
    // modes
    // The Nintendo DS supports various texture wrapping modes:
    // - Clamp to edge (no repeat)
    // - Repeat (wrap around)
    // - Mirrored repeat (flip at edges)

    // Clamp to edge with nearest filtering
    // used when polygon disables texture repeat (UI elements, sprites, etc.)
    MTLSamplerDescriptor *clampNearestDesc = [MTLSamplerDescriptor new];
  clampNearestDesc.minFilter = MTLSamplerMinMagFilterNearest;
  clampNearestDesc.magFilter = MTLSamplerMinMagFilterNearest;
  clampNearestDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
  clampNearestDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
  clampNearestDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
  clampNearestDesc.normalizedCoordinates = YES;

  _samplerStateClampNearest =
      [_device newSamplerStateWithDescriptor:clampNearestDesc];
  if (_samplerStateClampNearest == nil) {
    NSLog(@"Error: Failed to create clamp nearest sampler state");
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Clamp to edge with linear filtering
  // Used when texture smoothing is enabled for non-repeating textures
  MTLSamplerDescriptor *clampLinearDesc = [MTLSamplerDescriptor new];
  clampLinearDesc.minFilter = MTLSamplerMinMagFilterLinear;
  clampLinearDesc.magFilter = MTLSamplerMinMagFilterLinear;
  clampLinearDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
  clampLinearDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
  clampLinearDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
  clampLinearDesc.normalizedCoordinates = YES;

  _samplerStateClampLinear =
      [_device newSamplerStateWithDescriptor:clampLinearDesc];
  if (_samplerStateClampLinear == nil) {
    NSLog(@"Error: Failed to create clamp linear sampler state");
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Repeat with nearest neighbor filtering
  // Used for tiled textures (floors, walls, terrain)
  MTLSamplerDescriptor *repeatNearestDesc = [MTLSamplerDescriptor new];
  repeatNearestDesc.minFilter = MTLSamplerMinMagFilterNearest;
  repeatNearestDesc.magFilter = MTLSamplerMinMagFilterNearest;
  repeatNearestDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
  repeatNearestDesc.sAddressMode = MTLSamplerAddressModeRepeat;
  repeatNearestDesc.tAddressMode = MTLSamplerAddressModeRepeat;
  repeatNearestDesc.normalizedCoordinates = YES;

  _samplerStateRepeatNearest =
      [_device newSamplerStateWithDescriptor:repeatNearestDesc];
  if (_samplerStateRepeatNearest == nil) {
    NSLog(@"Error: Failed to create repeat nearest sampler state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // Repeat with linear filtering
  //  Used for tiled textures with smoothing enabled
  MTLSamplerDescriptor *repeatLinearDesc = [MTLSamplerDescriptor new];
  repeatLinearDesc.minFilter = MTLSamplerMinMagFilterLinear;
  repeatLinearDesc.magFilter = MTLSamplerMinMagFilterLinear;
  repeatLinearDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
  repeatLinearDesc.sAddressMode = MTLSamplerAddressModeRepeat;
  repeatLinearDesc.tAddressMode = MTLSamplerAddressModeRepeat;
  repeatLinearDesc.normalizedCoordinates = YES;

  _samplerStateRepeatLinear =
      [_device newSamplerStateWithDescriptor:repeatLinearDesc];
  if (_samplerStateRepeatLinear == nil) {
    NSLog(@"Error: Failed to create repeat linear sampler state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // Mirrored repeat with nearest neighbor filtering
  // Used when textures have mirrored repeat enabled (flips at boundaries)
  MTLSamplerDescriptor *mirrorNearestDesc = [MTLSamplerDescriptor new];
  mirrorNearestDesc.minFilter = MTLSamplerMinMagFilterNearest;
  mirrorNearestDesc.magFilter = MTLSamplerMinMagFilterNearest;
  mirrorNearestDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
  mirrorNearestDesc.sAddressMode = MTLSamplerAddressModeMirrorRepeat;
  mirrorNearestDesc.tAddressMode = MTLSamplerAddressModeMirrorRepeat;
  mirrorNearestDesc.normalizedCoordinates = YES;

  _samplerStateMirrorNearest =
      [_device newSamplerStateWithDescriptor:mirrorNearestDesc];
  if (_samplerStateMirrorNearest == nil) {
    NSLog(@"Error: Failed to create mirror nearest sampler state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // Mirrored repeat with linear filtering
  // Used when textures have mirrored repeat enabled with smoothing
  MTLSamplerDescriptor *mirrorLinearDesc = [MTLSamplerDescriptor new];
  mirrorLinearDesc.minFilter = MTLSamplerMinMagFilterLinear;
  mirrorLinearDesc.magFilter = MTLSamplerMinMagFilterLinear;
  mirrorLinearDesc.mipFilter = MTLSamplerMipFilterNotMipmapped;
  mirrorLinearDesc.sAddressMode = MTLSamplerAddressModeMirrorRepeat;
  mirrorLinearDesc.tAddressMode = MTLSamplerAddressModeMirrorRepeat;
  mirrorLinearDesc.normalizedCoordinates = YES;

  _samplerStateMirrorLinear =
      [_device newSamplerStateWithDescriptor:mirrorLinearDesc];
  if (_samplerStateMirrorLinear == nil) {
    NSLog(@"Error: Failed to create mirror linear sampler state");
    return RENDER3DERROR_INVALID_BINDING;
  }

  // Create a 1x1 white texture for untextured polygons
  // When texturing is disabled, we bind this white texture so the shader
  // samples (1,1,1,1) which preserves the vertex colors
  MTLTextureDescriptor *whiteTexDesc = [MTLTextureDescriptor 
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:1
                                  height:1
                               mipmapped:NO];
  whiteTexDesc.usage = MTLTextureUsageShaderRead;
  whiteTexDesc.storageMode = MTLStorageModeShared;
  
  _dummyWhiteTexture = [_device newTextureWithDescriptor:whiteTexDesc];
  if (_dummyWhiteTexture == nil) {
    NSLog(@"Error: Failed to create dummy white texture");
    return RENDER3DERROR_INVALID_BINDING;
  }
  
    // Fill the texture with white (255, 255, 255, 255)
    uint32_t whitePixel = 0xFFFFFFFF;
    MTLRegion region = MTLRegionMake2D(0, 0, 1, 1);
    [_dummyWhiteTexture replaceRegion:region
                          mipmapLevel:0
                            withBytes:&whitePixel
                          bytesPerRow:4];

    return RENDER3DERROR_NOERR;
  } // @autoreleasepool
}

Render3DError MetalRender::PostprocessFramebuffer() {
  // Early return if postprocessing is not needed
  if (_clippedPolyCount == 0 || (!_enableEdgeMark && !_enableFog)) {
    return RENDER3DERROR_NOERR;
  }

  // We need to encode post-processing passes
  // this happens after the geometry is rendered and has filled the
  // color/stencil/depth buffers

  @autoreleasepool {
    // Create a render pass, since we render back the same color texture, from
    // depth/stencil buffers
    MTLRenderPassDescriptor *postprocessPass =
        [MTLRenderPassDescriptor renderPassDescriptor];

    // Color attachment, read/write the same texture
    postprocessPass.colorAttachments[0].texture = _colorTexture;
    postprocessPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    postprocessPass.colorAttachments[0].storeAction = MTLStoreActionStore;

    // Create a render command encoder for the postprocessing pass
    id<MTLRenderCommandEncoder> encoder =
        [_commandBuffer renderCommandEncoderWithDescriptor:postprocessPass];
    if (encoder == nil) {
      return RENDER3DERROR_INVALID_BUFFER;
    }
    [encoder setLabel:@"DeSmuMe 3D Postprocessing Command Encoder"];

    MTLViewport viewport = {
        .originX = 0.0,
        .originY = 0.0,
        .width = static_cast<double>(_framebufferWidth),
        .height = static_cast<double>(_framebufferHeight),
        .znear = 0.0,
        .zfar = 1.0,
    };
    [encoder setViewport:viewport];

    // bind full-screen quad vertex buffer
    [encoder setVertexBuffer:_postprocessVertexBuffer offset:0 atIndex:0];

    // edge marking pass
    if (_enableEdgeMark) {
      [encoder pushDebugGroup:@"Edge Marking"];
      [encoder setRenderPipelineState:_pipelineStateEdgeMark];

      // Bind depth texture for edge depth testing
      [encoder setFragmentTexture:_depthTexture atIndex:0];

      // Bind polygon ID texture for edge detection
      // NOTE: You need to render polygon IDs during geometry pass
      // to a separate color attachment or use stencil buffer
      [encoder setFragmentTexture:_polygonIDTexture atIndex:1];

      // Bind render states (contains edge colors)
      [encoder setFragmentBuffer:_renderStatesBuffer offset:0 atIndex:0];

      // Draw fullscreen quad (triangle strip, 4 vertices)
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                  vertexStart:0
                  vertexCount:4];

      [encoder popDebugGroup];
    }

    // fog pass
    if (_enableFog) {
      [encoder pushDebugGroup:@"Fog"];
      [encoder setRenderPipelineState:_pipelineStateFog];

      // Bind depth texture for fog depth calculation
      [encoder setFragmentTexture:_depthTexture atIndex:0];

      // Bind fog attributes texture (per-pixel fog enable flags)
      // NOTE: This needs to be written during geometry pass
      [encoder setFragmentTexture:_fogAttributesTexture atIndex:1];

      // Bind fog density lookup table (1D texture)
      [encoder setFragmentTexture:_fogDensityTexture atIndex:2];

      // Bind render states (contains fog color, fog offset/step)
      [encoder setFragmentBuffer:_renderStatesBuffer offset:0 atIndex:0];

      // Set blend color to fog color for special fog blend mode
      float fogR = _currentRenderStates.fogColor[0];
      float fogG = _currentRenderStates.fogColor[1];
      float fogB = _currentRenderStates.fogColor[2];
      float fogA = _currentRenderStates.fogColor[3];
      [encoder setBlendColorRed:fogR green:fogG blue:fogB alpha:fogA];

      // Draw fullscreen quad
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                  vertexStart:0
                  vertexCount:4];

      [encoder popDebugGroup];
    }

    [encoder endEncoding];
  }
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::EndRender() {
  // Commit the command buffer and wait for it to complete
  [_commandBuffer commit];
  [_commandBuffer waitUntilCompleted];
  
  // Release the command buffer to prevent reuse of committed buffers
  _commandBuffer = nil;

  // Unbind the renderer to trigger framebuffer readback
  _colorOut->UnbindRenderer(this->_lastBoundColorOut);
  this->_lastBoundColorOut = RENDER3D_RESOURCE_INDEX_NONE;

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::ClearUsingImage(const u16 *__restrict colorBuffer,
                                           const u32 *__restrict depthBuffer,
                                           const u8 *__restrict fogBuffer,
                                           const u8 opaquePolyID) {
  // MetalRender uses GPU-based rendering, so uploading the clear image
  // would require:
  // 1. Creating temporary Metal textures for color and depth data
  // 2. Converting format from RGBA5551/24-bit depth to Metal formats
  // 3. Using a blit encoder or full-screen quad to render the clear image
  // 4. Handling scaling if framebuffer size != native DS resolution
  //
  // This is complex and the clear image feature (RearPlaneMode) is rarely
  // used by games. For now, we return an error to fall back to
  // ClearUsingValues, which provides a simple solid-color clear instead of
  // the full image.
  //
  // Games that rely on RearPlaneMode will have incorrect backgrounds (solid
  // color instead of texture) until this is fully implemented. Known
  // affected games:
  // - The Chronicles of Narnia: The Lion, the Witch and the Wardrobe
  // - Harry Potter and the Order of Phoenix
  // - Blazer Drive
  //
  // - Sonic Chronicles: The Dark Brotherhood
  //
  // TODO: Implement full GPU-based clear image rendering for proper
  // RearPlaneMode support.

  // Return an error to trigger fallback to ClearUsingValues
  return RENDER3DERROR_INVALID_BINDING;
}

Render3DError
MetalRender::ClearUsingValues(const Color4u8 &clearColor6665,
                              const FragmentAttributes &clearAttributes) {
  // Make a render pass descriptor that clears the attachments
  if (_renderPassDescriptor == nil) {
    return RENDER3DERROR_NOERR;
  }

  // Convert the DS color format (6665) to Metal color format (RGBA8888)
  _renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
  _renderPassDescriptor.colorAttachments[0].clearColor =
      MTLClearColorMake(clearColor6665.r / 63.0f, // 6-bit color
                        clearColor6665.g / 63.0f, // 6-bit color
                        clearColor6665.b / 63.0f, // 6-bit color
                        clearColor6665.a / 31.0f  // 5-bit alpha
      );

  // Set the clear depth/stencil
  _renderPassDescriptor.depthAttachment.loadAction = MTLLoadActionClear;
  _renderPassDescriptor.depthAttachment.clearDepth =
      clearAttributes.depth / (double)0x00FFFFFF;

  _renderPassDescriptor.stencilAttachment.loadAction = MTLLoadActionClear;
  _renderPassDescriptor.stencilAttachment.clearStencil =
      clearAttributes.opaquePolyID;

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::SetupTexture(const POLY &thePoly,
                                        size_t polyRenderIndex) {
  // Bounds check to prevent buffer overflow
  if (polyRenderIndex >= CLIPPED_POLYLIST_SIZE) {
    return RENDER3DERROR_NOERR; // Skip texture setup if index is out of
                                // bounds
  }

  // Get the metal texture for this polygon from the texture cache
  MetalTexture *theTexture = (MetalTexture *)_textureList[polyRenderIndex];

  if (theTexture == nil) {
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Check if the texture sampling is enabled
  // polygons can disable texturing even if the texture is loaded
  // (for flat-shaped polygons or when the texture format is TEXMODE_NONE)
  if (!theTexture->IsSamplingEnabled()) {
    // For untextured polygons, bind a white dummy texture
    // This ensures the shader samples (1,1,1,1) which will preserve vertex colors
    if (_dummyWhiteTexture != nil) {
      [_renderCommandEncoder setFragmentTexture:_dummyWhiteTexture atIndex:0];
      [_renderCommandEncoder setFragmentSamplerState:_samplerStateClampNearest atIndex:0];
    }
    return RENDER3DERROR_NOERR;
  }

  // Get the Metal Texture ID
  id<MTLTexture> texID = theTexture->GetTexID();
  if (texID == nil || !theTexture->IsTexInited()) {
    // Bind white dummy texture as fallback
    if (_dummyWhiteTexture != nil) {
      [_renderCommandEncoder setFragmentTexture:_dummyWhiteTexture atIndex:0];
      [_renderCommandEncoder setFragmentSamplerState:_samplerStateClampNearest atIndex:0];
    }
    return RENDER3DERROR_NOERR;
  }

  // Bind the texture to the fragment shader at binding point 0
  [_renderCommandEncoder setFragmentTexture:texID atIndex:0];

  // Select the appropriate sampler state based on the texture wrapping mode
  // The DS supports different texture wrapping modes for each axis:
  // 0: Clamp to edge (no repeat)
  // 1: Repeat (RepeatS/T_Enable = 1, MirroredRepeatS/T_Enable = 0)
  // 2: Mirrored repeat (RepeatS/T_Enable = 1, MirroredRepeatS/T_Enable = 1)
  // 3: Flip (not commonly used)
  //
  // The mirrored repeat mode interacts with the repeat enable flag:
  // - If RepeatS_Enable is set AND MirroredRepeatS_Enable is set, use mirrored
  // repeat for S
  // - If RepeatT_Enable is set AND MirroredRepeatT_Enable is set, use mirrored
  // repeat for T

  const bool repeatS = thePoly.texParam.RepeatS_Enable;
  const bool repeatT = thePoly.texParam.RepeatT_Enable;
  const bool mirrorS = thePoly.texParam.MirroredRepeatS_Enable;
  const bool mirrorT = thePoly.texParam.MirroredRepeatT_Enable;

  // Determine the wrapping mode
  // For simplicity, if either axis uses mirrored repeat, use mirror sampler
  // If either axis uses regular repeat (but not mirror), use repeat sampler
  // Otherwise use clamp sampler
  const bool useMirror = (repeatS && mirrorS) || (repeatT && mirrorT);
  const bool useRepeat = !useMirror && (repeatS || repeatT);

  // Determine if we should use linear or nearest filtering
  // _enableTextureSmoothing comes from the emulator settings
  const bool useLinearFiltering = this->_enableTextureSmoothing;

  // Select the appropriate sampler based on wrap mode and filtering
  id<MTLSamplerState> selectedSampler = nil;

  if (useMirror) {
    // Use mirrored repeat sampler
    if (useLinearFiltering) {
      selectedSampler = _samplerStateMirrorLinear;
    } else {
      selectedSampler = _samplerStateMirrorNearest;
    }
  } else if (useRepeat) {
    // Use regular repeat sampler
    if (useLinearFiltering) {
      selectedSampler = _samplerStateRepeatLinear;
    } else {
      selectedSampler = _samplerStateRepeatNearest;
    }
  } else {
    // Use clamp sampler
    if (useLinearFiltering) {
      selectedSampler = _samplerStateClampLinear;
    } else {
      selectedSampler = _samplerStateClampNearest;
    }
  }

  // Bind the sampler to the fragment shader at binding point 0
  // This corresponds to [[sampler(0)]] in the fragment shader
  [_renderCommandEncoder setFragmentSamplerState:selectedSampler atIndex:0];

  // Mark this texture as used this frame (for cache management)
  theTexture->MarkUsedThisFrame();

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::SetupViewport(const GFX3D_Viewport viewport) {
  // calculate how much the viewport is scaled from the native framebuffer
  // size
  const float wScalar = _framebufferWidth / (float)GPU_FRAMEBUFFER_NATIVE_WIDTH;
  const float hScalar =
      _framebufferHeight / (float)GPU_FRAMEBUFFER_NATIVE_HEIGHT;

  // set the viewport
  MTLViewport metalViewport;
  metalViewport.originX = viewport.x * wScalar;
  // Y-coordinate need special handling because the viewport is flipped
  // vertically
  // Metal uses top-left origin, but the NDS viewport is bottom-left origin
  // so we need to flip the Y-coordinate, then apply the scaling factor
  metalViewport.originY = (-viewport.y + (192 - viewport.height)) * hScalar;
  metalViewport.width = viewport.width * wScalar;
  metalViewport.height = viewport.height * hScalar;

  // Set the depth range for this
  // 0.0 is the near plane, 1.0 is the far plane
  // NDS uses full depth range, so we use Metal's default depth range
  metalViewport.znear = 0.0;
  metalViewport.zfar = 1.0;

  // Set the viewport
  [_renderCommandEncoder setViewport:metalViewport];

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::InitializePostprocessPipelines() {
  @autoreleasepool {
    // Load the default Metal library, as this will automatically load the
    // shaders. (+1 retain count, must release)
    id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];
    if (defaultLibrary == nil) {
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Get the vertex function from the library (+1 retain count, must release)
    id<MTLFunction> vertexFunction =
        [defaultLibrary newFunctionWithName:@"postprocessVertex"];
    if (vertexFunction == nil) {
      [defaultLibrary release];
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Get the fragment function from the library (+1 retain count, must release)
    id<MTLFunction> fragmentFunction =
        [defaultLibrary newFunctionWithName:@"fogFragment"];
    if (fragmentFunction == nil) {
      [vertexFunction release];
      [defaultLibrary release];
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Get the edge marking fragment function from the library (+1 retain count, must release)
    id<MTLFunction> edgeMarkFragmentFunction =
        [defaultLibrary newFunctionWithName:@"edgeMarkFragment"];
    if (edgeMarkFragmentFunction == nil) {
      [fragmentFunction release];
      [vertexFunction release];
      [defaultLibrary release];
      return RENDER3DERROR_INVALID_BINDING;
    }

  // Edge marking pipeline
  MTLRenderPipelineDescriptor *edgeDesc = [MTLRenderPipelineDescriptor new];
  edgeDesc.label = @"Edge Marking Pipeline";
  edgeDesc.vertexFunction = vertexFunction;
  edgeDesc.fragmentFunction = edgeMarkFragmentFunction;

  // Configure color attachments, blend edge colors onto the rendered image
  edgeDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
  edgeDesc.colorAttachments[0].blendingEnabled = YES;

  // Blend mode, add edge color on top, preserve the max alpha
  edgeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
  edgeDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
  edgeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
  edgeDesc.colorAttachments[0].sourceAlphaBlendFactor =
      MTLBlendFactorSourceAlpha;
  edgeDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorDestinationAlpha;
  edgeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationMax;

  // Vertex desc for fullscreen quad
  MTLVertexDescriptor *postprocessVertexDesc = [MTLVertexDescriptor new];
  // Position (x, y)
  postprocessVertexDesc.attributes[0].format = MTLVertexFormatFloat2;
  postprocessVertexDesc.attributes[0].bufferIndex = 0;
  postprocessVertexDesc.attributes[0].offset = 0;

  // TexCoord (u, v)
  postprocessVertexDesc.attributes[1].format = MTLVertexFormatFloat2;
  postprocessVertexDesc.attributes[1].bufferIndex = 0;
  postprocessVertexDesc.attributes[1].offset = 2 * sizeof(float);

  // Stride, 4 floats per vertex
  postprocessVertexDesc.layouts[0].stride = 4 * sizeof(float);
  postprocessVertexDesc.layouts[0].stepFunction =
      MTLVertexStepFunctionPerVertex;

  edgeDesc.vertexDescriptor = postprocessVertexDesc;

    NSError *error = nil;
    _pipelineStateEdgeMark =
        [_device newRenderPipelineStateWithDescriptor:edgeDesc error:&error];
    if (_pipelineStateEdgeMark == nil) {
      NSLog(@"Failed to create edge mark pipeline: %@", error);
      [edgeMarkFragmentFunction release];
      [fragmentFunction release];
      [vertexFunction release];
      [defaultLibrary release];
      return RENDER3DERROR_INVALID_BINDING;
    }

  // Fog rendering pipeline
  MTLRenderPipelineDescriptor *fogDesc = [MTLRenderPipelineDescriptor new];
  fogDesc.label = @"Fog Rendering Pipeline";
  fogDesc.vertexFunction = vertexFunction;
  fogDesc.fragmentFunction = fragmentFunction;

  // Color attachment - blend fog color with scene
  fogDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
  fogDesc.colorAttachments[0].blendingEnabled = YES;

  // Special fog blend mode from the NDS hardware
  // RGB: constantColor * (1 - srcColor)
  // Alpha: constantAlpha * (1 - srcAlpha)
  fogDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorBlendColor;
  fogDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceColor;
  fogDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
  fogDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorBlendAlpha;
  fogDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
  fogDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;

  fogDesc.vertexDescriptor = postprocessVertexDesc;

    _pipelineStateFog = [_device newRenderPipelineStateWithDescriptor:fogDesc
                                                                error:&error];

    if (_pipelineStateFog == nil) {
      NSLog(@"Failed to create fog pipeline: %@", error);
      [edgeMarkFragmentFunction release];
      [fragmentFunction release];
      [vertexFunction release];
      [defaultLibrary release];
      return RENDER3DERROR_INVALID_BINDING;
    }

    // Release temporary objects (descriptors auto-released by pool)
    [edgeMarkFragmentFunction release];
    [fragmentFunction release];
    [vertexFunction release];
    [defaultLibrary release];

    return RENDER3DERROR_NOERR;
  } // @autoreleasepool
}

Render3DError MetalRender::CreateFullscreenQuad() {
  // Fullscreen quad vertices in NDC space (-1 to 1)
  // Format: position (x, y), texCoord (u, v)
  float quadVertices[] = {
      // Positions    // TexCoords
      -1.0f, 1.0f,  0.0f, 0.0f, // Top-left
      1.0f,  1.0f,  1.0f, 0.0f, // Top-right
      -1.0f, -1.0f, 0.0f, 1.0f, // Bottom-left
      1.0f,  -1.0f, 1.0f, 1.0f  // Bottom-right
  };

  _postprocessVertexBuffer =
      [_device newBufferWithBytes:quadVertices
                           length:sizeof(quadVertices)
                          options:MTLResourceStorageModeShared];

  if (_postprocessVertexBuffer == nil) {
    NSLog(@"Error: Failed to create fullscreen quad vertex buffer");
    return RENDER3DERROR_INVALID_BINDING;
  }

  return RENDER3DERROR_NOERR;
}

MetalTexture *
MetalRender::GetLoadedTextureFromPolygon(const POLY &thePoly,
                                         bool enableTextureSampling) {
  // Get the texture from the texture cache
  MetalTexture *theTexture =
      (MetalTexture *)texCache.GetTexture(thePoly.texParam, thePoly.texPalette);

  // If this is a new texture, create it
  if (theTexture == nil) {
    theTexture =
        new MetalTexture(thePoly.texParam, thePoly.texPalette, _device);
    texCache.Add(theTexture);
  }

  // Determine if the texture is enabled
  const NDSTextureFormat packFormat = theTexture->GetPackFormat();
  const bool isTextureEnabled =
      ((packFormat != TEXMODE_NONE) && enableTextureSampling);
  theTexture->SetSamplingEnabled(isTextureEnabled);

  // Load the texture if it is needed
  if (theTexture->IsLoadNeeded() && isTextureEnabled) {
    // Set the preprocessing before loading
    theTexture->SetUseDeposterize(this->_enableTextureDeposterize);
    theTexture->SetScalingFactor(this->_textureScalingFactor);

    theTexture->Load(theTexture->GetUnpackBuffer());
  }

  return theTexture;
}

MetalTexture::MetalTexture(TEXIMAGE_PARAM texAttributes, u32 palAttributes,
                           MTLDevicePtr device)
    : Render3DTexture(texAttributes, palAttributes), _texID(nil),
      _device(device), _isTexInited(false), _unpackBuffer(nullptr) {
  // Allocate buffer for unpacking texture data from NDS format to RGBA8888
  // The size is calculated based on texture dimensions and 4 bytes per
  // pixel
  const size_t unpackBufferSize =
      this->GetUnpackSizeUsingFormat(TexFormat_32bpp);
  _unpackBuffer = (u32 *)malloc(unpackBufferSize);

  if (_unpackBuffer == nullptr) {
    throw std::runtime_error("Failed to allocate texture unpack buffer");
  }
}

MetalTexture::~MetalTexture() {
  // Metal ARC will clean up _texID automatically
  _texID = nil;

  // Free the unpack buffer
  free(_unpackBuffer);
  _unpackBuffer = nullptr;
}

void MetalTexture::Load(void *targetBuffer) {
  // Force reload from VRAM to ensure we have fresh texture data
  this->VRAMCompareAndUpdate();
  
  // Proceed with normal loading - unpack the texture
  // We'll validate the result after unpacking
  Render3DTexture::Load(targetBuffer);
  
  // Check if unpacking succeeded (produces non-zero data)
  // This is the definitive check - if unpacked data is all zeros, texture isn't ready
  const size_t pixelCount = this->_sizeS * this->_sizeT;
  const u32 *buf = (const u32 *)targetBuffer;
  bool hasPixelData = false;
  
  // For small textures, check all pixels. For larger textures, sample more pixels
  const size_t checkCount = (pixelCount <= 256) ? pixelCount : std::min<size_t>(256, pixelCount);
  for (size_t i = 0; i < checkCount; i++) {
    if (buf[i] != 0) {
      hasPixelData = true;
      break;
    }
  }
  
  if (!hasPixelData) {
    // Unpacking produced empty data - could be:
    // 1. Texture not ready yet (VRAM empty)
    // 2. Valid transparent texture (all pixels are transparent black)
    // For now, we'll accept transparent textures and let them render
    // (they'll appear as transparent/black, which is correct)
    
    // Only reject if this is a very small texture (likely to be invalid if all zeros)
    // For larger textures, accept them even if all zeros (might be intentionally transparent)
    if (pixelCount <= 64) {
      // Small texture with all zeros - likely invalid, keep trying
      this->_isLoadNeeded = true;
      return;  // Don't create Metal texture
    }
  }
  
  // Apply deposterization if enabled
  // The base class sets up _deposterizeSrcSurface and
  // _deposterizeDstSurface
  if (this->_useDeposterize) {
    // Deposterization requires additional surfaces; for now we skip this
    // and just use the unpacked data directly
    // TODO: Implement deposterization support for Metal textures
  }

  // Get texture dimensions from base class
  const size_t texWidth = this->_sizeS;
  const size_t texHeight = this->_sizeT;

  // Use the unpacked texture data from the target buffer (where base class wrote it)
  const u32 *texData = (const u32 *)targetBuffer; // RGBA8888 format

  // Create Metal texture descriptor
  // Nintendo DS textures are unpacked to RGBA8888 format
  MTLTextureDescriptor *texDescriptor = [MTLTextureDescriptor
      texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                   width:texWidth
                                  height:texHeight
                               mipmapped:NO];

  texDescriptor.usage = MTLTextureUsageShaderRead;
  texDescriptor.storageMode = MTLStorageModeShared;

  // Create the Metal texture
  _texID = [_device newTextureWithDescriptor:texDescriptor];
  if (_texID == nil) {
    // Allocation failed - mark for retry 
    this->_isLoadNeeded = true;
    return;
  }

  // Upload texture data to GPU
  MTLRegion region = MTLRegionMake2D(0, 0, texWidth, texHeight);
  [_texID replaceRegion:region
            mipmapLevel:0
              withBytes:texData
            bytesPerRow:texWidth * 4]; // 4 bytes per pixel (RGBA8)

  _isTexInited = true;
}

id<MTLTexture> MetalTexture::GetTexID() const { return _texID; }

bool MetalTexture::IsTexInited() const { return _isTexInited; }

u32 *MetalTexture::GetUnpackBuffer() const { return _unpackBuffer; }

MetalRenderColorOut::MetalRenderColorOut(MTLDevicePtr device, size_t w,
                                         size_t h)
    : Render3DColorOut(), _device(device), _colorTexture(nil),
      _masterBuffer32(nullptr), _readbackBuffer{nullptr, nullptr} {
  // set the frame buffer dimension
  this->_framebufferWidth = w;
  this->_framebufferHeight = h;
  this->_framebufferPixelCount = w * h;
  this->_framebufferSize32 = w * h * sizeof(Color4u8);

  // allocate single large buffer for both readback and master buffers
  // use an aligned allocation to ensure proper alignment for GPU operations
  _masterBuffer32 =
      (Color4u8 *)malloc_aligned(this->_framebufferSize32 * 2,
                                 64); // 64-byte alignment for SIMD operations

  if (_masterBuffer32 == nullptr) {
    throw std::runtime_error("Failed to allocate master buffer");
  }

  // initialize the readback buffers by splitting the master buffer into two
  // halves for double-buffering
  _readbackBuffer[0] = _masterBuffer32;
  _readbackBuffer[1] = _masterBuffer32 + this->_framebufferPixelCount;

  // Point the base class buffer pointers to the readback buffers
  this->_buffer32[0] = _readbackBuffer[0];
  this->_buffer32[1] = _readbackBuffer[1];

  // Initialize the state array for async readback (both buffers start free)
  this->_state[0] = AsyncReadState_Free;
  this->_state[1] = AsyncReadState_Free;

  NSLog(@"MetalRenderColorOut created with framebuffer size: %zu x %zu", w, h);
}

MetalRenderColorOut::~MetalRenderColorOut() {
  // free the master buffer
  if (_masterBuffer32 != nullptr) {
    free_aligned(_masterBuffer32);
    _masterBuffer32 = nullptr;
  }

  // set the color texture to nil
  _colorTexture = nil;

  // nullify the readback buffers
  _readbackBuffer[0] = nullptr;
  _readbackBuffer[1] = nullptr;
  _buffer32[0] = nullptr;
  _buffer32[1] = nullptr;

  // nullify the device
  _device = nil;

  NSLog(@"MetalRenderColorOut destroyed");
}

void MetalRenderColorOut::Reset() {
  if (_masterBuffer32 != nullptr) {
    memset(_masterBuffer32, 0, this->_framebufferSize32 * 2);
  }
}

// Note: SetColorFormat is NOT virtual in the base class Render3DColorOut,
// so we cannot override it. Instead, we check this->_format directly in UnbindRenderer.
// The base class SetColorFormat() will set this->_format for us.

size_t MetalRenderColorOut::BindRead32() {
  return this->Render3DColorOut::BindRead32();
}

size_t MetalRenderColorOut::UnbindRead32() {
  return this->Render3DColorOut::UnbindRead32();
}

size_t MetalRenderColorOut::BindRenderer() {
  size_t idxFree = RENDER3D_RESOURCE_INDEX_NONE;

  // Find a free or ready buffer slot
  if (_state[0] == AsyncReadState_Free) {
    idxFree = 0;
  } else if (_state[1] == AsyncReadState_Free) {
    idxFree = 1;
  } else if (_state[0] == AsyncReadState_Ready) {
    // reuse the ready buffer
    idxFree = 0;
    _currentReadyIdx = RENDER3D_RESOURCE_INDEX_NONE;
  } else if (_state[1] == AsyncReadState_Ready) {
    idxFree = 1;
    _currentReadyIdx = RENDER3D_RESOURCE_INDEX_NONE;
  }

  // mark the buffer as in-use
  if (idxFree != RENDER3D_RESOURCE_INDEX_NONE) {
    // Free the previously used buffer before switching to a new one
    if (_currentUsageIdx != RENDER3D_RESOURCE_INDEX_NONE) {
      _state[_currentUsageIdx] = AsyncReadState_Free;
    }

    _state[idxFree] = AsyncReadState_Using;
    _currentUsageIdx = idxFree;
  }

  return idxFree;
}

void MetalRenderColorOut::UnbindRenderer(const size_t idxRead) {
  if ((idxRead > 1) || (this->_state[idxRead] == AsyncReadState_Disabled)) {
    printf("Metal 3D ColorOut: UnbindRenderer - invalid index or disabled (idx=%zu)\n", idxRead);
    return;
  }

  // Call base class to update state
  this->Render3DColorOut::UnbindRenderer(idxRead);

  // printf("Metal 3D ColorOut: UnbindRenderer - reading back framebuffer (idx=%zu)\n", idxRead);

  // Copy texture data from GPU memory to CPU accessible buffer
  if (_colorTexture != nil && _readbackBuffer[idxRead] != nullptr) {
    @autoreleasepool {
      // Metal texture dimensions
      MTLRegion region =
          MTLRegionMake2D(0, 0, _framebufferWidth, _framebufferHeight);

      // Bytes per row in the destination buffer
      NSUInteger bytesPerRow = _framebufferWidth * sizeof(Color4u8);

      // Copy the texture data from the GPU memory to the CPU accessible
      // buffer
      [_colorTexture getBytes:_readbackBuffer[idxRead]
                  bytesPerRow:bytesPerRow
                   fromRegion:region
                  mipmapLevel:0];
      

      // Perform color format conversion if needed (Metal RGBA -> DS BGR)
      // The format is set by the base class SetColorFormat() method
      if (this->_format == NDSColorFormat_BGR666_Rev) {
        _ConvertColorFormat(_readbackBuffer[idxRead], _framebufferPixelCount);
      } else if (this->_format == NDSColorFormat_BGR888_Rev) {
        // BGR888_Rev format - Metal outputs RGBA8, display expects BGR8
        // We need to swap R and B channels (RGBA -> BGRA)
        ColorspaceCopyBuffer32<true, false>((u32 *)_readbackBuffer[idxRead], 
                                           (u32 *)_readbackBuffer[idxRead],
                                           _framebufferPixelCount);
      }
    }
  }
}

Render3DError MetalRenderColorOut::SetSize(size_t w, size_t h) {
  if (w == 0 || h == 0) {
    return RENDER3DERROR_INVALID_VALUE;
  }

  // Check if the new size is the same as the current size
  if ((w == this->_framebufferWidth) && (h == this->_framebufferHeight)) {
    return RENDER3DERROR_NOERR;
  }

  // Calculate new dimensions (but don't update member variables yet)
  size_t newPixelCount = w * h;
  size_t newSize32 = w * h * sizeof(Color4u8);

  // Attempt to allocate new buffer BEFORE modifying any state
  // This ensures strong exception safety: if allocation fails, object is
  // unchanged
  Color4u8 *newMasterBuffer32 = (Color4u8 *)malloc_aligned(newSize32 * 2, 64);

  if (newMasterBuffer32 == nullptr) {
    // Allocation failed - return error without modifying any member
    // variables
    NSLog(@"MetalRenderColorOut: Failed to allocate buffer for size %zux%zu", w,
          h);
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Allocation succeeded - now safe to update all state
  Color4u8 *oldMasterBuffer32 = _masterBuffer32;

  // Update dimension member variables only after successful allocation
  _framebufferWidth = w;
  _framebufferHeight = h;
  _framebufferPixelCount = newPixelCount;
  _framebufferSize32 = newSize32;

  // Update the buffer pointers
  _masterBuffer32 = newMasterBuffer32;
  _readbackBuffer[0] = newMasterBuffer32;
  _readbackBuffer[1] = newMasterBuffer32 + _framebufferPixelCount;
  _buffer32[0] = _readbackBuffer[0];
  _buffer32[1] = _readbackBuffer[1];

  // Clear the new master buffer
  memset(_masterBuffer32, 0, _framebufferSize32 * 2);

  // Free the old master buffer
  if (oldMasterBuffer32 != nullptr) {
    free_aligned(oldMasterBuffer32);
  }

  NSLog(@"MetalRenderColorOut resized to: %zu x %zu", this->_framebufferWidth,
        this->_framebufferHeight);
  return RENDER3DERROR_NOERR;
}

const Color4u8 *MetalRenderColorOut::GetFramebuffer32() const {
  // Return buffer that the emulator is currently using
  if (_currentReadingIdx32 != RENDER3D_RESOURCE_INDEX_NONE) {
    if (_state[_currentReadingIdx32] == AsyncReadState_Reading) {
      return _buffer32[_currentReadingIdx32];
    }
  } // Or return the buffer that the renderer is currently using
  else if (_currentUsageIdx != RENDER3D_RESOURCE_INDEX_NONE) {
    if (_state[_currentUsageIdx] == AsyncReadState_Using) {
      return _buffer32[_currentUsageIdx];
    }
  } // Otherwise, return nullptr

  return nullptr;
}

// Fill the framebuffer with zero
Render3DError MetalRenderColorOut::FillZero() {
  if (_masterBuffer32 != nullptr) {
    memset(_masterBuffer32, 0, this->_framebufferSize32 * 2);
  }
  return RENDER3DERROR_NOERR;
}

// Fill the framebuffer with a color
Render3DError MetalRenderColorOut::FillColor32(const Color4u8 *src,
                                               const bool isSrcNativeSize) {
  Render3DError error = RENDER3DERROR_NOERR;

  const u32 *__restrict src32 = (const u32 *__restrict)src;
  u32 *__restrict mutableFramebuffer32 =
      (u32 *__restrict)this->GetInUseFramebuffer32();

  if ((src32 == NULL) || (mutableFramebuffer32 == NULL)) {
    error = RENDER3DERROR_INVALID_BUFFER;
    return error;
  }

  // Ensure rendering is complete before modifying framebuffer
  if (this->_renderer != NULL) {
    this->_renderer->RenderFinish();
    this->_renderer->RenderFlush(false, false);
    this->_renderer->SetRenderNeedsFinish(false);
  }

  const size_t w = this->_framebufferWidth;
  const size_t h = this->_framebufferHeight;
  const bool isDstNativeSize = ((w == GPU_FRAMEBUFFER_NATIVE_WIDTH) &&
                                (h == GPU_FRAMEBUFFER_NATIVE_HEIGHT));

  if (isSrcNativeSize) {
    if (isDstNativeSize) {
      // Both source and destination are native size - direct copy or convert
      if (this->_format == NDSColorFormat_BGR666_Rev) {
        // Need R/B swap: Metal RGBA -> DS BGR
        ColorspaceConvertBuffer8888To6665<true, false>(
            src32, mutableFramebuffer32,
            GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT);
      } else {
        ColorspaceCopyBuffer32<false, false>(src32, mutableFramebuffer32,
                                             GPU_FRAMEBUFFER_NATIVE_WIDTH *
                                                 GPU_FRAMEBUFFER_NATIVE_HEIGHT);
      }
    } else {
      // Source is native size, destination is custom size - need to expand
      if (this->_format == NDSColorFormat_BGR666_Rev) {
        // Convert in-place first, then expand. Need R/B swap: Metal RGBA -> DS BGR
        ColorspaceConvertBuffer8888To6665<true, false>(
            src32, (u32 *)src32,
            GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT);
      }

      // Expand each line using the GPU line info
      for (size_t l = 0; l < GPU_FRAMEBUFFER_NATIVE_HEIGHT; l++) {
        const GPUEngineLineInfo &lineInfo = GPU->GetLineInfoAtIndex(l);
        CopyLineExpandHinted<0x3FFF, true, false, false, 4>(
            lineInfo, src32, mutableFramebuffer32);
        src32 += GPU_FRAMEBUFFER_NATIVE_WIDTH;
        mutableFramebuffer32 += lineInfo.pixelCount;
      }
    }
  } else {
    // Source is already at custom size - direct copy
    memcpy(mutableFramebuffer32, src32, this->_framebufferSize32);
  }

  return error;
}

void MetalRenderColorOut::SetColorTexture(MTLTexturePtr texture) {
  _colorTexture = texture;
}

void MetalRenderColorOut::_ConvertColorFormat(Color4u8 *buffer,
                                              size_t pixelCount) {
  // The Nintendo DS uses BGR666_Rev format for the framebuffer
  // (6 bits per RGB channel, 5 bits for alpha)
  // Metal uses RGBA8Unorm format (8 bits per channel)
  // Testing shows NO R/B swap gives correct colors (display handles BGR ordering)
  // But bit depth reduction (8888 -> 6665) makes colors too dark
  
  // Convert without R/B swap - just reduce bit depth
  ColorspaceConvertBuffer8888To6665<false, false>((u32 *)buffer, (u32 *)buffer,
                                                  pixelCount);
}

// Creation functions
Render3D* MetalRendererCreate()
{
    // Check if shared resources are available
    if (metal_getSharedDevice() == nil) {
        printf("Metal 3D: ERROR - No shared Metal resources. "
               "Metal display must be initialized first.\n");
        return nullptr;
    }
    
    // Two-phase initialization pattern (matches OpenGL renderer):
    // 1. Create object (constructor never fails)
    // 2. Initialize resources (can fail, returns error code)
    // 3. On failure, delete and return NULL
    MetalRender *newRenderer = new MetalRender();
    
    Render3DError error = newRenderer->InitResources();
    if (error != RENDER3DERROR_NOERR) {
        delete newRenderer;
        return nullptr;
    }
    
    return newRenderer;
}

void MetalRendererDestroy()
{
    if (CurrentRenderer == BaseRenderer)
    {
        return;
    }
    
    Render3DBaseDestroy();
    
    // Release shared resources reference
    @autoreleasepool {
        if (SharedMetalData != nil)
        {
            [(id)SharedMetalData release];
            SharedMetalData = nil;
        }
    }
}

// GPU3DInterface definition for Metal renderer
GPU3DInterface gpu3DMetal = {"Metal", MetalRendererCreate, MetalRendererDestroy};
