#include <Metal/Metal.h>

#include "MetalRender.h"

// The emulator seems to call the renderer in this sequence:
// Render(renderState, renderGList)
//   -> BeginRender // setup for this frame
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

MetalRender::MetalRender() {
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
}

MetalRender::~MetalRender() {
  // Clean up - ARC will handle the rest
  _device = nil;
  _commandQueue = nil;
}

Render3DError MetalRender::ClearFramebuffer(const GFX3D_State &renderState) {
  return RENDER3DERROR_NOERR;
}

Render3DError
MetalRender::ApplyRenderingSettings(const GFX3D_State &renderState) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::Reset() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::RenderPowerOff() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::Render(const GFX3D_State &renderState,
                                  const GFX3D_GeometryList &renderGList) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::RenderFinish() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::RenderFlush(bool willFlushBuffer32,
                                       bool willFlushBuffer16) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::SetFramebufferSize(size_t w, size_t h) {
  return RENDER3DERROR_NOERR;
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

Render3DError MetalRender::BeginRender(const GFX3D_State &renderState,
                                       const GFX3D_GeometryList &renderGList) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::PostprocessFramebuffer() {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::EndRender() { return RENDER3DERROR_NOERR; }

Render3DError MetalRender::ClearUsingImage(const u16 *__restrict colorBuffer,
                                           const u32 *__restrict depthBuffer,
                                           const u8 *__restrict fogBuffer,
                                           const u8 opaquePolyID) {
  return RENDER3DERROR_NOERR;
}

Render3DError
MetalRender::ClearUsingValues(const Color4u8 &clearColor6665,
                              const FragmentAttributes &clearAttributes) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::SetupTexture(const POLY &thePoly,
                                        size_t polyRenderIndex) {
  return RENDER3DERROR_NOERR;
}

Render3DError MetalRender::SetupViewport(const GFX3D_Viewport viewport) {
  return RENDER3DERROR_NOERR;
}
