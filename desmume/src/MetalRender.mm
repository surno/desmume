#include <Metal/Metal.h>
#include <cstddef>

#include "MetalRender.h"
#include "render3D.h"
#include "types.h"

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

MetalRender::MetalRender()
    : _device(nil), _commandQueue(nil), _commandBuffer(nil),
      _pipelineState(nil), _renderCommandEncoder(nil),
      _depthStencilStateOpaque(nil), _depthStencilStateDepthEqual(nil),
      _depthStencilStateTranslucent(nil),
      _depthStencilStateTranslucentDepthWrite(nil),
      _depthStencilStateShadowPass1(nil), _depthStencilStateShadowPass2(nil),
      _vertexBuffer(nil), _indexBuffer(nil), _colorTexture(nil),
      _depthTexture(nil), _renderPassDescriptor(nil),
      _enableAlphaBlending(false), _samplerStateClampNearest(nil),
      _samplerStateClampLinear(nil), _samplerStateRepeatNearest(nil),
      _samplerStateRepeatLinear(nil) {
  // Initialize device info
  _deviceInfo.renderID = RENDERID_METAL;
  _deviceInfo.renderName = "Metal";

  _device = MTLCreateSystemDefaultDevice();
  if (_device == nil) {
    throw std::runtime_error("Failed to create Metal device");
  }

  _commandQueue = [_device newCommandQueue];
  if (_commandQueue == nil) {
    throw std::runtime_error("Failed to create Metal command queue");
  }

  if (InitializePipelineState() != RENDER3DERROR_NOERR) {
    throw std::runtime_error("Failed to initialize pipeline state");
  }

  if (InitializeDepthStencilState() != RENDER3DERROR_NOERR) {
    throw std::runtime_error("Failed to initialize depth/stencil state");
  }

  // Initialize sampler states (sets all 4 sampler state pointers)
  if (InitializeSamplerState() != RENDER3DERROR_NOERR) {
    throw std::runtime_error("Failed to initialize sampler state");
  }
}

MetalRender::~MetalRender() {
  // Clean up - ARC will handle the rest
  _device = nil;
  _commandQueue = nil;
  _commandBuffer = nil;
  _pipelineState = nil;
  _renderCommandEncoder = nil;
  _depthStencilStateOpaque = nil;
  _depthStencilStateDepthEqual = nil;
  _depthStencilStateTranslucent = nil;
  _depthStencilStateTranslucentDepthWrite = nil;
  _depthStencilStateShadowPass1 = nil;
  _depthStencilStateShadowPass2 = nil;
  _vertexBuffer = nil;
  _indexBuffer = nil;
  _colorTexture = nil;
  _depthTexture = nil;
  _renderPassDescriptor = nil;
  _samplerStateClampNearest = nil;
  _samplerStateClampLinear = nil;
  _samplerStateRepeatNearest = nil;
  _samplerStateRepeatLinear = nil;
}

Render3DError
MetalRender::ApplyRenderingSettings(const GFX3D_State &renderState) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::Reset() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::RenderPowerOff() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::RenderFinish() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::RenderFlush(bool willFlushBuffer32,
                                       bool willFlushBuffer16) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::SetFramebufferSize(size_t w, size_t h) {
  return InitializeRenderTargets(w, h);
}

NDSColorFormat MetalRender::RequestColorFormat(NDSColorFormat colorFormat) {
  return colorFormat;
}

Render3DError MetalRender::FillZero() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::FillColor32(const Color4u8 *__restrict src,
                                       const bool isSrcNativeSize) {
  return RENDER3DERROR_NOERR;
}

ClipperMode MetalRender::GetPreferredPolygonClippingMode() const {
  return ClipperMode::ClipperMode_Full;
}

void MetalRender::_ClearImageBaseLoop(const u16 *__restrict inColor16,
                                      const u16 *__restrict inDepth16,
                                      u16 *__restrict outColor16,
                                      u32 *__restrict outDepth24,
                                      u8 *__restrict outFog) {
  return;
}

template <bool ISCOLORBLANK, bool ISDEPTHBLANK>
void MetalRender::_ClearImageScrolledLoop(const u8 xScroll, const u8 yScroll,
                                          const u16 *__restrict inColor16,
                                          const u16 *__restrict inDepth16,
                                          u16 *__restrict outColor16,
                                          u32 *__restrict outDepth24,
                                          u8 *__restrict outFog) {
  return;
}

