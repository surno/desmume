/*
	Copyright (C) 2009-2025 DeSmuME team

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

#ifndef _RASTERIZE_H_
#define _RASTERIZE_H_

#include "render3D.h"
#include "gfx3d.h"


#define SOFTRASTERIZER_MAX_THREADS 32

extern GPU3DInterface gpu3DRasterize;

class Task;
class SoftRasterizerRenderer;
struct edge_fx_fl;

struct SoftRasterizerPrecalculation
{
	Vector2s64 positionCeil;
	
	s64 zPosition;
	s64 invWPosition;
	Vector2s64 texCoord;
	Color3s64 color;
	s64 yPrestep;
	
	float zPositionNormalized;
	float invWPositionNormalized;
	Vector2f32 texCoordNormalized;
	Color3f32 colorNormalized;
	float yPrestepNormalized;
};
typedef struct SoftRasterizerPrecalculation SoftRasterizerPrecalculation;

struct SoftRasterizerClearParam
{
	SoftRasterizerRenderer *renderer;
	size_t startPixel;
	size_t endPixel;
};

struct SoftRasterizerPostProcessParams
{
	SoftRasterizerRenderer *renderer;
	size_t startLine;
	size_t endLine;
	bool enableEdgeMarking;
	bool enableFog;
	u32 fogColor;
	bool fogAlphaOnly;
};

class SoftRasterizerColorOut : public Render3DColorOut
{
protected:
	void *_masterBuffer;
	
public:
	SoftRasterizerColorOut(size_t w, size_t h);
	virtual ~SoftRasterizerColorOut();
	
	virtual size_t BindRead32();
	virtual Render3DError SetSize(size_t w, size_t h);
	
	virtual Render3DError FillZero();
	virtual Render3DError FillColor32(const Color4u8 *src, const bool isSrcNativeSize);
};

class SoftRasterizerTexture : public Render3DTexture
{
private:
	void _clamp(s32 &val, const int size, const s32 sizemask) const;
	void _hclamp(s32 &val) const;
	void _vclamp(s32 &val) const;
	void _repeat(s32 &val, const int size, const s32 sizemask) const;
	void _hrepeat(s32 &val) const;
	void _vrepeat(s32 &val) const;
	void _flip(s32 &val, const int size, const s32 sizemask) const;
	void _hflip(s32 &val) const;
	void _vflip(s32 &val) const;
	
protected:
	u32 *_unpackData;
	u32 *_customBuffer;
	
	u32 *_renderData;
	s32 _renderWidth;
	s32 _renderHeight;
	s32 _renderWidthMask;
	s32 _renderHeightMask;
	u32 _renderWidthShift;
	
public:
	SoftRasterizerTexture(TEXIMAGE_PARAM texAttributes, u32 palAttributes);
	virtual ~SoftRasterizerTexture();
	
	virtual void Load();
	
	u32* GetUnpackData();
	
	u32* GetRenderData();
	s32 GetRenderWidth() const;
	s32 GetRenderHeight() const;
	s32 GetRenderWidthMask() const;
	s32 GetRenderHeightMask() const;
	u32 GetRenderWidthShift() const;
	
	void GetRenderSamplerCoordinates(const u8 wrapMode, Vector2s32 &sampleCoordInOut) const;
	
	void SetUseDeposterize(bool willDeposterize);
	void SetScalingFactor(size_t scalingFactor);
};

template <bool RENDERER>
class RasterizerUnit
{
protected:
	bool _debug_thisPoly;
	s32 _SLI_startLine;
	s32 _SLI_endLine;
	
	SoftRasterizerRenderer *_softRender;
	SoftRasterizerTexture *_currentTexture;
	const NDSVertex *_currentVtx[MAX_CLIPPED_VERTS];
	const SoftRasterizerPrecalculation *_currentPrecalc[MAX_CLIPPED_VERTS];
	u8 _textureWrapMode;
	
	Render3DError _SetupTexture(const POLY &thePoly, size_t polyRenderIndex);
	FORCEINLINE Color4u8 _sample(const Vector2s32 &texCoord);
	FORCEINLINE float _round_s(const float val);
	
	template<bool ISSHADOWPOLYGON> FORCEINLINE void _shade(const PolygonMode polygonMode, const Color4u8 vtxColor, const Vector2s32 &texCoord, Color4u8 &outColor);
	template<bool ISFRONTFACING, bool ISSHADOWPOLYGON> FORCEINLINE void _pixel(const POLYGON_ATTR polyAttr, const bool isPolyTranslucent, const u32 depth, const Color4u8 &vtxColor, const Vector2s32 texCoord, const size_t fragmentIndex, Color4u8 &dstColor);
	template<bool ISFRONTFACING, bool ISSHADOWPOLYGON, bool USELINEHACK> FORCEINLINE void _drawscanline(const POLYGON_ATTR polyAttr, const bool isPolyTranslucent, Color4u8 *dstColor, const size_t framebufferWidth, const size_t framebufferHeight, const edge_fx_fl &pLeft, const edge_fx_fl &pRight);
	template<bool SLI, bool ISFRONTFACING, bool ISSHADOWPOLYGON, bool USELINEHACK> void _runscanlines(const POLYGON_ATTR polyAttr, const bool isPolyTranslucent, Color4u8 *dstColor, const size_t framebufferWidth, const size_t framebufferHeight, const bool isHorizontal, edge_fx_fl &left, edge_fx_fl &right);
	
	template<int TYPE> FORCEINLINE void _rot_verts();
	template<bool ISFRONTFACING, int TYPE> void _sort_verts();
	template<bool SLI, bool ISFRONTFACING, bool ISSHADOWPOLYGON, bool USELINEHACK> void _shape_engine(const POLYGON_ATTR polyAttr, const bool isPolyTranslucent, Color4u8 *dstColor, const size_t framebufferWidth, const size_t framebufferHeight, int type);
	
public:
	void SetSLI(const size_t startLine, const size_t endLine, bool debug);
	void SetRenderer(SoftRasterizerRenderer *theRenderer);
	template<bool SLI, bool USELINEHACK> FORCEINLINE void Render();
};

#if defined(ENABLE_AVX2)
class SoftRasterizerRenderer : public Render3D_AVX2
#elif defined(ENABLE_SSE2)
class SoftRasterizerRenderer : public Render3D_SSE2
#elif defined(ENABLE_NEON_A64)
class SoftRasterizerRenderer : public Render3D_NEON
#elif defined(ENABLE_ALTIVEC)
class SoftRasterizerRenderer : public Render3D_AltiVec
#else
class SoftRasterizerRenderer : public Render3D
#endif
{
private:
	void __InitTables();
	
protected:
	Task *_task;
	SoftRasterizerClearParam _threadClearParam[SOFTRASTERIZER_MAX_THREADS];
	SoftRasterizerPostProcessParams _threadPostprocessParam[SOFTRASTERIZER_MAX_THREADS];
	
	RasterizerUnit<true> _rasterizerUnit[SOFTRASTERIZER_MAX_THREADS];
	RasterizerUnit<false> _HACK_viewer_rasterizerUnit;
	
	size_t _threadCount;
	size_t _nativeLinesPerThread;
	size_t _nativePixelsPerThread;
	size_t _customLinesPerThread;
	size_t _customPixelsPerThread;
	
	SoftRasterizerPrecalculation *_precalc;
	
	u8 _fogTable[32768];
	Color4u8 _edgeMarkTable[8];
	bool _edgeMarkDisabled[8];
	
	bool _renderGeometryNeedsFinish;
	
	bool _enableHighPrecisionColorInterpolation;
	bool _enableLineHack;
	
	// SoftRasterizer-specific methods
	void _UpdateEdgeMarkColorTable(const u16 *edgeMarkColorTable);
	void _UpdateFogTable(const u8 *fogDensityTable);
	
	// Base rendering methods
	virtual Render3DError BeginRender(const GFX3D_State &renderState, const GFX3D_GeometryList &renderGList);
	virtual Render3DError RenderGeometry();
	virtual Render3DError EndRender();
	
	virtual Render3DError ClearUsingImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 opaquePolyID);
	virtual Render3DError ClearUsingValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
public:
	size_t _debug_drawClippedUserPoly;
	CACHE_ALIGN Color4u8 toonColor32LUT[32];
	FragmentAttributesBuffer *_framebufferAttributes;
	GFX3D_State *currentRenderState;
	
	bool _enableFragmentSamplingHack;
	
	SoftRasterizerRenderer();
	virtual ~SoftRasterizerRenderer();
	
	virtual ClipperMode GetPreferredPolygonClippingMode() const;
	
	void GetAndLoadAllTextures();
	void RasterizerPrecalculate();
	Render3DError RenderEdgeMarkingAndFog(const SoftRasterizerPostProcessParams &param);
	
	SoftRasterizerTexture* GetLoadedTextureFromPolygon(const POLY &thePoly, bool enableTexturing);
	
	const SoftRasterizerPrecalculation* GetPrecalculationList() const;
	
	// Base rendering methods
	virtual Render3DError Reset();
	virtual Render3DError ApplyRenderingSettings(const GFX3D_State &renderState);
	virtual Render3DError RenderFinish();
	virtual void ClearUsingValues_Execute(const size_t startPixel, const size_t endPixel);
	virtual Render3DError SetFramebufferSize(size_t w, size_t h);
};

template <size_t SIMDBYTES>
class SoftRasterizer_SIMD : public SoftRasterizerRenderer
{
protected:
	virtual void LoadClearValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes) = 0;
	virtual Render3DError ClearUsingValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
public:
	SoftRasterizer_SIMD();
	
	virtual Render3DError SetFramebufferSize(size_t w, size_t h);
};

#if defined(ENABLE_AVX2)
class SoftRasterizerRenderer_AVX2 : public SoftRasterizer_SIMD<32>
{
protected:
	v256u32 _clearColor_v256u32;
	v256u32 _clearDepth_v256u32;
	v256u8 _clearAttrOpaquePolyID_v256u8;
	v256u8 _clearAttrTranslucentPolyID_v256u8;
	v256u8 _clearAttrStencil_v256u8;
	v256u8 _clearAttrIsFogged_v256u8;
	v256u8 _clearAttrIsTranslucentPoly_v256u8;
	v256u8 _clearAttrPolyFacing_v256u8;
	
	virtual void LoadClearValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
public:
	virtual void ClearUsingValues_Execute(const size_t startPixel, const size_t endPixel);
};

#elif defined(ENABLE_SSE2)
class SoftRasterizerRenderer_SSE2 : public SoftRasterizer_SIMD<16>
{
protected:
	v128u32 _clearColor_v128u32;
	v128u32 _clearDepth_v128u32;
	v128u8 _clearAttrOpaquePolyID_v128u8;
	v128u8 _clearAttrTranslucentPolyID_v128u8;
	v128u8 _clearAttrStencil_v128u8;
	v128u8 _clearAttrIsFogged_v128u8;
	v128u8 _clearAttrIsTranslucentPoly_v128u8;
	v128u8 _clearAttrPolyFacing_v128u8;
	
	virtual void LoadClearValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
public:
	virtual void ClearUsingValues_Execute(const size_t startPixel, const size_t endPixel);
};

#elif defined(ENABLE_NEON_A64)
class SoftRasterizerRenderer_NEON : public SoftRasterizer_SIMD<16>
{
protected:
	uint32x4x4_t _clearColor_v128u32x4;
	uint32x4x4_t _clearDepth_v128u32x4;
	uint8x16x4_t _clearAttrOpaquePolyID_v128u8x4;
	uint8x16x4_t _clearAttrTranslucentPolyID_v128u8x4;
	uint8x16x4_t _clearAttrStencil_v128u8x4;
	uint8x16x4_t _clearAttrIsFogged_v128u8x4;
	uint8x16x4_t _clearAttrIsTranslucentPoly_v128u8x4;
	uint8x16x4_t _clearAttrPolyFacing_v128u8x4;
	
	virtual void LoadClearValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
public:
	virtual void ClearUsingValues_Execute(const size_t startPixel, const size_t endPixel);
};

#elif defined(ENABLE_ALTIVEC)
class SoftRasterizerRenderer_AltiVec : public SoftRasterizer_SIMD<16>
{
protected:
	v128u32 _clearColor_v128u32;
	v128u32 _clearDepth_v128u32;
	v128u8 _clearAttrOpaquePolyID_v128u8;
	v128u8 _clearAttrTranslucentPolyID_v128u8;
	v128u8 _clearAttrStencil_v128u8;
	v128u8 _clearAttrIsFogged_v128u8;
	v128u8 _clearAttrIsTranslucentPoly_v128u8;
	v128u8 _clearAttrPolyFacing_v128u8;
	
	virtual void LoadClearValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes);
	
public:
	virtual void ClearUsingValues_Execute(const size_t startPixel, const size_t endPixel);
};

#endif

#endif // _RASTERIZE_H_
