/*
	Copyright (C) 2006-2007 shash
	Copyright (C) 2007-2025 DeSmuME team

	This file is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with the this software.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef RENDER3D_H
#define RENDER3D_H

#include "types.h"
#include "gfx3d.h"
#include "texcache.h"
#include "./filter/filter.h"

#define kUnsetTranslucentPolyID 255
#define DEPTH_EQUALS_TEST_TOLERANCE 255

class Render3D;

typedef struct Render3DInterface
{
	const char *name;				// The name of the renderer.
	Render3D* (*NDS_3D_Init)();		// Called when the renderer is created.
	void (*NDS_3D_Close)();			// Called when the renderer is destroyed.
	
} GPU3DInterface;

extern int cur3DCore;

// gpu 3D core list, per port
extern GPU3DInterface *core3DList[];

// Default null plugin
extern GPU3DInterface gpu3DNull;

// Extern pointer
extern Render3D *BaseRenderer;
extern Render3D *CurrentRenderer;
extern GPU3DInterface *gpu3D;

Render3D* Render3DBaseCreate();
void Render3DBaseDestroy();

void Render3D_Init();
void Render3D_DeInit();

enum RendererID
{
	RENDERID_NULL           = 0,
	RENDERID_SOFTRASTERIZER = 1,
	RENDERID_OPENGL_AUTO    = 1000,
	RENDERID_OPENGL_LEGACY  = 1001,
	RENDERID_OPENGL_3_2     = 1002,
	RENDERID_OPENGL_ES      = 1003,
	RENDERID_METAL          = 2000
};

enum Render3DErrorCode
{
	RENDER3DERROR_NOERR           = 0,
	RENDER3DERROR_INVALID_VALUE   = 1,
	RENDER3DERROR_INVALID_BUFFER  = 2,
	RENDER3DERROR_INVALID_BINDING = 3
};
typedef int Render3DError;

enum PolyFacing
{
	PolyFacing_Unwritten = 0,
	PolyFacing_Front     = 1,
	PolyFacing_Back      = 2
};

struct FragmentAttributes
{
	u32 depth;
	u8 opaquePolyID;
	u8 translucentPolyID;
	u8 stencil;
	u8 isFogged;
	u8 isTranslucentPoly;
	u8 polyFacing;
};

struct FragmentAttributesBuffer
{
	size_t count;
	u32 *depth;
	u8 *opaquePolyID;
	u8 *translucentPolyID;
	u8 *stencil;
	u8 *isFogged;
	u8 *isTranslucentPoly;
	u8 *polyFacing;
	
	FragmentAttributesBuffer(size_t newCount);
	~FragmentAttributesBuffer();
	
	void SetAtIndex(const size_t index, const FragmentAttributes &attr);
};

struct Render3DDeviceInfo
{
	RendererID renderID;
	std::string renderName;
	
	bool isTexturingSupported;
	bool isEdgeMarkSupported;
	bool isFogSupported;
	bool isTextureSmoothingSupported;
	
	float maxAnisotropy;
	u8 maxSamples;
};

enum AsyncWriteState
{
	AsyncWriteState_Disabled = 0, // The resources at this index are not available for use.
	AsyncWriteState_Free     = 1, // The resources at this index have no owner, but are made available to the emulation.
	AsyncWriteState_Writing  = 2, // The emulation has taken ownership of the resources at this index for writing new data.
	AsyncWriteState_Ready    = 3, // The emulation has finished writing to the resources at this index. The renderer is allowed to take ownership at any time.
	AsyncWriteState_Using    = 4  // The renderer has taken ownership of the resources at this index for reading the data.
};

enum AsyncReadState
{
	AsyncReadState_Disabled = 0, // The resources at this index are not available for use.
	AsyncReadState_Free     = 1, // The resources at this index have no owner, but are made available to the renderer.
	AsyncReadState_Using    = 4, // The renderer has taken ownership of the resources at this index for writing new data.
	AsyncReadState_Ready    = 3, // The renderer has finished writing to the resources at this index. The emulation is allowed to take ownership at any time.
	AsyncReadState_Reading  = 2  // The emulation has taken ownership of the resources at this index for reading the data.
};

#define RENDER3D_RESOURCE_INDEX_NONE 0xFFFF

class Render3DResource
{
protected:
	AsyncWriteState _state[3];
	size_t _currentReadyIdx;
	size_t _currentUsingIdx;
	
public:
	Render3DResource();
	
	size_t BindWrite();
	void UnbindWrite(const size_t idxWrite);
	
	size_t BindUsage();
	size_t UnbindUsage();
};

class Render3DResourceGeometry : public Render3DResource
{
protected:
	NDSVertex *_vertexBuffer[3];
	size_t _rawVertexCount[3];
	size_t _clippedPolyCount[3];
	
public:
	Render3DResourceGeometry();
	
	size_t BindWrite(const size_t rawVtxCount, const size_t clippedPolyCount);
	NDSVertex* GetVertexBuffer(const size_t index);
};

class Render3DColorOut
{
protected:
	Render3D *_renderer;
	NDSColorFormat _format;
	
	AsyncReadState _state[2];
	Color5551 *_buffer16[2];
	Color4u8 *_buffer32[2];
	
	size_t _currentUsageIdx;
	size_t _currentReadyIdx;
	size_t _currentReadingIdx16;
	size_t _currentReadingIdx32;
	
	size_t _framebufferWidth;
	size_t _framebufferHeight;
	size_t _framebufferPixelCount;
	size_t _framebufferSize16;
	size_t _framebufferSize32;
	
public:
	Render3DColorOut();
	virtual ~Render3DColorOut() {};
	
	Render3D* GetRenderer() const;
	void SetRenderer(Render3D *theRenderer);
	
	NDSColorFormat GetColorFormat() const;
	void SetColorFormat(NDSColorFormat theFormat);
	
	virtual void Reset();
	
	virtual size_t BindRead16();
	virtual size_t UnbindRead16();
	
	virtual size_t BindRead32();
	virtual size_t UnbindRead32();
	
	virtual size_t BindRenderer();
	virtual void UnbindRenderer(const size_t idxRead);
	
	virtual Render3DError SetSize(size_t w, size_t h);
	virtual const Color5551* GetFramebuffer16() const;
	virtual const Color4u8* GetFramebuffer32() const;
	virtual Color5551* GetInUseFramebuffer16() const;
	virtual Color4u8* GetInUseFramebuffer32() const;
	
	virtual Render3DError FillZero();
	virtual Render3DError FillColor32(const Color4u8 *src, const bool isSrcNativeSize);
};

class Null3DColorOut : public Render3DColorOut
{
public:
	Null3DColorOut();
	virtual ~Null3DColorOut();
	
	virtual size_t BindRead16();
	virtual size_t UnbindRead16();
	
	virtual size_t BindRead32();
	virtual size_t UnbindRead32();
	
	virtual size_t BindRenderer();
	
	virtual Render3DError SetSize(size_t w, size_t h);
	virtual const Color5551* GetFramebuffer16() const;
	virtual const Color4u8* GetFramebuffer32() const;
	virtual Color5551* GetInUseFramebuffer16() const;
	virtual Color4u8* GetInUseFramebuffer32() const;
};

class Render3DTexture : public TextureStore
{
protected:
	bool _isSamplingEnabled;
	bool _useDeposterize;
	size_t _scalingFactor;
	SSurface _deposterizeSrcSurface;
	SSurface _deposterizeDstSurface;
	
	template<size_t SCALEFACTOR> void _Upscale(const u32 *__restrict src, u32 *__restrict dst);
	
public:
	Render3DTexture(TEXIMAGE_PARAM texAttributes, u32 palAttributes);
	
	bool IsSamplingEnabled() const;
	void SetSamplingEnabled(bool isEnabled);
		
	bool IsUsingDeposterize() const;
	void SetUseDeposterize(bool willDeposterize);
	
	size_t GetScalingFactor() const;
	void SetScalingFactor(size_t scalingFactor);
};

class Render3D
{
protected:
	Render3DDeviceInfo _deviceInfo;
	
	size_t _framebufferWidth;
	size_t _framebufferHeight;
	size_t _framebufferPixCount;
	size_t _framebufferSIMDPixCount;
	size_t _framebufferColorSizeBytes;
	
	Render3DColorOut *_colorOut;
	size_t _lastBoundColorOut;
	
	Color4u8 _clearColor6665;
	FragmentAttributes _clearAttributes;
	
	NDSColorFormat _internalRenderingFormat;
	NDSColorFormat _outputFormat;
	bool _renderNeedsFinish;
	bool _renderNeedsFlushMain;
	bool _renderNeedsFlush16;
	bool _isPoweredOn;
	
	bool _enableEdgeMark;
	bool _enableFog;
	bool _enableTextureSampling;
	bool _enableTextureDeposterize;
	bool _enableTextureSmoothing;
	size_t _textureScalingFactor;
	
	bool _prevEnableTextureSampling;
	bool _prevEnableTextureDeposterize;
	size_t _prevTextureScalingFactor;
	
	SSurface _textureDeposterizeSrcSurface;
	SSurface _textureDeposterizeDstSurface;
	
	u32 *_textureUpscaleBuffer;
	Render3DTexture *_textureList[CLIPPED_POLYLIST_SIZE];
	
	size_t _clippedPolyCount;
	size_t _clippedPolyOpaqueCount;
	CPoly *_clippedPolyList;
	POLY *_rawPolyList;
	
	CACHE_ALIGN u16 clearImageColor16Buffer[GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT];
	CACHE_ALIGN u32 clearImageDepthBuffer[GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT];
	CACHE_ALIGN u8 clearImageFogBuffer[GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT];
	
	virtual void _ClearImageBaseLoop(const u16 *__restrict inColor16, const u16 *__restrict inDepth16, u16 *__restrict outColor16, u32 *__restrict outDepth24, u8 *__restrict outFog);
	template<bool ISCOLORBLANK, bool ISDEPTHBLANK> void _ClearImageScrolledLoop(const u8 xScroll, const u8 yScroll, const u16 *__restrict inColor16, const u16 *__restrict inDepth16,
																				u16 *__restrict outColor16, u32 *__restrict outDepth24, u8 *__restrict outFog);
	
	
	virtual Render3DError BeginRender(const GFX3D_State &renderState, const GFX3D_GeometryList &renderGList);
	virtual Render3DError RenderGeometry();
	virtual Render3DError PostprocessFramebuffer();
	virtual Render3DError EndRender();
	
	virtual Render3DError ClearUsingImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 opaquePolyID);
	virtual Render3DError ClearUsingValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
	virtual Render3DError SetupTexture(const POLY &thePoly, size_t polyRenderIndex);
	virtual Render3DError SetupViewport(const GFX3D_Viewport viewport);
	
public:
	static void* operator new(size_t size);
	static void operator delete(void *p);
	Render3D();
	virtual ~Render3D();
	
	const Render3DDeviceInfo& GetDeviceInfo();
	RendererID GetRenderID();
	std::string GetName();
	
	size_t GetFramebufferWidth();
	size_t GetFramebufferHeight();
	bool IsFramebufferNativeSize();
	
	virtual Render3DError ClearFramebuffer(const GFX3D_State &renderState);
	
	virtual Render3DError ApplyRenderingSettings(const GFX3D_State &renderState);
	
	virtual Render3DError Reset();						// Called when the emulator resets.
	
	virtual Render3DError RenderPowerOff();				// Called when the renderer needs to handle a power-off condition by clearing its framebuffers.
	
	virtual Render3DError Render(const GFX3D_State &renderState, const GFX3D_GeometryList &renderGList);	// Called whenever the 3D renderer needs to render the geometry lists.
	
	virtual Render3DError RenderFinish();				// Called whenever 3D rendering needs to finish. This function should block the calling thread
														// and only release the block when 3D rendering is finished. (Before reading the 3D layer, be
														// sure to always call this function.)
	
	virtual Render3DError RenderFlush(bool willFlushBuffer32, bool willFlushBuffer16);	// Called whenever the emulator needs the flushed results of the 3D renderer. Before calling this,
																						// the 3D renderer must be finished using RenderFinish() or confirmed already finished using
																						// GetRenderNeedsFinish().
	
	virtual Render3DError VramReconfigureSignal();		// Called when the emulator reconfigures its VRAM. You may need to invalidate your texture cache.
	
	virtual Render3DError SetFramebufferSize(size_t w, size_t h);	// Called whenever the output framebuffer size changes.
	
	virtual NDSColorFormat RequestColorFormat(NDSColorFormat colorFormat);	// Called whenever the output framebuffer color format changes. The framebuffer
																			// output by the 3D renderer is expected to match the requested format. If the
																			// internal color format of the 3D renderer doesn't natively match the requested
																			// format, then a colorspace conversion will be required in order to match. The
																			// only exception to this rule is if the requested output format is RGBA5551. In
																			// this particular case, the 3D renderer is expected to output a framebuffer in
																			// RGBA6665 color format. Again, if the internal color format does not match this,
																			// then a colorspace conversion will be required for RGBA6665.
	
	virtual NDSColorFormat GetColorFormat() const;							// The output color format of the 3D renderer.
	
	virtual Render3DError FillZero();
	virtual Render3DError FillColor32(const Color4u8 *__restrict src, const bool isSrcNativeSize);
	
	const Color5551* GetFramebuffer16() const;
	const Color4u8* GetFramebuffer32() const;
	Color4u8* GetInUseFramebuffer32() const;
	
	bool GetRenderNeedsFinish() const;
	void SetRenderNeedsFinish(const bool renderNeedsFinish);
	
	bool GetRenderNeedsFlushMain() const;
	bool GetRenderNeedsFlush16() const;
	
	void SetTextureProcessingProperties();
	Render3DTexture* GetTextureByPolygonRenderIndex(size_t polyRenderIndex) const;
	
	virtual ClipperMode GetPreferredPolygonClippingMode() const;
	const CPoly& GetClippedPolyByIndex(size_t index) const;
	size_t GetClippedPolyCount() const;
	
	const POLY* GetRawPolyList() const;
};

template <size_t SIMDBYTES>
class Render3D_SIMD : public Render3D
{
public:
	Render3D_SIMD();
	virtual ~Render3D_SIMD() {}
	
	virtual Render3DError SetFramebufferSize(size_t w, size_t h);
};

#if defined(ENABLE_AVX2)

class Render3D_AVX2 : public Render3D_SIMD<32>
{
public:
	virtual ~Render3D_AVX2() {}
	virtual void _ClearImageBaseLoop(const u16 *__restrict inColor16, const u16 *__restrict inDepth16, u16 *__restrict outColor16, u32 *__restrict outDepth24, u8 *__restrict outFog);
};

#elif defined(ENABLE_SSE2)

class Render3D_SSE2 : public Render3D_SIMD<16>
{
public:
	virtual ~Render3D_SSE2() {}
	virtual void _ClearImageBaseLoop(const u16 *__restrict inColor16, const u16 *__restrict inDepth16, u16 *__restrict outColor16, u32 *__restrict outDepth24, u8 *__restrict outFog);
};

#elif defined(ENABLE_NEON_A64)

class Render3D_NEON : public Render3D_SIMD<16>
{
public:
	virtual ~Render3D_NEON() {}
	virtual void _ClearImageBaseLoop(const u16 *__restrict inColor16, const u16 *__restrict inDepth16, u16 *__restrict outColor16, u32 *__restrict outDepth24, u8 *__restrict outFog);
};

#elif defined(ENABLE_ALTIVEC)

class Render3D_AltiVec : public Render3D_SIMD<16>
{
public:
	virtual ~Render3D_AltiVec() {}
	virtual void _ClearImageBaseLoop(const u16 *__restrict inColor16, const u16 *__restrict inDepth16, u16 *__restrict outColor16, u32 *__restrict outDepth24, u8 *__restrict outFog);
};

#endif

#endif // RENDER3D_H