Render3DError MetalRender::InitializePipelineState() {
  // Load the default Metal library, as this will automatically load the
  // shaders.
  id<MTLLibrary> defaultLibrary = [_device newDefaultLibrary];
  if (defaultLibrary == nil) {
    return RENDER3DERROR_INVALID_BINDING;
  }

  // Get the vertex function from the library
  id<MTLFunction> vertexFunction =
      [defaultLibrary newFunctionWithName:@"vertexShader"];
  if (vertexFunction == nil) {
    return RENDER3DERROR_INVALID_BINDING;
  }

  // Get the fragment function from the library
  id<MTLFunction> fragmentFunction =
      [defaultLibrary newFunctionWithName:@"fragmentShaderTextured"];
  if (fragmentFunction == nil) {
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

  // Layout the vertex data
  vertexDescriptor.layouts[0].stride = sizeof(NDSVertex);
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
  pipelineDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

  pipelineDesc.stencilAttachmentPixelFormat = MTLPixelFormatStencil8;

  // create the pipeline state
  NSError *error = nil;
  _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                           error:&error];
  if (_pipelineState == nil) {
    if (error != nil) {
      NSLog(@"Error creating pipeline state: %@", error.localizedDescription);
    }
    return RENDER3DERROR_INVALID_BINDING;
  }

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::InitializeDepthStencilState() {
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
  translucentDesc.depthCompareFunction = MTLCompareFunctionLess;
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
  translucentDepthWriteDesc.depthCompareFunction = MTLCompareFunctionLess;
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
}

Render3DError MetalRender::BeginRender(const GFX3D_State &renderState,
                                       const GFX3D_GeometryList &renderGList) {
  // Store the geometry list
  _clippedPolyCount = renderGList.clippedPolyCount;
  _clippedPolyOpaqueCount = renderGList.clippedPolyOpaqueCount;
  _clippedPolyList = (CPoly *)renderGList.clippedPolyList;
  _rawPolyList = (POLY *)renderGList.rawPolyList;

  // Store the render state
  _enableAlphaBlending =
      (renderState.DISP3DCNT.EnableAlphaBlending) ? true : false;

  // If there isn't anything to render, return early
  if (_clippedPolyCount == 0) {
    return RENDER3DERROR_NOERR;
  }

  // Calculate the buffer size, need space for all vertices
  size_t vertexBufferSize = renderGList.rawVertCount * sizeof(NDSVertex);

  // Need space for index buffer, quads will be converted to triangles.
  // Estimate: assume all quads, so 6 indices per quad instead of 4.
  size_t maxIndexCount =
      renderGList.rawVertCount * 2; // Very conservative estimate.
  size_t indexBufferSize = maxIndexCount * sizeof(uint16_t);

  // Create or update the vertex buffer
  if (_vertexBuffer == nil || vertexBufferSize > [_vertexBuffer length]) {
    // Set buffer to use unified memory, so it can be shared with the CPU
    _vertexBuffer = [_device newBufferWithLength:vertexBufferSize
                                         options:MTLResourceStorageModeShared];
    if (_vertexBuffer == nil) {
      return RENDER3DERROR_INVALID_BUFFER;
    }
  }

  // Upload data to the Metal buffer
  memcpy([_vertexBuffer contents], renderGList.rawVtxList, vertexBufferSize);

  // Now, create the index buffer
  if (_indexBuffer == nil || indexBufferSize > [_indexBuffer length]) {
    _indexBuffer = [_device newBufferWithLength:indexBufferSize
                                        options:MTLResourceStorageModeShared];
    if (_indexBuffer == nil) {
      return RENDER3DERROR_INVALID_BUFFER;
    }
  }

  // Build the index buffer
  // Need to convert all polygon types to triangles.
  u16 *indexPtr = (u16 *)[_indexBuffer contents];
  size_t indexCount = 0;

  for (size_t i = 0; i < _clippedPolyCount; i++) {
    const CPoly &cPoly = _clippedPolyList[i];
    const POLY &rawPoly = _rawPolyList[cPoly.index];
    const size_t polyType = rawPoly.type; // number of vertices in the polygon

    // Convert quads to triangles
    // Quad (0, 1, 2, 3) -> Triangle (0, 1, 2) and Triangle (0, 2, 3)
    if (!GFX3D_IsPolyWireframe(rawPoly) && polyType == 4 &&
        (rawPoly.vtxFormat == GFX3D_QUADS ||
         rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
      // First triangle: 0, 1, 2
      indexPtr[indexCount++] = rawPoly.vertIndexes[0];
      indexPtr[indexCount++] = rawPoly.vertIndexes[1];
      indexPtr[indexCount++] = rawPoly.vertIndexes[2];
      // Second triangle: 0, 2, 3
      indexPtr[indexCount++] = rawPoly.vertIndexes[0];
      indexPtr[indexCount++] = rawPoly.vertIndexes[2];
      indexPtr[indexCount++] = rawPoly.vertIndexes[3];
    } else {
      // Triangles and other primitives: add vertices as-is
      for (size_t j = 0; j < polyType; j++) {
        const u16 vertIndex = rawPoly.vertIndexes[j];
        indexPtr[indexCount++] = vertIndex;
      }
    }

    // Store this texture for the polygon
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

Render3DError MetalRender::RenderGeometry() {
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
  [_renderCommandEncoder setVertexBuffer:_vertexBuffer offset:0 atIndex:0];

  // The emulator separates polygons into two groups:
  // 1. Opaque polygons (0 to _clippedPolyOpaqueCount-1)
  // 2. Translucent polygons (_clippedPolyOpaqueCount to _clippedPolyCount-1)

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

      // set up the texture for this polygon
      this->SetupTexture(rawPoly, i);

      // set up the viewport for this polygon
      this->SetupViewport(rawPoly.viewport);

      // determine the number of indices to draw for this polygon
      const size_t polyType = rawPoly.type; // number of vertices in the polygon
      size_t indexCount = polyType;

      // recall: quads were converted to triangles, so we need to draw the
      // polygon as a triangle
      if (!GFX3D_IsPolyWireframe(rawPoly) &&
          (rawPoly.vtxFormat == GFX3D_QUADS ||
           rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
        indexCount = 6; // 3 doe first triangle, 3 for the second triangle
      }

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
    // TODO: Update pipeline state for alpha blending, if needed.

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

      // set up the texture for this polygon
      this->SetupTexture(rawPoly, i);

      // set up the viewport for this polygon
      this->SetupViewport(rawPoly.viewport);

      // determine the number of indices to draw for this polygon
      const size_t polyType = rawPoly.type; // number of vertices in the polygon
      size_t indexCount = polyType;

      // recall: quads were converted to triangles, so we need to draw the
      // polygon as a triangle
      if (!GFX3D_IsPolyWireframe(rawPoly) &&
          (rawPoly.vtxFormat == GFX3D_QUADS ||
           rawPoly.vtxFormat == GFX3D_QUAD_STRIP)) {
        indexCount = 6; // 3 doe first triangle, 3 for the second triangle
      }

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

  // finish recording the render command encoder
  [_renderCommandEncoder endEncoding];

  _renderCommandEncoder = nil;

  // commit the command buffer
  [_commandBuffer commit];

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
  depthTexDescriptor.usage = MTLTextureUsageRenderTarget;
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

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::InitializeSamplerState() {
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

  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::PostprocessFramebuffer() {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::EndRender() { return RENDER3DERROR_NOERR; }

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
  // Get the metal texture for this polygon from the texture cache
  MetalTexture *theTexture = (MetalTexture *)_textureList[polyRenderIndex];

  if (theTexture == nil) {
    return RENDER3DERROR_INVALID_BUFFER;
  }

  // Check if the texture sampling is enabled
  // polygons can disable texturing even if the texture is loaded
  // (for flat-shaped polygons or when the texture format is TEXMODE_NONE)
  if (!theTexture->IsSamplingEnabled()) {
    return RENDER3DERROR_NOERR;
  }

  // Get the Metal Texture ID
  id<MTLTexture> texID = theTexture->GetTexID();
  if (texID == nil || !theTexture->IsTexInited()) {
    // We cannot bind the texture as it might not be initialized yet
    // texture loading might have failed
    return RENDER3DERROR_NOERR;
  }

  // Bind the texture to the fragment shader at binding point 0
  [_renderCommandEncoder setFragmentTexture:texID atIndex:0];

  // Select the appropriate sampler state based on the texture wrapping mode
  // The DS suppoets different texture wrapping modes for each axis:
  // 0: Clamp to edge (no repeat)
  // 1: Repeat
  // 2: Mirrored repeat
  // 3: Flip
  // The sampler state is stored in the texture object
  // For simplicity, we check if both S and T use the same wrap mode
  // A more complete implementation could create samplers for all combinations
  const bool repeatS = thePoly.texParam.RepeatS_Enable;
  const bool repeatT = thePoly.texParam.RepeatT_Enable;
  // Note: MirroredRepeat modes are available but not currently used in this
  // simplified implementation. A more complete implementation could create
  // additional samplers for mirrored repeat modes.

  // Determine if we should use repeat or clamp mode
  // If either axis repeats, we use repeat mode
  const bool useRepeat = (repeatS || repeatT);

  // Determine if we should use linear or nearest filtering
  // _enableTextureSmoothing comes from the emulator settings
  const bool useLinearFiltering = this->_enableTextureSmoothing;

  // Select the appropriate sampler
  id<MTLSamplerState> selectedSampler = nil;

  if (useRepeat) {
    if (useLinearFiltering) {
      selectedSampler = _samplerStateRepeatLinear;
    } else {
      selectedSampler = _samplerStateRepeatNearest;
    }
  } else {
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
  // Call base class to decode the texture from NDS format into our unpack
  // buffer The base class TextureStore::Load will call
  // Unpack<TexFormat_32bpp> which converts the NDS texture format to
  // RGBA8888 pixels in _unpackBuffer
  Render3DTexture::Load(_unpackBuffer);

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

  // Use the unpacked texture data from our buffer
  const u32 *texData = _unpackBuffer; // RGBA8888 format

  // Create Metal texture descriptor
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
