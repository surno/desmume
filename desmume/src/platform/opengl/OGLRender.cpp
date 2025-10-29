/*
	Copyright (C) 2006 yopyop
	Copyright (C) 2006-2007 shash
	Copyright (C) 2008-2025 DeSmuME team

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

#include "OGLRender.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <sstream>

#include "common.h"
#include "debug.h"
#include "NDSSystem.h"

#include "./filter/filter.h"
#include "./filter/xbrz.h"

#ifdef ENABLE_SSE2
#include <emmintrin.h>
#include "./utils/colorspacehandler/colorspacehandler_SSE2.h"
#endif

typedef struct
{
	unsigned int major;
	unsigned int minor;
	unsigned int revision;
} OGLVersion;

static OGLVersion _OGLDriverVersion = {0, 0, 0};

// Lookup Tables
CACHE_ALIGN const GLfloat divide5bitBy31_LUT[32]	= {0.0,             0.0322580645161, 0.0645161290323, 0.0967741935484,
													   0.1290322580645, 0.1612903225806, 0.1935483870968, 0.2258064516129,
													   0.2580645161290, 0.2903225806452, 0.3225806451613, 0.3548387096774,
													   0.3870967741935, 0.4193548387097, 0.4516129032258, 0.4838709677419,
													   0.5161290322581, 0.5483870967742, 0.5806451612903, 0.6129032258065,
													   0.6451612903226, 0.6774193548387, 0.7096774193548, 0.7419354838710,
													   0.7741935483871, 0.8064516129032, 0.8387096774194, 0.8709677419355,
													   0.9032258064516, 0.9354838709677, 0.9677419354839, 1.0};


CACHE_ALIGN const GLfloat divide6bitBy63_LUT[64]	= {0.0,             0.0158730158730, 0.0317460317460, 0.0476190476191,
													   0.0634920634921, 0.0793650793651, 0.0952380952381, 0.1111111111111,
													   0.1269841269841, 0.1428571428571, 0.1587301587302, 0.1746031746032,
													   0.1904761904762, 0.2063492063492, 0.2222222222222, 0.2380952380952,
													   0.2539682539683, 0.2698412698413, 0.2857142857143, 0.3015873015873,
													   0.3174603174603, 0.3333333333333, 0.3492063492064, 0.3650793650794,
													   0.3809523809524, 0.3968253968254, 0.4126984126984, 0.4285714285714,
													   0.4444444444444, 0.4603174603175, 0.4761904761905, 0.4920634920635,
													   0.5079365079365, 0.5238095238095, 0.5396825396825, 0.5555555555556,
													   0.5714285714286, 0.5873015873016, 0.6031746031746, 0.6190476190476,
													   0.6349206349206, 0.6507936507937, 0.6666666666667, 0.6825396825397,
													   0.6984126984127, 0.7142857142857, 0.7301587301587, 0.7460317460318,
													   0.7619047619048, 0.7777777777778, 0.7936507936508, 0.8095238095238,
													   0.8253968253968, 0.8412698412698, 0.8571428571429, 0.8730158730159,
													   0.8888888888889, 0.9047619047619, 0.9206349206349, 0.9365079365079,
													   0.9523809523810, 0.9682539682540, 0.9841269841270, 1.0};

const GLfloat PostprocessVtxBuffer[16]	= {-1.0f, -1.0f,  1.0f, -1.0f, -1.0f,  1.0f,  1.0f,  1.0f,
                                            0.0f,  0.0f,  1.0f,  0.0f,  0.0f,  1.0f,  1.0f,  1.0f};

static const GLenum GeometryDrawBuffersEnumStandard[8][4] = {
	{ OGL_COLOROUT_ATTACHMENT_ID,                         GL_NONE,                         GL_NONE,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID, OGL_FOGATTRIBUTES_ATTACHMENT_ID,                         GL_NONE,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID,        OGL_POLYID_ATTACHMENT_ID,                         GL_NONE,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID,        OGL_POLYID_ATTACHMENT_ID, OGL_FOGATTRIBUTES_ATTACHMENT_ID,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID,       OGL_WORKING_ATTACHMENT_ID,                         GL_NONE,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID,       OGL_WORKING_ATTACHMENT_ID, OGL_FOGATTRIBUTES_ATTACHMENT_ID,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID,       OGL_WORKING_ATTACHMENT_ID,        OGL_POLYID_ATTACHMENT_ID,                         GL_NONE },
	{ OGL_COLOROUT_ATTACHMENT_ID,       OGL_WORKING_ATTACHMENT_ID,        OGL_POLYID_ATTACHMENT_ID, OGL_FOGATTRIBUTES_ATTACHMENT_ID }
};

static const GLint GeometryAttachmentWorkingBufferStandard[8] = { 0,0,0,0,1,1,1,1 };
static const GLint GeometryAttachmentPolyIDStandard[8]        = { 0,0,1,1,0,0,2,2 };
static const GLint GeometryAttachmentFogAttributesStandard[8] = { 0,1,0,2,0,2,0,3 };

// OPENGL RENDERER OBJECT CREATION FUNCTION POINTERS
void (*OGLLoadEntryPoints_3_2_Func)() = NULL;
void (*OGLCreateRenderer_3_2_Func)(OpenGLRenderer **rendererPtr) = NULL;
void (*OGLLoadEntryPoints_ES_3_0_Func)() = NULL;
void (*OGLCreateRenderer_ES_3_0_Func)(OpenGLRenderer **rendererPtr) = NULL;

// OPENGL CLIENT CONTEXT FUNCTION POINTERS
bool (*oglrender_init)() = NULL;
void (*oglrender_deinit)() = NULL;
bool (*oglrender_beginOpenGL)() = NULL;
void (*oglrender_endOpenGL)() = NULL;
bool (*oglrender_framebufferDidResizeCallback)(const bool isFBOSupported, size_t w, size_t h) = NULL;

bool (*__BeginOpenGL)() = NULL;
void (*__EndOpenGL)() = NULL;

bool BEGINGL()
{
	if (__BeginOpenGL != NULL)
		return __BeginOpenGL();
	else
		return true;
}

void ENDGL()
{
	if (__EndOpenGL != NULL)
		__EndOpenGL();
}

//------------------------------------------------------------

// Textures
#if !defined(GLX_H)
OGLEXT(PFNGLACTIVETEXTUREPROC, glActiveTexture) // Core in v1.3
#endif

// Blending
#if !defined(GLX_H)
OGLEXT(PFNGLBLENDCOLORPROC, glBlendColor) // Core in v1.2
OGLEXT(PFNGLBLENDEQUATIONPROC, glBlendEquation) // Core in v1.2
#endif
OGLEXT(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) // Core in v1.4
OGLEXT(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) // Core in v2.0

// Shaders
OGLEXT(PFNGLCREATESHADERPROC, glCreateShader) // Core in v2.0
OGLEXT(PFNGLSHADERSOURCEPROC, glShaderSource) // Core in v2.0
OGLEXT(PFNGLCOMPILESHADERPROC, glCompileShader) // Core in v2.0
OGLEXT(PFNGLCREATEPROGRAMPROC, glCreateProgram) // Core in v2.0
OGLEXT(PFNGLATTACHSHADERPROC, glAttachShader) // Core in v2.0
OGLEXT(PFNGLDETACHSHADERPROC, glDetachShader) // Core in v2.0
OGLEXT(PFNGLLINKPROGRAMPROC, glLinkProgram) // Core in v2.0
OGLEXT(PFNGLUSEPROGRAMPROC, glUseProgram) // Core in v2.0
OGLEXT(PFNGLGETSHADERIVPROC, glGetShaderiv) // Core in v2.0
OGLEXT(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) // Core in v2.0
OGLEXT(PFNGLDELETESHADERPROC, glDeleteShader) // Core in v2.0
OGLEXT(PFNGLDELETEPROGRAMPROC, glDeleteProgram) // Core in v2.0
OGLEXT(PFNGLGETPROGRAMIVPROC, glGetProgramiv) // Core in v2.0
OGLEXT(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) // Core in v2.0
OGLEXT(PFNGLVALIDATEPROGRAMPROC, glValidateProgram) // Core in v2.0
OGLEXT(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) // Core in v2.0
OGLEXT(PFNGLUNIFORM1IPROC, glUniform1i) // Core in v2.0
OGLEXT(PFNGLUNIFORM1IVPROC, glUniform1iv) // Core in v2.0
OGLEXT(PFNGLUNIFORM1FPROC, glUniform1f) // Core in v2.0
OGLEXT(PFNGLUNIFORM1FVPROC, glUniform1fv) // Core in v2.0
OGLEXT(PFNGLUNIFORM2FPROC, glUniform2f) // Core in v2.0
OGLEXT(PFNGLUNIFORM4FPROC, glUniform4f) // Core in v2.0
OGLEXT(PFNGLUNIFORM4FVPROC, glUniform4fv) // Core in v2.0
OGLEXT(PFNGLDRAWBUFFERSPROC, glDrawBuffers) // Core in v2.0

// Generic vertex attributes
OGLEXT(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) // Core in v2.0
OGLEXT(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) // Core in v2.0
OGLEXT(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) // Core in v2.0
OGLEXT(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) // Core in v2.0

// VAO (always available in Apple's implementation of OpenGL, including old versions)
OGLEXT(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) // Core in v3.0 and ES v3.0
OGLEXT(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) // Core in v3.0 and ES v3.0
OGLEXT(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) // Core in v3.0 and ES v3.0

OGLEXT(PFNGLGENBUFFERSPROC, glGenBuffers) // Core in v1.5
OGLEXT(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) // Core in v1.5
OGLEXT(PFNGLBINDBUFFERPROC, glBindBuffer) // Core in v1.5
OGLEXT(PFNGLBUFFERDATAPROC, glBufferData) // Core in v1.5
OGLEXT(PFNGLBUFFERSUBDATAPROC, glBufferSubData) // Core in v1.5
#if defined(GL_VERSION_1_5)
OGLEXT(PFNGLMAPBUFFERPROC, glMapBuffer) // Core in v1.5
#endif
OGLEXT(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) // Core in v1.5

#ifdef GL_EXT_framebuffer_object
// FBO
OGLEXT(PFNGLGENFRAMEBUFFERSEXTPROC, glGenFramebuffersEXT)
OGLEXT(PFNGLBINDFRAMEBUFFEREXTPROC, glBindFramebufferEXT)
OGLEXT(PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC, glFramebufferRenderbufferEXT)
OGLEXT(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, glFramebufferTexture2DEXT)
OGLEXT(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, glCheckFramebufferStatusEXT)
OGLEXT(PFNGLDELETEFRAMEBUFFERSEXTPROC, glDeleteFramebuffersEXT)
OGLEXT(PFNGLBLITFRAMEBUFFEREXTPROC, glBlitFramebufferEXT)

// Multisampled FBO
OGLEXT(PFNGLGENRENDERBUFFERSEXTPROC, glGenRenderbuffersEXT)
OGLEXT(PFNGLBINDRENDERBUFFEREXTPROC, glBindRenderbufferEXT)
OGLEXT(PFNGLRENDERBUFFERSTORAGEEXTPROC, glRenderbufferStorageEXT)
OGLEXT(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC, glRenderbufferStorageMultisampleEXT)
OGLEXT(PFNGLDELETERENDERBUFFERSEXTPROC, glDeleteRenderbuffersEXT)
#endif // GL_EXT_framebuffer_object

#if defined(GL_APPLE_vertex_array_object)
OGLEXT(PFNGLBINDVERTEXARRAYAPPLEPROC, glBindVertexArrayAPPLE)
OGLEXT(PFNGLDELETEVERTEXARRAYSAPPLEPROC, glDeleteVertexArraysAPPLE)
OGLEXT(PFNGLGENVERTEXARRAYSAPPLEPROC, glGenVertexArraysAPPLE)
#endif

#if defined(GL_APPLE_texture_range)
OGLEXT(PFNGLTEXTURERANGEAPPLEPROC, glTextureRangeAPPLE)
#endif

static void OGLLoadEntryPoints_Legacy()
{
	// Textures
#if !defined(GLX_H)
	INITOGLEXT(PFNGLACTIVETEXTUREPROC, glActiveTexture) // Core in v1.3
#endif

	// Blending
#if !defined(GLX_H)
	INITOGLEXT(PFNGLBLENDCOLORPROC, glBlendColor) // Core in v1.2
	INITOGLEXT(PFNGLBLENDEQUATIONPROC, glBlendEquation) // Core in v1.2
#endif
	INITOGLEXT(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) // Core in v1.4
	INITOGLEXT(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate) // Core in v2.0

	// Shaders
	INITOGLEXT(PFNGLCREATESHADERPROC, glCreateShader) // Core in v2.0
	INITOGLEXT(PFNGLSHADERSOURCEPROC, glShaderSource) // Core in v2.0
	INITOGLEXT(PFNGLCOMPILESHADERPROC, glCompileShader) // Core in v2.0
	INITOGLEXT(PFNGLCREATEPROGRAMPROC, glCreateProgram) // Core in v2.0
	INITOGLEXT(PFNGLATTACHSHADERPROC, glAttachShader) // Core in v2.0
	INITOGLEXT(PFNGLDETACHSHADERPROC, glDetachShader) // Core in v2.0
	INITOGLEXT(PFNGLLINKPROGRAMPROC, glLinkProgram) // Core in v2.0
	INITOGLEXT(PFNGLUSEPROGRAMPROC, glUseProgram) // Core in v2.0
	INITOGLEXT(PFNGLGETSHADERIVPROC, glGetShaderiv) // Core in v2.0
	INITOGLEXT(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) // Core in v2.0
	INITOGLEXT(PFNGLDELETESHADERPROC, glDeleteShader) // Core in v2.0
	INITOGLEXT(PFNGLDELETEPROGRAMPROC, glDeleteProgram) // Core in v2.0
	INITOGLEXT(PFNGLGETPROGRAMIVPROC, glGetProgramiv) // Core in v2.0
	INITOGLEXT(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) // Core in v2.0
	INITOGLEXT(PFNGLVALIDATEPROGRAMPROC, glValidateProgram) // Core in v2.0
	INITOGLEXT(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1IPROC, glUniform1i) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1IVPROC, glUniform1iv) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1FPROC, glUniform1f) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM1FVPROC, glUniform1fv) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM2FPROC, glUniform2f) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM4FPROC, glUniform4f) // Core in v2.0
	INITOGLEXT(PFNGLUNIFORM4FVPROC, glUniform4fv) // Core in v2.0
	INITOGLEXT(PFNGLDRAWBUFFERSPROC, glDrawBuffers) // Core in v2.0
	
	// Generic vertex attributes
	INITOGLEXT(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) // Core in v2.0
	INITOGLEXT(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) // Core in v2.0
	INITOGLEXT(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray) // Core in v2.0
	INITOGLEXT(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) // Core in v2.0

	// Buffer Objects
	INITOGLEXT(PFNGLGENBUFFERSPROC, glGenBuffers) // Core in v1.5
	INITOGLEXT(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) // Core in v1.5
	INITOGLEXT(PFNGLBINDBUFFERPROC, glBindBuffer) // Core in v1.5
	INITOGLEXT(PFNGLBUFFERDATAPROC, glBufferData) // Core in v1.5
	INITOGLEXT(PFNGLBUFFERSUBDATAPROC, glBufferSubData) // Core in v1.5
#if defined(GL_VERSION_1_5)
	INITOGLEXT(PFNGLMAPBUFFERPROC, glMapBuffer) // Core in v1.5
#endif
	INITOGLEXT(PFNGLUNMAPBUFFERPROC, glUnmapBuffer) // Core in v1.5
	
	// VAO (always available in Apple's implementation of OpenGL, including old versions)
	INITOGLEXT(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) // Core in v3.0 and ES v3.0
	INITOGLEXT(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays) // Core in v3.0 and ES v3.0
	INITOGLEXT(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) // Core in v3.0 and ES v3.0

#ifdef GL_EXT_framebuffer_object
	// FBO
	INITOGLEXT(PFNGLGENFRAMEBUFFERSEXTPROC, glGenFramebuffersEXT)
	INITOGLEXT(PFNGLBINDFRAMEBUFFEREXTPROC, glBindFramebufferEXT)
	INITOGLEXT(PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC, glFramebufferRenderbufferEXT)
	INITOGLEXT(PFNGLFRAMEBUFFERTEXTURE2DEXTPROC, glFramebufferTexture2DEXT)
	INITOGLEXT(PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC, glCheckFramebufferStatusEXT)
	INITOGLEXT(PFNGLDELETEFRAMEBUFFERSEXTPROC, glDeleteFramebuffersEXT)
	INITOGLEXT(PFNGLBLITFRAMEBUFFEREXTPROC, glBlitFramebufferEXT)
	
	// Multisampled FBO
	INITOGLEXT(PFNGLGENRENDERBUFFERSEXTPROC, glGenRenderbuffersEXT)
	INITOGLEXT(PFNGLBINDRENDERBUFFEREXTPROC, glBindRenderbufferEXT)
	INITOGLEXT(PFNGLRENDERBUFFERSTORAGEEXTPROC, glRenderbufferStorageEXT)
	INITOGLEXT(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC, glRenderbufferStorageMultisampleEXT)
	INITOGLEXT(PFNGLDELETERENDERBUFFERSEXTPROC, glDeleteRenderbuffersEXT)
#endif // GL_EXT_framebuffer_object

#if defined(GL_APPLE_vertex_array_object)
	INITOGLEXT(PFNGLBINDVERTEXARRAYAPPLEPROC, glBindVertexArrayAPPLE)
	INITOGLEXT(PFNGLDELETEVERTEXARRAYSAPPLEPROC, glDeleteVertexArraysAPPLE)
	INITOGLEXT(PFNGLGENVERTEXARRAYSAPPLEPROC, glGenVertexArraysAPPLE)
#endif

#if defined(GL_APPLE_texture_range)
	INITOGLEXT(PFNGLTEXTURERANGEAPPLEPROC, glTextureRangeAPPLE)
#endif
}

// Vertex Shader GLSL 1.00
static const char *GeometryVtxShader_100 = {"\
attribute vec4 inPosition; \n\
attribute vec2 inTexCoord0; \n\
attribute vec3 inColor; \n\
\n\
uniform int polyMode;\n\
uniform bool polyDrawShadow;\n\
\n\
uniform float polyAlpha; \n\
uniform vec2 polyTexScale; \n\
\n\
varying vec2 vtxTexCoord; \n\
varying vec4 vtxColor; \n\
varying float isPolyDrawable;\n\
\n\
void main() \n\
{ \n\
	mat2 texScaleMtx	= mat2(	vec2(polyTexScale.x,            0.0), \n\
								vec2(           0.0, polyTexScale.y)); \n\
	\n\
	vtxTexCoord = (texScaleMtx * inTexCoord0) / 16.0; \n\
	vtxColor = vec4(inColor / 63.0, polyAlpha); \n\
	isPolyDrawable = ((polyMode != 3) || polyDrawShadow) ? 1.0 : -1.0;\n\
	\n\
	gl_Position = vec4(inPosition.x, -inPosition.y, inPosition.z, inPosition.w) / 4096.0;\n\
} \n\
"};

// Fragment Shader GLSL 1.00
static const char *GeometryFragShader_100 = {"\
varying vec2 vtxTexCoord;\n\
varying vec4 vtxColor;\n\
varying float isPolyDrawable;\n\
\n\
uniform sampler2D texRenderObject;\n\
uniform sampler1D texToonTable;\n\
\n\
uniform float stateAlphaTestRef;\n\
\n\
uniform int polyMode;\n\
uniform bool polyIsWireframe;\n\
uniform bool polySetNewDepthForTranslucent;\n\
uniform int polyID;\n\
\n\
uniform bool polyEnableTexture;\n\
uniform bool polyEnableFog;\n\
uniform bool texDrawOpaque;\n\
uniform bool texSingleBitAlpha;\n\
uniform bool polyIsBackFacing;\n\
\n\
uniform bool drawModeDepthEqualsTest;\n\
uniform bool polyDrawShadow;\n\
uniform float polyDepthOffset;\n\
\n\
#if USE_DEPTH_LEQUAL_POLYGON_FACING && !DRAW_MODE_OPAQUE\n\
uniform sampler2D inDstBackFacing;\n\
#endif\n\
\n\
void main()\n\
{\n\
#if USE_DEPTH_LEQUAL_POLYGON_FACING && !DRAW_MODE_OPAQUE\n\
	bool isOpaqueDstBackFacing = bool( texture2D(inDstBackFacing, vec2(gl_FragCoord.x/FRAMEBUFFER_SIZE_X, gl_FragCoord.y/FRAMEBUFFER_SIZE_Y)).r );\n\
	if (drawModeDepthEqualsTest && (polyIsBackFacing || !isOpaqueDstBackFacing))\n\
	{\n\
		discard;\n\
	}\n\
#endif\n\
	\n\
	vec4 mainTexColor = (ENABLE_TEXTURE_SAMPLING && polyEnableTexture) ? texture2D(texRenderObject, vtxTexCoord) : vec4(1.0, 1.0, 1.0, 1.0);\n\
	vec3 newToonColor = texture1D(texToonTable, vtxColor.r).rgb;\n\
	\n\
	if (!texSingleBitAlpha)\n\
	{\n\
		if (texDrawOpaque && (polyMode != 1) && (mainTexColor.a <= 0.999))\n\
		{\n\
			discard;\n\
		}\n\
	}\n\
#if USE_TEXTURE_SMOOTHING\n\
	else\n\
	{\n\
		if (mainTexColor.a < 0.500)\n\
		{\n\
			mainTexColor.a = 0.0;\n\
		}\n\
		else\n\
		{\n\
			mainTexColor.rgb = mainTexColor.rgb / mainTexColor.a;\n\
			mainTexColor.a = 1.0;\n\
		}\n\
	}\n\
#endif\n\
	\n\
	vec4 newFragColor = mainTexColor * vtxColor;\n\
	\n\
	if (polyMode == 1)\n\
	{\n\
		newFragColor.rgb = (ENABLE_TEXTURE_SAMPLING && polyEnableTexture) ? mix(vtxColor.rgb, mainTexColor.rgb, mainTexColor.a) : vtxColor.rgb;\n\
		newFragColor.a = vtxColor.a;\n\
	}\n\
	else if (polyMode == 2)\n\
	{\n\
#if TOON_SHADING_MODE\n\
		newFragColor.rgb = min((mainTexColor.rgb * vtxColor.r) + newToonColor.rgb, 1.0);\n\
#else\n\
		newFragColor.rgb = mainTexColor.rgb * newToonColor.rgb;\n\
#endif\n\
	}\n\
	else if ((polyMode == 3) && polyDrawShadow)\n\
	{\n\
		newFragColor = vtxColor;\n\
	}\n\
	\n\
	if ( (isPolyDrawable > 0.0) && ((newFragColor.a < 0.001) || (ENABLE_ALPHA_TEST && (newFragColor.a < stateAlphaTestRef))) )\n\
	{\n\
		discard;\n\
	}\n\
	\n\
	OUTFRAGCOLOR = newFragColor;\n\
	\n\
#if ENABLE_EDGE_MARK\n\
	gl_FragData[ATTACHMENT_POLY_ID] = (isPolyDrawable > 0.0) ? vec4( float(polyID)/63.0, float(polyIsWireframe), 0.0, float(newFragColor.a > 0.999) ) : vec4(0.0, 0.0, 0.0, 0.0);\n\
#endif\n\
#if ENABLE_FOG\n\
	gl_FragData[ATTACHMENT_FOG_ATTRIBUTES] = (isPolyDrawable > 0.0) ? vec4( float(polyEnableFog), 0.0, 0.0, float((newFragColor.a > 0.999) ? 1.0 : 0.5) ) : vec4(0.0, 0.0, 0.0, 0.0);\n\
#endif\n\
#if DRAW_MODE_OPAQUE\n\
	gl_FragData[ATTACHMENT_WORKING_BUFFER] = vec4(float(polyIsBackFacing), 0.0, 0.0, 1.0);\n\
#endif\n\
	\n\
#if USE_NDS_DEPTH_CALCULATION || ENABLE_FOG\n\
	// It is tempting to perform the NDS depth calculation in the vertex shader rather than in the fragment shader.\n\
	// Resist this temptation! It is much more reliable to do the depth calculation in the fragment shader due to\n\
	// subtle interpolation differences between various GPUs and/or drivers. If the depth calculation is not done\n\
	// here, then it is very possible for the user to experience Z-fighting in certain rendering situations.\n\
	\n\
	#if ENABLE_W_DEPTH\n\
	gl_FragDepth = clamp( ((1.0/gl_FragCoord.w) * (4096.0/16777215.0)) + polyDepthOffset, 0.0, 1.0 );\n\
	#else\n\
	// hack: when using z-depth, drop some LSBs so that the overworld map in Dragon Quest IV shows up correctly\n\
	gl_FragDepth = clamp( (floor(gl_FragCoord.z * 4194303.0) * (4.0/16777215.0)) + polyDepthOffset, 0.0, 1.0 );\n\
	#endif\n\
#endif\n\
}\n\
"};

// Vertex shader for determining which pixels have a zero alpha, GLSL 1.00
static const char *GeometryZeroDstAlphaPixelMaskVtxShader_100 = {"\
attribute vec2 inPosition;\n\
attribute vec2 inTexCoord0;\n\
varying vec2 texCoord;\n\
\n\
void main()\n\
{\n\
	texCoord = inTexCoord0;\n\
	gl_Position = vec4(inPosition, 0.0, 1.0);\n\
}\n\
"};

// Fragment shader for determining which pixels have a zero alpha, GLSL 1.00
static const char *GeometryZeroDstAlphaPixelMaskFragShader_100 = {"\
varying vec2 texCoord;\n\
uniform sampler2D texInFragColor;\n\
\n\
void main()\n\
{\n\
	vec4 inFragColor = texture2D(texInFragColor, texCoord);\n\
	\n\
	if (inFragColor.a <= 0.001)\n\
	{\n\
		discard;\n\
	}\n\
}\n\
"};

// Vertex shader for applying edge marking, GLSL 1.00
static const char *EdgeMarkVtxShader_100 = {"\
attribute vec2 inPosition;\n\
attribute vec2 inTexCoord0;\n\
varying vec2 texCoord[5];\n\
\n\
void main()\n\
{\n\
	vec2 texInvScale = vec2(1.0/FRAMEBUFFER_SIZE_X, 1.0/FRAMEBUFFER_SIZE_Y);\n\
	\n\
	texCoord[0] = inTexCoord0; // Center\n\
	texCoord[1] = inTexCoord0 + (vec2( 1.0, 0.0) * texInvScale); // Right\n\
	texCoord[2] = inTexCoord0 + (vec2( 0.0, 1.0) * texInvScale); // Down\n\
	texCoord[3] = inTexCoord0 + (vec2(-1.0, 0.0) * texInvScale); // Left\n\
	texCoord[4] = inTexCoord0 + (vec2( 0.0,-1.0) * texInvScale); // Up\n\
	\n\
	gl_Position = vec4(inPosition, 0.0, 1.0);\n\
}\n\
"};

// Fragment shader for applying edge marking, GLSL 1.00
static const char *EdgeMarkFragShader_100 = {"\
varying vec2 texCoord[5];\n\
\n\
uniform sampler2D texInFragDepth;\n\
uniform sampler2D texInPolyID;\n\
uniform sampler1D texEdgeColor;\n\
\n\
uniform int clearPolyID;\n\
uniform float clearDepth;\n\
\n\
void main()\n\
{\n\
	vec4 polyIDInfo[5];\n\
	polyIDInfo[0] = texture2D(texInPolyID, texCoord[0]);\n\
	polyIDInfo[1] = texture2D(texInPolyID, texCoord[1]);\n\
	polyIDInfo[2] = texture2D(texInPolyID, texCoord[2]);\n\
	polyIDInfo[3] = texture2D(texInPolyID, texCoord[3]);\n\
	polyIDInfo[4] = texture2D(texInPolyID, texCoord[4]);\n\
	\n\
	vec4 edgeColor[5];\n\
	edgeColor[0] = texture1D(texEdgeColor, polyIDInfo[0].r);\n\
	edgeColor[1] = texture1D(texEdgeColor, polyIDInfo[1].r);\n\
	edgeColor[2] = texture1D(texEdgeColor, polyIDInfo[2].r);\n\
	edgeColor[3] = texture1D(texEdgeColor, polyIDInfo[3].r);\n\
	edgeColor[4] = texture1D(texEdgeColor, polyIDInfo[4].r);\n\
	\n\
	float depth[5];\n\
	depth[0] = texture2D(texInFragDepth, texCoord[0]).r;\n\
	depth[1] = texture2D(texInFragDepth, texCoord[1]).r;\n\
	depth[2] = texture2D(texInFragDepth, texCoord[2]).r;\n\
	depth[3] = texture2D(texInFragDepth, texCoord[3]).r;\n\
	depth[4] = texture2D(texInFragDepth, texCoord[4]).r;\n\
	\n\
	bool isWireframe[5];\n\
	isWireframe[0] = bool(polyIDInfo[0].g);\n\
	\n\
	vec4 newEdgeColor = vec4(0.0, 0.0, 0.0, 0.0);\n\
	\n\
	if (!isWireframe[0])\n\
	{\n\
		int polyID[5];\n\
		polyID[0] = int((polyIDInfo[0].r * 63.0) + 0.5);\n\
		polyID[1] = int((polyIDInfo[1].r * 63.0) + 0.5);\n\
		polyID[2] = int((polyIDInfo[2].r * 63.0) + 0.5);\n\
		polyID[3] = int((polyIDInfo[3].r * 63.0) + 0.5);\n\
		polyID[4] = int((polyIDInfo[4].r * 63.0) + 0.5);\n\
		\n\
		isWireframe[1] = bool(polyIDInfo[1].g);\n\
		isWireframe[2] = bool(polyIDInfo[2].g);\n\
		isWireframe[3] = bool(polyIDInfo[3].g);\n\
		isWireframe[4] = bool(polyIDInfo[4].g);\n\
		\n\
		bool isEdgeMarkingClearValues = ((polyID[0] != clearPolyID) && (depth[0] < clearDepth) && !isWireframe[0]);\n\
		\n\
		if ( ((gl_FragCoord.x >= FRAMEBUFFER_SIZE_X-1.0) ? isEdgeMarkingClearValues : ((polyID[0] != polyID[1]) && (depth[0] >= depth[1]) && !isWireframe[1])) )\n\
		{\n\
			newEdgeColor = (gl_FragCoord.x >= FRAMEBUFFER_SIZE_X-1.0) ? edgeColor[0] : edgeColor[1];\n\
		}\n\
		else if ( ((gl_FragCoord.y >= FRAMEBUFFER_SIZE_Y-1.0) ? isEdgeMarkingClearValues : ((polyID[0] != polyID[2]) && (depth[0] >= depth[2]) && !isWireframe[2])) )\n\
		{\n\
			newEdgeColor = (gl_FragCoord.y >= FRAMEBUFFER_SIZE_Y-1.0) ? edgeColor[0] : edgeColor[2];\n\
		}\n\
		else if ( ((gl_FragCoord.x < 1.0) ? isEdgeMarkingClearValues : ((polyID[0] != polyID[3]) && (depth[0] >= depth[3]) && !isWireframe[3])) )\n\
		{\n\
			newEdgeColor = (gl_FragCoord.x < 1.0) ? edgeColor[0] : edgeColor[3];\n\
		}\n\
		else if ( ((gl_FragCoord.y < 1.0) ? isEdgeMarkingClearValues : ((polyID[0] != polyID[4]) && (depth[0] >= depth[4]) && !isWireframe[4])) )\n\
		{\n\
			newEdgeColor = (gl_FragCoord.y < 1.0) ? edgeColor[0] : edgeColor[4];\n\
		}\n\
	}\n\
	\n\
	gl_FragColor = newEdgeColor;\n\
}\n\
"};

// Vertex shader for applying fog, GLSL 1.00
static const char *FogVtxShader_100 = {"\
attribute vec2 inPosition;\n\
attribute vec2 inTexCoord0;\n\
varying vec2 texCoord;\n\
\n\
void main() \n\
{ \n\
	texCoord = inTexCoord0;\n\
	gl_Position = vec4(inPosition, 0.0, 1.0);\n\
}\n\
"};

// Fragment shader for applying fog, GLSL 1.00
static const char *FogFragShader_100 = {"\
varying vec2 texCoord;\n\
\n\
uniform sampler2D texInFragDepth;\n\
uniform sampler2D texInFogAttributes;\n\
uniform sampler1D texFogDensityTable;\n\
uniform bool stateEnableFogAlphaOnly;\n\
\n\
void main()\n\
{\n\
	float inFragDepth = texture2D(texInFragDepth, texCoord).r;\n\
	vec4 inFogAttributes = texture2D(texInFogAttributes, texCoord);\n\
	bool polyEnableFog = (inFogAttributes.r > 0.999);\n\
	float densitySelect = (FOG_STEP == 0) ? ((inFragDepth <= FOG_OFFSETF) ? 0.0 : 1.0) : (inFragDepth * (1024.0/float(FOG_STEP))) + (((-float(FOG_OFFSET)/float(FOG_STEP)) - 0.5) / 32.0);\n\
	float fogMixWeight = texture1D(texFogDensityTable, densitySelect).r;\n\
	\n\
	gl_FragColor = (polyEnableFog) ? ((stateEnableFogAlphaOnly) ? vec4(vec3(0.0), fogMixWeight) : vec4(fogMixWeight)) : vec4(0.0);\n\
}\n\
"};

// Vertex shader for the final framebuffer
static const char *FramebufferOutputVtxShader = {"\
IN_VTX_POSITION vec2 inPosition;\n\
IN_VTX_TEXCOORD0 vec2 inTexCoord0;\n\
OUT_VARYING vec2 texCoord;\n\
\n\
void main()\n\
{\n\
	texCoord = inTexCoord0;\n\
	gl_Position = vec4(inPosition, 0.0, 1.0);\n\
}\n\
"};

// Fragment shader for the final BGRA6665 formatted framebuffer
static const char *FramebufferOutputBGRA6665FragShader = {"\
IN_VARYING vec2 texCoord;\n\
\n\
uniform sampler2D texInFragColor;\n\
\n\
void main()\n\
{\n\
	// Note that we swap B and R since pixel readbacks are done in BGRA format for fastest\n\
	// performance. The final color is still in RGBA format.\n\
	vec4 colorRGBA6665 = SAMPLE4_TEX_2D(texInFragColor, texCoord).bgra;\n\
	colorRGBA6665     = floor((colorRGBA6665 * 255.0) + 0.5);\n\
	colorRGBA6665.rgb = floor(colorRGBA6665.rgb / 4.0);\n\
	colorRGBA6665.a   = floor(colorRGBA6665.a   / 8.0);\n\
	\n\
	FRAG_COLOR_VAR = (colorRGBA6665 / 255.0);\n\
}\n\
"};

// Fragment shader for the final BGRA8888 formatted framebuffer
static const char *FramebufferOutputBGRA8888FragShader = {"\
IN_VARYING vec2 texCoord;\n\
\n\
uniform sampler2D texInFragColor;\n\
\n\
void main()\n\
{\n\
	// Note that we swap B and R since pixel readbacks are done in BGRA format for fastest\n\
	// performance. The final color is still in RGBA format.\n\
	FRAG_COLOR_VAR = SAMPLE4_TEX_2D(texInFragColor, texCoord).bgra;\n\
}\n\
"};

// Fragment shader for the final RGBA6665 formatted framebuffer
static const char *FramebufferOutputRGBA6665FragShader = {"\
IN_VARYING vec2 texCoord;\n\
\n\
uniform sampler2D texInFragColor;\n\
\n\
void main()\n\
{\n\
	vec4 colorRGBA6665 = SAMPLE4_TEX_2D(texInFragColor, texCoord);\n\
	colorRGBA6665     = floor((colorRGBA6665 * 255.0) + 0.5);\n\
	colorRGBA6665.rgb = floor(colorRGBA6665.rgb / 4.0);\n\
	colorRGBA6665.a   = floor(colorRGBA6665.a   / 8.0);\n\
	\n\
	FRAG_COLOR_VAR = (colorRGBA6665 / 255.0);\n\
}\n\
"};

bool IsOpenGLDriverVersionSupported(unsigned int checkVersionMajor, unsigned int checkVersionMinor, unsigned int checkVersionRevision)
{
	bool result = false;
	
	if ( (_OGLDriverVersion.major > checkVersionMajor) ||
		 (_OGLDriverVersion.major >= checkVersionMajor && _OGLDriverVersion.minor > checkVersionMinor) ||
		 (_OGLDriverVersion.major >= checkVersionMajor && _OGLDriverVersion.minor >= checkVersionMinor && _OGLDriverVersion.revision >= checkVersionRevision) )
	{
		result = true;
	}
	
	return result;
}

static void OGLGetDriverVersion(const char *oglVersionString,
                                unsigned int *versionMajor,
                                unsigned int *versionMinor,
                                unsigned int *versionRevision)
{
	size_t versionStringLength = 0;
	
	if (oglVersionString == NULL)
	{
		return;
	}
	
	// First, check for the dot in the revision string. There should be at
	// least one present.
	const char *versionStrDot = strstr(oglVersionString, ".");
	if (versionStrDot == NULL)
	{
		return;
	}
	
	// Next, check for a space that is after the dot, but before the vendor-specific info (if present).
	versionStringLength = strlen(oglVersionString); // Set our default string length here
	const size_t endCheckLength = versionStringLength - (versionStrDot - oglVersionString); // Maximum possible length to check
	const char *checkStringLimit = (endCheckLength < 10) ? versionStrDot + endCheckLength : versionStrDot + 10; // But we're going to limit ourselves to checking only the first 10 characters
	
	char *versionStrEnd = (char *)versionStrDot;
	while (versionStrEnd < checkStringLimit)
	{
		versionStrEnd++;
		if (*versionStrEnd == ' ')
		{
			versionStringLength = versionStrEnd - oglVersionString;
			break;
		}
	}
	
	// Check for any spaces before the dot. There shouldn't be any text before the version number, so
	// this step shouldn't be necessary. However, some drivers can defy the OpenGL spec and include
	// text before the version number, and so we need to handle such non-compliant drivers just in case.
	char *versionStrStart = (char *)versionStrDot;
	while (versionStrStart > oglVersionString)
	{
		versionStrStart--;
		if (*versionStrStart == ' ')
		{
			versionStrStart++; // Don't include the space we just checked.
			versionStringLength -= versionStrStart - oglVersionString;
			break;
		}
	}
	
	// Copy the version substring and parse it.
	char *versionSubstring = (char *)malloc((versionStringLength + 1) * sizeof(char));
	versionSubstring[versionStringLength] = '\0';
	strncpy(versionSubstring, versionStrStart, versionStringLength);
	
	unsigned int major = 0;
	unsigned int minor = 0;
	unsigned int revision = 0;
	
	sscanf(versionSubstring, "%u.%u.%u", &major, &minor, &revision);
	
	free(versionSubstring);
	versionSubstring = NULL;
	
	if (versionMajor != NULL)
	{
		*versionMajor = major;
	}
	
	if (versionMinor != NULL)
	{
		*versionMinor = minor;
	}
	
	if (versionRevision != NULL)
	{
		*versionRevision = revision;
	}
}

static bool ValidateShaderCompileOGL(GLenum shaderType, GLuint theShader)
{
	bool isCompileValid = false;
	GLint status = GL_FALSE;
	
	glGetShaderiv(theShader, GL_COMPILE_STATUS, &status);
	if(status == GL_TRUE)
	{
		isCompileValid = true;
	}
	else
	{
		GLint logSize;
		GLchar *log = NULL;
		
		glGetShaderiv(theShader, GL_INFO_LOG_LENGTH, &logSize);
		log = new GLchar[logSize];
		glGetShaderInfoLog(theShader, logSize, &logSize, log);
		
		switch (shaderType)
		{
			case GL_VERTEX_SHADER:
				INFO("OpenGL: FAILED TO COMPILE VERTEX SHADER:\n%s\n", log);
				break;
				
			case GL_FRAGMENT_SHADER:
				INFO("OpenGL: FAILED TO COMPILE FRAGMENT SHADER:\n%s\n", log);
				break;
				
			default:
				INFO("OpenGL: FAILED TO COMPILE SHADER:\n%s\n", log);
				break;
		}
		
		delete[] log;
	}
	
	return isCompileValid;
}

Render3DError ShaderProgramCreateOGL(GLuint &vtxShaderID,
                                     GLuint &fragShaderID,
                                     GLuint &programID,
                                     const char *vtxShaderCString,
                                     const char *fragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	
	if (vtxShaderID == 0)
	{
		vtxShaderID = glCreateShader(GL_VERTEX_SHADER);
		if (vtxShaderID == 0)
		{
			INFO("OpenGL: Failed to create the vertex shader.\n");
			return OGLERROR_SHADER_CREATE_ERROR;
		}
		
		const char *vtxShaderCStringPtr = vtxShaderCString;
		glShaderSource(vtxShaderID, 1, (const GLchar **)&vtxShaderCStringPtr, NULL);
		glCompileShader(vtxShaderID);
		if (!ValidateShaderCompileOGL(GL_VERTEX_SHADER, vtxShaderID))
		{
			error = OGLERROR_SHADER_CREATE_ERROR;
			return error;
		}
	}
	
	if (fragShaderID == 0)
	{
		fragShaderID = glCreateShader(GL_FRAGMENT_SHADER);
		if (fragShaderID == 0)
		{
			INFO("OpenGL: Failed to create the fragment shader.\n");
			error = OGLERROR_SHADER_CREATE_ERROR;
			return error;
		}
		
		const char *fragShaderCStringPtr = fragShaderCString;
		glShaderSource(fragShaderID, 1, (const GLchar **)&fragShaderCStringPtr, NULL);
		glCompileShader(fragShaderID);
		if (!ValidateShaderCompileOGL(GL_FRAGMENT_SHADER, fragShaderID))
		{
			error = OGLERROR_SHADER_CREATE_ERROR;
			return error;
		}
	}
	
	programID = glCreateProgram();
	if (programID == 0)
	{
		INFO("OpenGL: Failed to create the shader program.\n");
		error = OGLERROR_SHADER_CREATE_ERROR;
		return error;
	}
	
	glAttachShader(programID, vtxShaderID);
	glAttachShader(programID, fragShaderID);
	
	return error;
}

bool ValidateShaderProgramLinkOGL(GLuint theProgram)
{
	bool isLinkValid = false;
	GLint status = GL_FALSE;
	
	glGetProgramiv(theProgram, GL_LINK_STATUS, &status);
	if(status == GL_TRUE)
	{
		isLinkValid = true;
	}
	else
	{
		GLint logSize;
		GLchar *log = NULL;
		
		glGetProgramiv(theProgram, GL_INFO_LOG_LENGTH, &logSize);
		log = new GLchar[logSize];
		glGetProgramInfoLog(theProgram, logSize, &logSize, log);
		
		INFO("OpenGL: FAILED TO LINK SHADER PROGRAM:\n%s\n", log);
		delete[] log;
	}
	
	return isLinkValid;
}

static void FramebufferProcessVertexAttribEnableLEGACYOGL(const OGLFeatureInfo &feature, GLuint vaoID, GLuint vboID)
{
	if (!feature.supportShaders)
	{
		// Framebuffer processing requires shaders, so there is no legacy code path
		// for this method.
		return;
	}
	
	if (feature.supportVAO || feature.supportVAO_APPLE)
	{
		glBindVertexArray(vaoID);
	}
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, vboID);
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glDisableVertexAttribArray(OGLVertexAttributeID_Color); // Framebuffer processing doesn't use vertex colors.
		glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
	}
}

static void FramebufferProcessVertexAttribDisableLEGACYOGL(const OGLFeatureInfo &feature)
{
	if (!feature.supportShaders)
	{
		// Framebuffer processing requires shaders, so there is no legacy code path
		// for this method.
		return;
	}
	
	if (feature.supportVAO || feature.supportVAO_APPLE)
	{
		glBindVertexArray(0);
	}
	else
	{
		glDisableVertexAttribArray(OGLVertexAttributeID_Position);
		glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glDisableVertexAttribArray(OGLVertexAttributeID_Color);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
	}
}

OpenGLRenderColorOut::OpenGLRenderColorOut(const OGLFeatureInfo &feature, size_t w, size_t h)
{
	_format = NDSColorFormat_BGR888_Rev;
	_feature = feature;
	
	_framebufferWidth      = w;
	_framebufferHeight     = h;
	_framebufferPixelCount = w * h;
	_framebufferSize16     = w * h * sizeof(u16);
	_framebufferSize32     = w * h * sizeof(Color4u8);
	
	_vsFramebufferOutput6665ShaderID = 0;
	_vsFramebufferOutput8888ShaderID = 0;
	_fsFramebufferRGBA6665OutputShaderID = 0;
	_fsFramebufferRGBA8888OutputShaderID = 0;
	_pgFramebufferRGBA6665OutputID = 0;
	_pgFramebufferRGBA8888OutputID = 0;
	
	_vboPostprocessVtxID = 0;
	_vaoPostprocessStatesID = 0;
	_fboRenderID = 0;
	
	_pbo[0] = 0;
	_pbo[1] = 0;
	_texColorOut[0] = 0;
	_texColorOut[1] = 0;
	_masterBufferCPU32 = NULL;
	_bufferCPU32[0] = NULL;
	_bufferCPU32[1] = NULL;
	
	_willConvertColorOnGPU = _feature.supportVBO && _feature.supportShaders;
	if (_willConvertColorOnGPU)
	{
		Render3DError error = RENDER3DERROR_NOERR;
		bool wasErrorDetected = false;
		
		glGenBuffers(1, &_vboPostprocessVtxID);
		glBindBuffer(GL_ARRAY_BUFFER, _vboPostprocessVtxID);
		glBufferData(GL_ARRAY_BUFFER, sizeof(PostprocessVtxBuffer), PostprocessVtxBuffer, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		
		if ( _feature.supportVAO_APPLE ||
			(_feature.supportVAO && ((_feature.variantID & OpenGLVariantFamily_Legacy) != 0)) )
		{
			glGenVertexArrays(1, &_vaoPostprocessStatesID);
			glBindVertexArray(_vaoPostprocessStatesID);
			
			glBindBuffer(GL_ARRAY_BUFFER, _vboPostprocessVtxID);
			glEnableVertexAttribArray(OGLVertexAttributeID_Position);
			glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
			glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
			glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
			
			glBindVertexArray(0);
		}
		
		if (_feature.readPixelsBestFormat == GL_BGRA)
		{
			error = _CreateFramebufferOutput6665Program(FramebufferOutputVtxShader, FramebufferOutputBGRA6665FragShader);
			wasErrorDetected = wasErrorDetected || (error != RENDER3DERROR_NOERR);
			
			error = _CreateFramebufferOutput8888Program(FramebufferOutputVtxShader, FramebufferOutputBGRA8888FragShader);
			wasErrorDetected = wasErrorDetected || (error != RENDER3DERROR_NOERR);
		}
		else
		{
			error = _CreateFramebufferOutput6665Program(FramebufferOutputVtxShader, FramebufferOutputRGBA6665FragShader);
			wasErrorDetected = wasErrorDetected || (error != RENDER3DERROR_NOERR);

			// No need to create a shader program for RGBA8888 since no conversion is necessary in this case.
		}
		
		if (!wasErrorDetected)
		{
			INFO("OpenGL: Supports converting the framebuffer color on the GPU.\n");
		}
	}
	else
	{
		INFO("OpenGL: Cannot convert the framebuffer color on the GPU due to missing OpenGL features.\n        Will convert the framebuffer color on the CPU instead.\n");
	}
	
	
	if (_feature.supportTextureRange_APPLE && _feature.supportClientStorage_APPLE)
	{
		// If GL_APPLE_texture_range and GL_APPLE_client_storage extensions are both
		// supported, then try using them first before any other methods. Using these
		// extensions together allows for asynchronous direct DMA transfers from GPU to
		// CPU much like PBOs, but the Apple method is slightly faster and uses less memory.
		glGenTextures(2, _texColorOut);
		INFO("OpenGL: Supports asynchronous framebuffer downloads using Apple Client Storage.\n");
	}
	else if (_feature.supportPBO)
	{
		if ((_feature.variantID & OpenGLVariantFamily_ES) == 0)
		{
			// PBOs are supported on most platforms since PBOs only require OpenGL v2.1.
			// PBOs turn glReadPixels() from a synchronous call into an asynchronous call,
			// which helps hide copy latencies when downloading the framebuffer, ultimately
			// boosting performance.
			glGenBuffers(2, _pbo);
			INFO("OpenGL: Supports asynchronous framebuffer downloads using PBOs.\n");
		}
		else
		{
			// PBOs work by doing an asynchronous glReadPixels() call, which is very beneficial
			// to desktop-class GPUs since such devices are expected to be connected to a data
			// bus.
			//
			// However, many ARM-based mobile devices use integrated GPUs of varying degrees
			// of memory latency and implementation quality. This means that the performance
			// of an asynchronous glReadPixels() call is NOT guaranteed on such devices.
			//
			// In fact, many ARM-based devices suffer devastating performance drops when trying
			// to do asynchronous framebuffer reads. Therefore, since most OpenGL ES users will
			// be running an ARM-based iGPU, we will disable PBOs for OpenGL ES and stick with
			// a traditional synchronous glReadPixels() call instead.
			INFO("OpenGL ES: PBOs provide poor performance on this platform.\n           Framebuffer downloads will be synchronous only.\n");
		}
	}
	else
	{
		INFO("OpenGL: Does not support asynchronous framebuffer downloads.\n        Framebuffer downloads will be synchronous only.\n");
	}
	
	if (_feature.readPixelsBestFormat == GL_RGBA)
	{
		INFO("        Pixel read format is GL_RGBA.\n");
	}
	else if (_feature.readPixelsBestFormat == GL_BGRA)
	{
		INFO("        Pixel read format is GL_BGRA.\n");
	}
	
	if ( !_willConvertColorOnGPU || (_texColorOut[0] != 0) || (_texColorOut[1] != 0) || (_pbo[0] == 0) || (_pbo[1] == 0) )
	{
		_masterBufferCPU32 = (Color4u8 *)malloc_alignedPage(_framebufferSize32 * 2);
		memset(_masterBufferCPU32, 0, _framebufferSize32 * 2);
	}
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
	if ( (_texColorOut[0] != 0) || (_texColorOut[1] != 0) )
	{
		glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB, (GLsizei)(_framebufferSize32 * 2), _masterBufferCPU32);
		glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
	}
#endif
	if ( (w != 0) && (h != 0) )
	{
		for (size_t i = 0; i < 2; i++)
		{
			if (_pbo[i] != 0)
			{
				glBindBuffer(GL_PIXEL_PACK_BUFFER, _pbo[i]);
				glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizei)_framebufferSize32, NULL, GL_STREAM_READ);
				_buffer32[i] = NULL;
			}
			
			if ( !_willConvertColorOnGPU || (_pbo[i] == 0) || (_texColorOut[i] != 0) )
			{
				_bufferCPU32[i] = _masterBufferCPU32 + (_framebufferPixelCount * i);
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
				if (_texColorOut[i] != 0)
				{
					glBindTexture(GL_TEXTURE_RECTANGLE_ARB, _texColorOut[i]);
					glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
					glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, (GLsizei)_framebufferWidth, (GLsizei)_framebufferHeight, 0, _feature.readPixelsBestFormat, _feature.readPixelsBestDataType, _bufferCPU32[i]);
				}
#endif
				if (_pbo[i] == 0)
				{
					_buffer32[i] = _bufferCPU32[i];
				}
			}
			
			_buffer16[i] = (Color5551 *)malloc_alignedPage(_framebufferSize16);
			memset(_buffer16[i], 0, _framebufferSize16);
			
			_state[i] = AsyncReadState_Free;
		}
	}
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
	if ( (_texColorOut[0] != 0) || (_texColorOut[1] != 0) )
	{
		glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
	}
#endif
}

OpenGLRenderColorOut::~OpenGLRenderColorOut()
{
	glFinish();
	
	if (this->_feature.supportShaders)
	{
		glUseProgram(0);
		this->_DestroyFramebufferOutput6665Programs();
		this->_DestroyFramebufferOutput8888Programs();
	}
	
	for (size_t i = 0; i < 2; i++)
	{
		if (this->_state[i] == AsyncReadState_Disabled)
		{
			continue;
		}
		
		if ( (this->_buffer32[i] != NULL) && (this->_pbo[i] != 0) )
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[i]);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			this->_buffer32[i] = NULL;
		}
		
		this->_bufferCPU32[i] = NULL;
		this->_buffer32[i] = NULL;
		
		if (this->_buffer16[i] != NULL)
		{
			free_aligned(this->_buffer16[i]);
			this->_buffer16[i] = NULL;
		}
		
		this->_state[i] = AsyncReadState_Disabled;
	}
	
	free_aligned(this->_masterBufferCPU32);
	this->_masterBufferCPU32 = NULL;
	
	if (this->_pbo[0] != 0)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
		glDeleteBuffers(2, this->_pbo);
		this->_pbo[0] = 0;
		this->_pbo[1] = 0;
	}
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
	if (this->_texColorOut[0] != 0)
	{
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
		glDeleteTextures(2, this->_texColorOut);
		this->_texColorOut[0] = 0;
		this->_texColorOut[1] = 0;
	}
#endif
	if (this->_vaoPostprocessStatesID != 0)
	{
		if (this->_feature.supportVAO || this->_feature.supportVAO_APPLE)
		{
			glBindVertexArray(0);
			glDeleteVertexArrays(1, &this->_vaoPostprocessStatesID);
			this->_vaoPostprocessStatesID = 0;
		}
	}
	
	if (this->_vboPostprocessVtxID != 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDeleteBuffers(1, &this->_vboPostprocessVtxID);
		this->_vboPostprocessVtxID = 0;
	}
	
	glFinish();
}

Color4u8* OpenGLRenderColorOut::_MapBuffer32OGL() const
{
	return (Color4u8 *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
}

Render3DError OpenGLRenderColorOut::_CreateFramebufferOutput6665Program(const char *vtxShaderCString, const char *fragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	
	if ( (vtxShaderCString == NULL) || (fragShaderCString == NULL) )
	{
		return error;
	}
	
	std::stringstream shaderHeader;
	if (this->_feature.variantID & OpenGLVariantFamily_ES3)
	{
		shaderHeader << "#version 300 es\n";
		shaderHeader << "precision highp float;\n";
		shaderHeader << "precision highp int;\n";
		shaderHeader << "\n";
	}
	else if (this->_feature.supportShaderFixedLocation)
	{
		shaderHeader << "#version 330\n";
		shaderHeader << "\n";
	}
	else if (this->_feature.variantID & OpenGLVariantFamily_CoreProfile)
	{
		shaderHeader << "#version 150\n";
		shaderHeader << "\n";
	}
	shaderHeader << "#define FRAMEBUFFER_SIZE_X " << this->_framebufferWidth  << ".0 \n";
	shaderHeader << "#define FRAMEBUFFER_SIZE_Y " << this->_framebufferHeight << ".0 \n";
	shaderHeader << "\n";
	
	std::stringstream vsHeader;
	if (this->_feature.supportShaderFixedLocation)
	{
		vsHeader << "#define IN_VTX_POSITION layout (location = "  << OGLVertexAttributeID_Position  << ") in\n";
		vsHeader << "#define IN_VTX_TEXCOORD0 layout (location = " << OGLVertexAttributeID_TexCoord0 << ") in\n";
		vsHeader << "#define IN_VTX_COLOR layout (location = "     << OGLVertexAttributeID_Color     << ") in\n";
		vsHeader << "#define OUT_VARYING out\n";
	}
	else if (this->_feature.variantID & OpenGLVariantFamily_CoreProfile)
	{
		vsHeader << "#define IN_VTX_POSITION in\n";
		vsHeader << "#define IN_VTX_TEXCOORD0 in\n";
		vsHeader << "#define IN_VTX_COLOR in\n";
		vsHeader << "#define OUT_VARYING out\n";
	}
	else
	{
		vsHeader << "#define IN_VTX_POSITION attribute\n";
		vsHeader << "#define IN_VTX_TEXCOORD0 attribute\n";
		vsHeader << "#define IN_VTX_COLOR attribute\n";
		vsHeader << "#define OUT_VARYING varying\n";
	}
	
	std::stringstream fsHeader;
	if (this->_feature.variantID & OpenGLVariantFamily_ES3)
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture(t,c)\n";
		fsHeader << "#define IN_VARYING in\n";
		fsHeader << "#define OUT_COLOR layout (location = " << (OGL_WORKING_ATTACHMENT_ID - GL_COLOR_ATTACHMENT0_EXT) << ") out\n";
		fsHeader << "#define FRAG_COLOR_VAR outFragColor\n\n";
		fsHeader << "OUT_COLOR vec4 FRAG_COLOR_VAR;\n\n";
	}
	else if (this->_feature.supportShaderFixedLocation)
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture(t,c)\n";
		fsHeader << "#define IN_VARYING in\n";
		fsHeader << "#define OUT_COLOR layout (location = 0) out\n";
		fsHeader << "#define FRAG_COLOR_VAR outFragColor\n\n";
		fsHeader << "OUT_COLOR vec4 FRAG_COLOR_VAR;\n\n";
	}
	else if (this->_feature.variantID & OpenGLVariantFamily_CoreProfile)
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture(t,c)\n";
		fsHeader << "#define IN_VARYING in\n";
		fsHeader << "#define OUT_COLOR out\n";
		fsHeader << "#define FRAG_COLOR_VAR outFragColor\n\n";
		fsHeader << "OUT_COLOR vec4 FRAG_COLOR_VAR;\n\n";
	}
	else
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture2D(t,c)\n";
		fsHeader << "#define IN_VARYING varying\n";
		fsHeader << "#define OUT_COLOR varying\n";
		fsHeader << "#define FRAG_COLOR_VAR gl_FragColor\n\n";
	}
	
	std::string vtxShaderCode  = shaderHeader.str() + vsHeader.str() + std::string(vtxShaderCString);
	std::string fragShaderCode = shaderHeader.str() + fsHeader.str() + std::string(fragShaderCString);
	
	error = ShaderProgramCreateOGL(this->_vsFramebufferOutput6665ShaderID,
	                               this->_fsFramebufferRGBA6665OutputShaderID,
	                               this->_pgFramebufferRGBA6665OutputID,
	                               vtxShaderCode.c_str(),
								   fragShaderCode.c_str());
	if (error != OGLERROR_NOERR)
	{
		INFO("OpenGL: Failed to create the FRAMEBUFFER OUTPUT RGBA6665 shader program.\n");
		glUseProgram(0);
		this->_DestroyFramebufferOutput6665Programs();
		return error;
	}
	
	glBindAttribLocation(this->_pgFramebufferRGBA6665OutputID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(this->_pgFramebufferRGBA6665OutputID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	glLinkProgram(this->_pgFramebufferRGBA6665OutputID);
	if (!ValidateShaderProgramLinkOGL(this->_pgFramebufferRGBA6665OutputID))
	{
		INFO("OpenGL: Failed to link the FRAMEBUFFER OUTPUT RGBA6665 shader program.\n");
		glUseProgram(0);
		this->_DestroyFramebufferOutput6665Programs();
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(this->_pgFramebufferRGBA6665OutputID);
	glUseProgram(this->_pgFramebufferRGBA6665OutputID);
	
	const GLint uniformTexGColor = glGetUniformLocation(this->_pgFramebufferRGBA6665OutputID, "texInFragColor");
	if (this->_feature.supportFBO)
	{
		glUniform1i(uniformTexGColor, OGLTextureUnitID_GColor);
	}
	else
	{
		// Reading back the output framebuffer without FBOs requires
		// sampling from the working buffer.
		glUniform1i(uniformTexGColor, OGLTextureUnitID_FinalColor);
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderColorOut::_DestroyFramebufferOutput6665Programs()
{
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	if (this->_pgFramebufferRGBA6665OutputID != 0)
	{
		glDetachShader(this->_pgFramebufferRGBA6665OutputID, this->_vsFramebufferOutput6665ShaderID);
		glDetachShader(this->_pgFramebufferRGBA6665OutputID, this->_fsFramebufferRGBA6665OutputShaderID);
		glDeleteProgram(this->_pgFramebufferRGBA6665OutputID);
		this->_pgFramebufferRGBA6665OutputID = 0;
	}
	
	if (this->_vsFramebufferOutput6665ShaderID != 0)
	{
		glDeleteShader(this->_vsFramebufferOutput6665ShaderID);
		this->_vsFramebufferOutput6665ShaderID = 0;
	}
	
	if (this->_fsFramebufferRGBA6665OutputShaderID != 0)
	{
		glDeleteShader(this->_fsFramebufferRGBA6665OutputShaderID);
		this->_fsFramebufferRGBA6665OutputShaderID = 0;
	}
}

Render3DError OpenGLRenderColorOut::_CreateFramebufferOutput8888Program(const char *vtxShaderCString, const char *fragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	
	if ( (vtxShaderCString == NULL) || (fragShaderCString == NULL) )
	{
		return error;
	}
	
	std::stringstream shaderHeader;
	if (this->_feature.supportShaderFixedLocation)
	{
		shaderHeader << "#version 330\n";
		shaderHeader << "\n";
	}
	else if (this->_feature.variantID & OpenGLVariantFamily_CoreProfile)
	{
		shaderHeader << "#version 150\n";
		shaderHeader << "\n";
	}
	shaderHeader << "#define FRAMEBUFFER_SIZE_X " << this->_framebufferWidth  << ".0 \n";
	shaderHeader << "#define FRAMEBUFFER_SIZE_Y " << this->_framebufferHeight << ".0 \n";
	shaderHeader << "\n";
	
	std::stringstream vsHeader;
	if (this->_feature.supportShaderFixedLocation)
	{
		vsHeader << "#define IN_VTX_POSITION layout (location = "  << OGLVertexAttributeID_Position  << ") in\n";
		vsHeader << "#define IN_VTX_TEXCOORD0 layout (location = " << OGLVertexAttributeID_TexCoord0 << ") in\n";
		vsHeader << "#define IN_VTX_COLOR layout (location = "     << OGLVertexAttributeID_Color     << ") in\n";
		vsHeader << "#define OUT_VARYING out\n";
	}
	else if (this->_feature.variantID & OpenGLVariantFamily_CoreProfile)
	{
		vsHeader << "#define IN_VTX_POSITION in\n";
		vsHeader << "#define IN_VTX_TEXCOORD0 in\n";
		vsHeader << "#define IN_VTX_COLOR in\n";
		vsHeader << "#define OUT_VARYING out\n";
	}
	else
	{
		vsHeader << "#define IN_VTX_POSITION attribute\n";
		vsHeader << "#define IN_VTX_TEXCOORD0 attribute\n";
		vsHeader << "#define IN_VTX_COLOR attribute\n";
		vsHeader << "#define OUT_VARYING varying\n";
	}
	
	std::stringstream fsHeader;
	if (this->_feature.supportShaderFixedLocation)
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture(t,c)\n";
		fsHeader << "#define IN_VARYING in\n";
		fsHeader << "#define OUT_COLOR layout (location = 0) out\n";
		fsHeader << "#define FRAG_COLOR_VAR outFragColor\n\n";
		fsHeader << "OUT_COLOR vec4 FRAG_COLOR_VAR;\n\n";
	}
	else if (this->_feature.variantID & OpenGLVariantFamily_CoreProfile)
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture(t,c)\n";
		fsHeader << "#define IN_VARYING in\n";
		fsHeader << "#define OUT_COLOR out\n";
		fsHeader << "#define FRAG_COLOR_VAR outFragColor\n\n";
		fsHeader << "OUT_COLOR vec4 FRAG_COLOR_VAR;\n\n";
	}
	else
	{
		fsHeader << "#define SAMPLE4_TEX_2D(t,c) texture2D(t,c)\n";
		fsHeader << "#define IN_VARYING varying\n";
		fsHeader << "#define OUT_COLOR varying\n";
		fsHeader << "#define FRAG_COLOR_VAR gl_FragColor\n\n";
	}
	
	std::string vtxShaderCode  = shaderHeader.str() + vsHeader.str() + std::string(vtxShaderCString);
	std::string fragShaderCode = shaderHeader.str() + fsHeader.str() + std::string(fragShaderCString);
	
	error = ShaderProgramCreateOGL(this->_vsFramebufferOutput8888ShaderID,
	                               this->_fsFramebufferRGBA8888OutputShaderID,
	                               this->_pgFramebufferRGBA8888OutputID,
	                               vtxShaderCode.c_str(),
								   fragShaderCode.c_str());
	if (error != OGLERROR_NOERR)
	{
		INFO("OpenGL: Failed to create the FRAMEBUFFER OUTPUT RGBA8888 shader program.\n");
		glUseProgram(0);
		this->_DestroyFramebufferOutput8888Programs();
		return error;
	}
	
	glBindAttribLocation(this->_pgFramebufferRGBA8888OutputID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(this->_pgFramebufferRGBA8888OutputID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	glLinkProgram(this->_pgFramebufferRGBA8888OutputID);
	if (!ValidateShaderProgramLinkOGL(this->_pgFramebufferRGBA8888OutputID))
	{
		INFO("OpenGL: Failed to link the FRAMEBUFFER OUTPUT RGBA8888 shader program.\n");
		glUseProgram(0);
		this->_DestroyFramebufferOutput8888Programs();
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(this->_pgFramebufferRGBA8888OutputID);
	glUseProgram(this->_pgFramebufferRGBA8888OutputID);
	
	const GLint uniformTexGColor = glGetUniformLocation(this->_pgFramebufferRGBA8888OutputID, "texInFragColor");
	if (this->_feature.supportFBO)
	{
		glUniform1i(uniformTexGColor, OGLTextureUnitID_GColor);
	}
	else
	{
		// Reading back the output framebuffer without FBOs requires
		// sampling from the working buffer.
		glUniform1i(uniformTexGColor, OGLTextureUnitID_FinalColor);
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderColorOut::_DestroyFramebufferOutput8888Programs()
{
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	if (this->_pgFramebufferRGBA8888OutputID != 0)
	{
		glDetachShader(this->_pgFramebufferRGBA8888OutputID, this->_vsFramebufferOutput8888ShaderID);
		glDetachShader(this->_pgFramebufferRGBA8888OutputID, this->_fsFramebufferRGBA8888OutputShaderID);
		glDeleteProgram(this->_pgFramebufferRGBA8888OutputID);
		this->_pgFramebufferRGBA8888OutputID = 0;
	}
	
	if (this->_vsFramebufferOutput8888ShaderID != 0)
	{
		glDeleteShader(this->_vsFramebufferOutput8888ShaderID);
		this->_vsFramebufferOutput8888ShaderID = 0;
	}
	
	if (this->_fsFramebufferRGBA8888OutputShaderID != 0)
	{
		glDeleteShader(this->_fsFramebufferRGBA8888OutputShaderID);
		this->_fsFramebufferRGBA8888OutputShaderID = 0;
	}
}

Render3DError OpenGLRenderColorOut::_FramebufferConvertColorFormat()
{
	if (  this->_willConvertColorOnGPU &&
		((this->_format == NDSColorFormat_BGR666_Rev) || (this->_feature.readPixelsBestFormat == GL_BGRA)) )
	{
		const GLuint convertProgramID = (this->_format == NDSColorFormat_BGR666_Rev) ? this->_pgFramebufferRGBA6665OutputID : this->_pgFramebufferRGBA8888OutputID;
		glUseProgram(convertProgramID);
		
		if (this->_feature.supportFBO)
		{
			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, this->_fboRenderID);
			glReadBuffer(OGL_WORKING_ATTACHMENT_ID);
			glDrawBuffer(OGL_WORKING_ATTACHMENT_ID);
		}
		else
		{
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FinalColor);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
			glActiveTexture(GL_TEXTURE0);
		}
		
		glViewport(0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		
		FramebufferProcessVertexAttribEnableLEGACYOGL(this->_feature, this->_vaoPostprocessStatesID, this->_vboPostprocessVtxID);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		FramebufferProcessVertexAttribDisableLEGACYOGL(this->_feature);
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderColorOut::Reset()
{
	this->FillZero();
}

size_t OpenGLRenderColorOut::BindRead32()
{
	const size_t oldReadingIdx32 = this->_currentReadingIdx32;
	
	if (this->_currentUsageIdx != RENDER3D_RESOURCE_INDEX_NONE)
	{
		this->_state[this->_currentUsageIdx] = AsyncReadState_Reading;
		this->_currentReadingIdx32 = this->_currentUsageIdx;
		this->_currentUsageIdx = RENDER3D_RESOURCE_INDEX_NONE;
	}
	else if (this->_currentReadyIdx != RENDER3D_RESOURCE_INDEX_NONE)
	{
		this->_state[this->_currentReadyIdx] = AsyncReadState_Reading;
		this->_currentReadingIdx32 = this->_currentReadyIdx;
		this->_currentReadyIdx = RENDER3D_RESOURCE_INDEX_NONE;
	}
	
	if ( (oldReadingIdx32 != RENDER3D_RESOURCE_INDEX_NONE) && (this->_currentReadingIdx32 != oldReadingIdx32) )
	{
		this->_state[oldReadingIdx32] = AsyncReadState_Free;
		
		if (this->_pbo[oldReadingIdx32] != 0)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[oldReadingIdx32]);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			this->_buffer32[oldReadingIdx32] = NULL;
		}
	}
	
	// Do not proceed if we can't bind a buffer for reading.
	if (this->_currentReadingIdx32 == RENDER3D_RESOURCE_INDEX_NONE)
	{
		return this->_currentReadingIdx32;
	}
	
	if (this->_pbo[this->_currentReadingIdx32] != 0)
	{
		if (this->_buffer32[this->_currentReadingIdx32] == NULL)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[this->_currentReadingIdx32]);
			this->_buffer32[this->_currentReadingIdx32] = this->_MapBuffer32OGL();
		}
	}
	else
	{
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
		if (this->_texColorOut[this->_currentReadingIdx32] != 0)
		{
			glBindTexture(GL_TEXTURE_RECTANGLE_ARB, this->_texColorOut[this->_currentReadingIdx32]);
			glGetTexImage(GL_TEXTURE_RECTANGLE_ARB, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, this->_bufferCPU32[this->_currentReadingIdx32]);
		}
		else
#endif
		{
			glReadPixels(0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, this->_buffer32[this->_currentReadingIdx32]);
		}
	}
	
	if (!this->_willConvertColorOnGPU)
	{
		if (this->_format == NDSColorFormat_BGR666_Rev)
		{
			if (this->_feature.readPixelsBestFormat == GL_BGRA)
			{
				ColorspaceConvertBuffer8888To6665< true, false>((u32 *)this->_buffer32[this->_currentReadingIdx32], (u32 *)this->_bufferCPU32[this->_currentReadingIdx32], this->_framebufferPixelCount);
			}
			else
			{
				ColorspaceConvertBuffer8888To6665<false, false>((u32 *)this->_buffer32[this->_currentReadingIdx32], (u32 *)this->_bufferCPU32[this->_currentReadingIdx32], this->_framebufferPixelCount);
			}
		}
		else if ( (this->_format == NDSColorFormat_BGR888_Rev) && (this->_feature.readPixelsBestFormat == GL_BGRA) )
		{
			ColorspaceCopyBuffer32<true, false>((u32 *)this->_buffer32[this->_currentReadingIdx32], (u32 *)this->_bufferCPU32[this->_currentReadingIdx32], this->_framebufferPixelCount);
		}
	}
	
	return this->_currentReadingIdx32;
}

size_t OpenGLRenderColorOut::UnbindRead32()
{
	size_t newFreeIdx = this->Render3DColorOut::UnbindRead32();
	
	if ( (newFreeIdx != RENDER3D_RESOURCE_INDEX_NONE) && (this->_pbo[newFreeIdx] != 0) )
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[newFreeIdx]);
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		this->_buffer32[newFreeIdx] = NULL;
	}
	
	return newFreeIdx;
}
   
size_t OpenGLRenderColorOut::BindRenderer()
{
	size_t idxFree = RENDER3D_RESOURCE_INDEX_NONE;
	
	if (this->_state[0] == AsyncReadState_Free)
	{
		idxFree = 0;
	}
	else if (this->_state[1] == AsyncReadState_Free)
	{
		idxFree = 1;
	}
	else if (this->_state[0] == AsyncReadState_Ready)
	{
		idxFree = 0;
		this->_currentReadyIdx = RENDER3D_RESOURCE_INDEX_NONE;
	}
	else if (this->_state[1] == AsyncReadState_Ready)
	{
		idxFree = 1;
		this->_currentReadyIdx = RENDER3D_RESOURCE_INDEX_NONE;
	}
	
	if (idxFree != RENDER3D_RESOURCE_INDEX_NONE)
	{
		this->_state[idxFree] = AsyncReadState_Using;
		this->_currentUsageIdx = idxFree;
	}
	
	return idxFree;
}

void OpenGLRenderColorOut::UnbindRenderer(const size_t idxRead)
{
	if ( (idxRead > 1) || (this->_state[idxRead] == AsyncReadState_Disabled) )
	{
		return;
	}
	
	this->_FramebufferConvertColorFormat();
	this->Render3DColorOut::UnbindRenderer(idxRead);
	
	if (this->_pbo[idxRead] != 0)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[idxRead]);
		glReadPixels(0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, 0);
	}
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
	else if (this->_texColorOut[idxRead] != 0)
	{
		glBindTexture(GL_TEXTURE_RECTANGLE_ARB, this->_texColorOut[idxRead]);
		glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, 0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
		glFlush(); // glCopyTexSubImage2D() doesn't do an implicit flush like glReadPixels() does, so we need to explicitly call glFlush() here.
	}
#endif
}

Render3DError OpenGLRenderColorOut::SetSize(size_t w, size_t h)
{
	if ( (w == 0) || (h == 0) )
	{
		return RENDER3DERROR_INVALID_VALUE;
	}
	
	if ( (w == this->_framebufferWidth) && (h == this->_framebufferHeight) )
	{
		return RENDER3DERROR_NOERR;
	}
	
	glFinish();
	
	this->_framebufferWidth      = w;
	this->_framebufferHeight     = h;
	this->_framebufferPixelCount = w * h;
	this->_framebufferSize16     = w * h * sizeof(u16);
	this->_framebufferSize32     = w * h * sizeof(Color4u8);
	
	Color4u8 *oldMasterBuffer32 = this->_masterBufferCPU32;
	Color4u8 *newMasterBuffer32 = NULL;
	
	if (oldMasterBuffer32 != NULL)
	{
		newMasterBuffer32 = (Color4u8 *)malloc_alignedPage(this->_framebufferSize32 * 2);
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
		if ( (this->_texColorOut[0] != 0) || (this->_texColorOut[1] != 0) )
		{
			glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB, (GLsizei)(this->_framebufferSize32 * 2), newMasterBuffer32);
			glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRUE);
		}
#endif
	}
	
	for (size_t i = 0; i < 2; i++)
	{
		if (this->_pbo[i] != 0)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[i]);
			
			if (this->_buffer32[i] != NULL)
			{
				glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			}
			
			glBufferData(GL_PIXEL_PACK_BUFFER, this->_framebufferSize32, NULL, GL_STREAM_READ);
			
			if (this->_buffer32[i] != NULL)
			{
				this->_buffer32[i] = this->_MapBuffer32OGL();
			}
		}
		
		if (this->_bufferCPU32[i] != NULL)
		{
			this->_bufferCPU32[i] = newMasterBuffer32 + (this->_framebufferPixelCount * i);
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
			if (this->_texColorOut[i] != 0)
			{
				glBindTexture(GL_TEXTURE_RECTANGLE_ARB, this->_texColorOut[i]);
				glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_SHARED_APPLE);
				glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, this->_bufferCPU32[i]);
			}
#endif
			if (this->_pbo[i] == 0)
			{
				this->_buffer32[i] = this->_bufferCPU32[i];
			}
		}
		
		free_aligned(this->_buffer16[i]);
		this->_buffer16[i] = (Color5551 *)malloc_alignedPage(this->_framebufferSize16);
	}
	
	if (oldMasterBuffer32 != NULL)
	{
#if defined(GL_APPLE_texture_range) && defined(GL_APPLE_client_storage)
		if ( (_texColorOut[0] != 0) || (_texColorOut[1] != 0) )
		{
			glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, GL_FALSE);
			glBindTexture(GL_TEXTURE_RECTANGLE_ARB, 0);
		}
#endif
		this->_masterBufferCPU32 = newMasterBuffer32;
		free_aligned(oldMasterBuffer32);
		oldMasterBuffer32 = NULL;
	}
	
	if (this->_feature.supportShaders)
	{
		// Recreate shaders that use the framebuffer size.
		glUseProgram(0);
		this->_DestroyFramebufferOutput6665Programs();
		this->_DestroyFramebufferOutput8888Programs();
		
		if (this->_feature.readPixelsBestFormat == GL_BGRA)
		{
			this->_CreateFramebufferOutput6665Program(FramebufferOutputVtxShader, FramebufferOutputBGRA6665FragShader);
			this->_CreateFramebufferOutput8888Program(FramebufferOutputVtxShader, FramebufferOutputBGRA8888FragShader);
		}
		else
		{
			this->_CreateFramebufferOutput6665Program(FramebufferOutputVtxShader, FramebufferOutputRGBA6665FragShader);
			// No need to create a shader program for RGBA8888 since no conversion is necessary in this case.
		}
	}
	
	glFinish();
	
	return RENDER3DERROR_NOERR;
}

const Color4u8* OpenGLRenderColorOut::GetFramebuffer32() const
{
	const bool didConvertOnCPU = !this->_willConvertColorOnGPU && ((this->_format == NDSColorFormat_BGR666_Rev) || (this->_feature.readPixelsBestFormat == GL_BGRA));
	
	if (this->_currentReadingIdx32 != RENDER3D_RESOURCE_INDEX_NONE)
	{
		if (this->_state[this->_currentReadingIdx32] == AsyncReadState_Reading)
		{
			if (didConvertOnCPU)
			{
				return this->_bufferCPU32[this->_currentReadingIdx32];
			}
			else
			{
				return this->_buffer32[this->_currentReadingIdx32];
			}
		}
	}
	else if (this->_currentUsageIdx != RENDER3D_RESOURCE_INDEX_NONE)
	{
		if (this->_state[this->_currentUsageIdx] == AsyncReadState_Using)
		{
			if (didConvertOnCPU)
			{
				return this->_bufferCPU32[this->_currentUsageIdx];
			}
			else
			{
				return this->_buffer32[this->_currentUsageIdx];
			}
		}
	}
	
	return NULL;
}

Render3DError OpenGLRenderColorOut::FillZero()
{
	Render3DError error = RENDER3DERROR_NOERR;
	size_t bufferIdx32 = RENDER3D_RESOURCE_INDEX_NONE;
	
	if (this->_currentReadingIdx32 != RENDER3D_RESOURCE_INDEX_NONE)
	{
		bufferIdx32 = this->_currentReadingIdx32;
	}
	else if (this->_currentReadyIdx != RENDER3D_RESOURCE_INDEX_NONE)
	{
		bufferIdx32 = this->_currentReadyIdx;
	}
	else if (this->_currentUsageIdx != RENDER3D_RESOURCE_INDEX_NONE)
	{
		bufferIdx32 = this->_currentUsageIdx;
	}
	else
	{
		// No buffer was or is in use, so there is nothing to modify.
		error = RENDER3DERROR_INVALID_BUFFER;
		return error;
	}
	
	if (this->_willConvertColorOnGPU || (this->_texColorOut[bufferIdx32] != 0))
	{
		if (!BEGINGL())
		{
			error = OGLERROR_BEGINGL_FAILED;
			return error;
		}
	}
	
	if ( (this->_pbo[bufferIdx32] != 0) &&
	      this->_willConvertColorOnGPU &&
	     (bufferIdx32 == this->_currentReadingIdx32) )
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, this->_pbo[bufferIdx32]);
		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);
		
		glReadPixels(0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, 0);
		this->_buffer32[bufferIdx32] = this->_MapBuffer32OGL();
	}
	else
	{
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	
	if (bufferIdx32 == this->_currentReadingIdx32)
	{
		this->_currentReadingIdx16 = bufferIdx32;
		memset(this->_buffer16[bufferIdx32], 0, this->_framebufferSize16);
		
		if (this->_bufferCPU32[bufferIdx32] != NULL)
		{
			memset(this->_bufferCPU32[bufferIdx32], 0, this->_framebufferSize32);
		}
	}
	
	if (this->_willConvertColorOnGPU || (this->_texColorOut[bufferIdx32] != 0))
	{
		ENDGL();
	}
	
	return error;
}

Render3DError OpenGLRenderColorOut::FillColor32(const Color4u8 *src, const bool isSrcNativeSize)
{
	// TODO: Implement this method.
	// Not a high priority, since the only function that calls this is gfx3d_FinishLoadStateBufferRead(),
	// which historically was never able to copy the saved framebuffer into OpenGL in the first place.
	return RENDER3DERROR_NOERR;
}

const OGLFeatureInfo& OpenGLRenderColorOut::GetFeatureInfo() const
{
	return this->_feature;
}

GLuint OpenGLRenderColorOut::GetFBORenderID() const
{
	return this->_fboRenderID;
}

void OpenGLRenderColorOut::SetFBORenderID(GLuint i)
{
	this->_fboRenderID = i;
}

OpenGLTexture::OpenGLTexture(TEXIMAGE_PARAM texAttributes, u32 palAttributes) : Render3DTexture(texAttributes, palAttributes)
{
	_cacheSize = GetUnpackSizeUsingFormat(TexFormat_32bpp);
	_invSizeS = 1.0f / (float)_sizeS;
	_invSizeT = 1.0f / (float)_sizeT;
	_isTexInited = false;
	
	_upscaleBuffer = NULL;
	
	glGenTextures(1, &_texID);
}

OpenGLTexture::~OpenGLTexture()
{
	glDeleteTextures(1, &this->_texID);
}

void OpenGLTexture::Load(bool forceTextureInit)
{
	u32 *textureSrc = (u32 *)this->_deposterizeSrcSurface.Surface;
	
	this->Unpack<TexFormat_32bpp>(textureSrc);
	
	if (this->_useDeposterize)
	{
		RenderDeposterize(this->_deposterizeSrcSurface, this->_deposterizeDstSurface);
	}
	
	glBindTexture(GL_TEXTURE_2D, this->_texID);
	
	switch (this->_scalingFactor)
	{
		case 1:
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			
			if (forceTextureInit || !this->_isTexInited)
			{
				this->_isTexInited = true;
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_sizeS, this->_sizeT, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
			}
			else
			{
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, this->_sizeS, this->_sizeT, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
			}
			break;
		}
			
		case 2:
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
			
			this->_Upscale<2>(textureSrc, this->_upscaleBuffer);
			
			if (forceTextureInit || !this->_isTexInited)
			{
				this->_isTexInited = true;
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_sizeS*2, this->_sizeT*2, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_upscaleBuffer);
				glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, this->_sizeS*1, this->_sizeT*1, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
			}
			else
			{
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, this->_sizeS*2, this->_sizeT*2, GL_RGBA, GL_UNSIGNED_BYTE, this->_upscaleBuffer);
				glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, this->_sizeS*1, this->_sizeT*1, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
			}
			break;
		}
			
		case 4:
		{
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
			
			this->_Upscale<4>(textureSrc, this->_upscaleBuffer);
			
			if (forceTextureInit || !this->_isTexInited)
			{
				this->_isTexInited = true;
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->_sizeS*4, this->_sizeT*4, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_upscaleBuffer);
				
				this->_Upscale<2>(textureSrc, this->_upscaleBuffer);
				glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, this->_sizeS*2, this->_sizeT*2, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->_upscaleBuffer);
				
				glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, this->_sizeS*1, this->_sizeT*1, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
			}
			else
			{
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, this->_sizeS*4, this->_sizeT*4, GL_RGBA, GL_UNSIGNED_BYTE, this->_upscaleBuffer);
				
				this->_Upscale<2>(textureSrc, this->_upscaleBuffer);
				glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, this->_sizeS*2, this->_sizeT*2, GL_RGBA, GL_UNSIGNED_BYTE, this->_upscaleBuffer);
				
				glTexSubImage2D(GL_TEXTURE_2D, 2, 0, 0, this->_sizeS*1, this->_sizeT*1, GL_RGBA, GL_UNSIGNED_BYTE, textureSrc);
			}
			break;
		}
			
		default:
			break;
	}
	
	this->_isLoadNeeded = false;
}

GLuint OpenGLTexture::GetID() const
{
	return this->_texID;
}

GLfloat OpenGLTexture::GetInvWidth() const
{
	return this->_invSizeS;
}

GLfloat OpenGLTexture::GetInvHeight() const
{
	return this->_invSizeT;
}

void OpenGLTexture::SetUnpackBuffer(void *unpackBuffer)
{
	this->_deposterizeSrcSurface.Surface = (unsigned char *)unpackBuffer;
}

void OpenGLTexture::SetDeposterizeBuffer(void *dstBuffer, void *workingBuffer)
{
	this->_deposterizeDstSurface.Surface = (unsigned char *)dstBuffer;
	this->_deposterizeDstSurface.workingSurface[0] = (unsigned char *)workingBuffer;
}

void OpenGLTexture::SetUpscalingBuffer(void *upscaleBuffer)
{
	this->_upscaleBuffer = (u32 *)upscaleBuffer;
}

template <OpenGLVariantID VARIANTID>
static Render3D* OpenGLRendererCreate()
{
	OpenGLRenderer *newRenderer = NULL;
	Render3DError error = OGLERROR_NOERR;
	CACHE_ALIGN char variantStringFull[32] = {0};
	CACHE_ALIGN char variantFamilyString[32] = {0};
	
	switch (VARIANTID)
	{
		case OpenGLVariantID_Legacy_1_2:
			strncpy(variantStringFull, "OpenGL 1.2 (forced)", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL", sizeof(variantFamilyString));
			break;

		case OpenGLVariantID_Legacy_2_0:
			strncpy(variantStringFull, "OpenGL 2.0 (forced)", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL", sizeof(variantFamilyString));
			break;

		case OpenGLVariantID_Legacy_2_1:
			strncpy(variantStringFull, "OpenGL 2.1 (forced)", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL", sizeof(variantFamilyString));
			break;

		case OpenGLVariantID_CoreProfile_3_2:
			strncpy(variantStringFull, "OpenGL 3.2 (forced)", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL", sizeof(variantFamilyString));
			break;

		case OpenGLVariantID_LegacyAuto:
		case OpenGLVariantID_StandardAuto:
			strncpy(variantStringFull, "OpenGL (auto)", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL", sizeof(variantFamilyString));
			break;

		case OpenGLVariantID_ES3_3_0:
			strncpy(variantStringFull, "OpenGL ES 3.0", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL ES", sizeof(variantFamilyString));
			break;

		default:
			strncpy(variantStringFull, "OpenGL UNKNOWN", sizeof(variantStringFull));
			strncpy(variantFamilyString, "OpenGL UNKNOWN", sizeof(variantFamilyString));
			break;
	}
	
	if (oglrender_init == NULL)
	{
		INFO("%s: oglrender_init is unassigned. Clients must assign this function pointer and have a working context before running OpenGL.\n",
		     variantStringFull);
		return newRenderer;
	}
	
	if (oglrender_beginOpenGL == NULL)
	{
		INFO("%s: oglrender_beginOpenGL is unassigned. Clients must assign this function pointer before running OpenGL.\n",
		     variantStringFull);
		return newRenderer;
	}
	
	if (oglrender_endOpenGL == NULL)
	{
		INFO("%s: oglrender_endOpenGL is unassigned. Clients must assign this function pointer before running OpenGL.\n",
		     variantStringFull);
		return newRenderer;
	}
	
	if (!oglrender_init())
	{
		return newRenderer;
	}
	
	__BeginOpenGL = oglrender_beginOpenGL;
	__EndOpenGL   = oglrender_endOpenGL;
	
	if (!BEGINGL())
	{
		INFO("%s: Could not initialize -- BEGINGL() failed.\n", variantStringFull);
		return newRenderer;
	}
	
	// Get OpenGL info
	const char *oglVersionString = (const char *)glGetString(GL_VERSION);
	const char *oglVendorString = (const char *)glGetString(GL_VENDOR);
	const char *oglRendererString = (const char *)glGetString(GL_RENDERER);

	// Writing to gl_FragDepth causes the driver to fail miserably on systems equipped
	// with a Intel G965 graphic card. Warn the user and fail gracefully.
	// http://forums.desmume.org/viewtopic.php?id=9286
	if(!strcmp(oglVendorString,"Intel") && strstr(oglRendererString,"965"))
	{
		INFO("%s: Incompatible graphic card detected. Disabling OpenGL support.\n",
			 variantStringFull);
		
		ENDGL();
		return newRenderer;
	}
	
	// Check the driver's OpenGL version
	OGLGetDriverVersion(oglVersionString, &_OGLDriverVersion.major, &_OGLDriverVersion.minor, &_OGLDriverVersion.revision);
	
	if (VARIANTID & OpenGLVariantFamily_Standard)
	{
		if (!IsOpenGLDriverVersionSupported(OGLRENDER_STANDARD_MINIMUM_CONTEXT_VERSION_REQUIRED_MAJOR,
		                                    OGLRENDER_STANDARD_MINIMUM_CONTEXT_VERSION_REQUIRED_MINOR,
		                                    OGLRENDER_STANDARD_MINIMUM_CONTEXT_VERSION_REQUIRED_REVISION))
		{
			INFO("%s: This context does not support OpenGL v%u.%u.%u or later. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantStringFull,
			     OGLRENDER_STANDARD_MINIMUM_CONTEXT_VERSION_REQUIRED_MAJOR,
			     OGLRENDER_STANDARD_MINIMUM_CONTEXT_VERSION_REQUIRED_MINOR,
			     OGLRENDER_STANDARD_MINIMUM_CONTEXT_VERSION_REQUIRED_REVISION,
			     oglVersionString, oglVendorString, oglRendererString);
			ENDGL();
			return newRenderer;
		}
	}
	else if (VARIANTID & OpenGLVariantFamily_ES)
	{
		if (!IsOpenGLDriverVersionSupported(OGLRENDER_ES_MINIMUM_CONTEXT_VERSION_REQUIRED_MAJOR,
		                                    OGLRENDER_ES_MINIMUM_CONTEXT_VERSION_REQUIRED_MINOR,
		                                    OGLRENDER_ES_MINIMUM_CONTEXT_VERSION_REQUIRED_REVISION))
		{
			INFO("%s: This context does not support OpenGL ES v%u.%u.%u or later. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantStringFull,
			     OGLRENDER_ES_MINIMUM_CONTEXT_VERSION_REQUIRED_MAJOR,
			     OGLRENDER_ES_MINIMUM_CONTEXT_VERSION_REQUIRED_MINOR,
			     OGLRENDER_ES_MINIMUM_CONTEXT_VERSION_REQUIRED_REVISION,
			     oglVersionString, oglVendorString, oglRendererString);
			ENDGL();
			return newRenderer;
		}
	}
	else
	{
		INFO("%s: This renderer does not support the OpenGL variant requested by this context. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
		     variantStringFull,
		     oglVersionString, oglVendorString, oglRendererString);
		ENDGL();
		return newRenderer;
	}
	
	// Create new OpenGL rendering object
	if (VARIANTID == OpenGLVariantID_ES3_3_0)
	{
		if ( (OGLLoadEntryPoints_ES_3_0_Func != NULL) && (OGLCreateRenderer_ES_3_0_Func != NULL) )
		{
			OGLLoadEntryPoints_ES_3_0_Func();
			OGLLoadEntryPoints_Legacy();
			OGLCreateRenderer_ES_3_0_Func(&newRenderer);
		}
		else
		{
			ENDGL();
			return newRenderer;
		}
	}
	else
	{
		if ( (VARIANTID == OpenGLVariantID_StandardAuto) || (VARIANTID == OpenGLVariantID_CoreProfile_3_2) )
		{
			if ( (OGLLoadEntryPoints_3_2_Func != NULL) && (OGLCreateRenderer_3_2_Func != NULL) )
			{
				OGLLoadEntryPoints_3_2_Func();
				OGLLoadEntryPoints_Legacy(); //zero 04-feb-2013 - this seems to be necessary as well
				OGLCreateRenderer_3_2_Func(&newRenderer);
			}
			else if (VARIANTID == OpenGLVariantID_CoreProfile_3_2)
			{
				ENDGL();
				return newRenderer;
			}
		}
		
		if ( (VARIANTID == OpenGLVariantID_StandardAuto) || (VARIANTID == OpenGLVariantID_LegacyAuto) )
		{
			// If the renderer doesn't initialize with OpenGL v3.2 or higher, fall back
			// to one of the lower versions.
			if (newRenderer == NULL)
			{
				OGLLoadEntryPoints_Legacy();
				if (IsOpenGLDriverVersionSupported(2, 1, 0))
				{
					newRenderer = new OpenGLRenderer_2_1;
				}
				else if (IsOpenGLDriverVersionSupported(2, 0, 0))
				{
					newRenderer = new OpenGLRenderer_2_0;
				}
				else if (IsOpenGLDriverVersionSupported(1, 2, 0))
				{
					newRenderer = new OpenGLRenderer_1_2;
				}
			}
		}
		else if ( (VARIANTID == OpenGLVariantID_Legacy_2_1) && IsOpenGLDriverVersionSupported(2, 1, 0) )
		{
			OGLLoadEntryPoints_Legacy();
			newRenderer = new OpenGLRenderer_2_1;
		}
		else if ( (VARIANTID == OpenGLVariantID_Legacy_2_0) && IsOpenGLDriverVersionSupported(2, 0, 0) )
		{
			OGLLoadEntryPoints_Legacy();
			newRenderer = new OpenGLRenderer_2_0;
		}
		else if ( (VARIANTID == OpenGLVariantID_Legacy_1_2) && IsOpenGLDriverVersionSupported(1, 2, 0) )
		{
			OGLLoadEntryPoints_Legacy();
			newRenderer = new OpenGLRenderer_1_2;
		}
	}
	
	if (newRenderer == NULL)
	{
		INFO("%s: Renderer did not initialize. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
		     variantStringFull,
		     oglVersionString, oglVendorString, oglRendererString);
		
		ENDGL();
		return newRenderer;
	}
	
	// Initialize OpenGL extensions
	error = newRenderer->InitExtensions();
	if (error != OGLERROR_NOERR)
	{
		if (error == OGLERROR_DRIVER_VERSION_TOO_OLD)
		{
			INFO("%s: This driver does not support the minimum feature set required to run this renderer. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantFamilyString,
			     oglVersionString, oglVendorString, oglRendererString);
		}
		else if (newRenderer->IsVersionSupported(1, 5, 0) && (error == OGLERROR_VBO_UNSUPPORTED))
		{
			INFO("%s: VBOs are not available, even though this version of OpenGL requires them. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantFamilyString,
			     oglVersionString, oglVendorString, oglRendererString);
		}
		else if ( newRenderer->IsVersionSupported(2, 0, 0) &&
		         (error == OGLERROR_SHADER_CREATE_ERROR ||
		          error == OGLERROR_VERTEX_SHADER_PROGRAM_LOAD_ERROR ||
		          error == OGLERROR_FRAGMENT_SHADER_PROGRAM_LOAD_ERROR) )
		{
			INFO("%s: Shaders are not working, even though they should be on this version of OpenGL. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantFamilyString,
			     oglVersionString, oglVendorString, oglRendererString);
		}
		else if (newRenderer->IsVersionSupported(2, 1, 0) && (error == OGLERROR_PBO_UNSUPPORTED))
		{
			INFO("%s: PBOs are not available, even though this version of OpenGL requires them. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantFamilyString,
			     oglVersionString, oglVendorString, oglRendererString);
		}
		else if ( newRenderer->IsVersionSupported(3, 0, 0) && (error == OGLERROR_FBO_CREATE_ERROR) &&
		         ((OGLLoadEntryPoints_3_2_Func != NULL) || (OGLLoadEntryPoints_ES_3_0_Func != NULL)) )
		{
			INFO("%s: FBOs are not available, even though this version of OpenGL requires them. Disabling 3D renderer.\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
			     variantFamilyString,
			     oglVersionString, oglVendorString, oglRendererString);
		}
		
		delete newRenderer;
		newRenderer = NULL;
		
		ENDGL();
		return newRenderer;
	}
	
	ENDGL();
	
	// Initialization finished -- reset the renderer
	newRenderer->Reset();
	
	const OGLFeatureInfo &feature = newRenderer->GetFeatureInfo();
	const int vMajor = (feature.variantID >> 4) & 0x000F;
	const int vMinor = feature.variantID & 0x000F;
	const int vRevision = 0;
	
	INFO("%s: Renderer initialized successfully (v%i.%i.%i).\n[ Context Info -\n    Version: %s\n    Vendor: %s\n    Renderer: %s ]\n",
	     variantFamilyString, vMajor, vMinor, vRevision, oglVersionString, oglVendorString, oglRendererString);
	
	return newRenderer;
}

static void OpenGLRendererDestroy()
{
	if (!BEGINGL())
		return;
	
	if (CurrentRenderer != BaseRenderer)
	{
		OpenGLRenderer *oldRenderer = (OpenGLRenderer *)CurrentRenderer;
		CurrentRenderer = BaseRenderer;
		delete oldRenderer;
	}
	
	ENDGL();
	
	__BeginOpenGL = NULL;
	__EndOpenGL   = NULL;
	
	if (oglrender_deinit != NULL)
	{
		oglrender_deinit();
	}
}

//automatically select 3.2 or old profile depending on whether 3.2 is available
GPU3DInterface gpu3Dgl = {
	"OpenGL",
	OpenGLRendererCreate<OpenGLVariantID_StandardAuto>,
	OpenGLRendererDestroy
};

//forcibly use old profile
GPU3DInterface gpu3DglOld = {
	"OpenGL Old",
	OpenGLRendererCreate<OpenGLVariantID_LegacyAuto>,
	OpenGLRendererDestroy
};

//forcibly use new profile
GPU3DInterface gpu3Dgl_3_2 = {
	"OpenGL 3.2",
	OpenGLRendererCreate<OpenGLVariantID_CoreProfile_3_2>,
	OpenGLRendererDestroy
};

// OpenGL ES 3.0 (this is the only version of ES that is supported right now)
GPU3DInterface gpu3Dgl_ES_3_0 = {
	"OpenGL ES 3.0",
	OpenGLRendererCreate<OpenGLVariantID_ES3_3_0>,
	OpenGLRendererDestroy
};

OpenGLRenderer::OpenGLRenderer()
{
	memset(&_feature, 0, sizeof(OGLFeatureInfo));
	_feature.variantID = OpenGLVariantID_LegacyAuto;
	
	_deviceInfo.renderID = RENDERID_OPENGL_AUTO;
	_deviceInfo.renderName = "OpenGL";
	_deviceInfo.isTexturingSupported = true;
	_deviceInfo.isEdgeMarkSupported = true;
	_deviceInfo.isFogSupported = true;
	_deviceInfo.isTextureSmoothingSupported = true;
	_deviceInfo.maxAnisotropy = 1.0f;
	_deviceInfo.maxSamples = 0;
	
	_internalRenderingFormat = NDSColorFormat_BGR888_Rev;
	
	_isDepthLEqualPolygonFacingSupported = false;
	_willUseMultisampleShaders = false;
	
	_emulateShadowPolygon = true;
	_emulateSpecialZeroAlphaBlending = true;
	_emulateNDSDepthCalculation = true;
	_emulateDepthLEqualPolygonFacing = false;
	
	// Init OpenGL rendering states
	ref = (OGLRenderRef *)malloc_alignedPage(sizeof(OGLRenderRef));
	memset(ref, 0, sizeof(OGLRenderRef));
	
	_colorOut = NULL;
	_lastBoundColorOut = RENDER3D_RESOURCE_INDEX_NONE;
	
	_workingTextureUnpackBuffer = (Color4u8 *)malloc_alignedPage(1024 * 1024 * sizeof(Color4u8));
	_needsZeroDstAlphaPass = true;
	_currentPolyIndex = 0;
	_enableAlphaBlending = true;
	_geometryProgramFlags.value = 0;
	_fogProgramKey.key = 0;
	_fogProgramMap.clear();
	_clearImageIndex = 0;
	
	_geometryAttachmentWorkingBuffer = NULL;
	_geometryAttachmentPolyID = NULL;
	_geometryAttachmentFogAttributes = NULL;
	
	memset(&_pendingRenderStates, 0, sizeof(_pendingRenderStates));
}

OpenGLRenderer::~OpenGLRenderer()
{
	free_aligned(this->_workingTextureUnpackBuffer);
	
	delete this->_colorOut;
	this->_colorOut = NULL;
	
	// Destroy OpenGL rendering states
	free_aligned(this->ref);
	this->ref = NULL;
}

const OGLFeatureInfo& OpenGLRenderer::GetFeatureInfo() const
{
	return this->_feature;
}

bool OpenGLRenderer::IsExtensionPresent(const std::set<std::string> *oglExtensionSet, const std::string extensionName) const
{
	if (oglExtensionSet == NULL || oglExtensionSet->size() == 0)
	{
		return false;
	}
	
	return (oglExtensionSet->find(extensionName) != oglExtensionSet->end());
}

bool OpenGLRenderer::IsVersionSupported(unsigned int checkVersionMajor, unsigned int checkVersionMinor, unsigned int checkVersionRevision) const
{
	bool result = false;
	
	const unsigned int vMajor = (this->_feature.variantID >> 4) & 0x000F;
	const unsigned int vMinor = this->_feature.variantID & 0x000F;
	const unsigned int vRevision = 0;
	
	if ( (vMajor > checkVersionMajor) ||
	     (vMajor >= checkVersionMajor && vMinor > checkVersionMinor) ||
	     (vMajor >= checkVersionMajor && vMinor >= checkVersionMinor && vRevision >= checkVersionRevision) )
	{
		result = true;
	}
	
	return result;
}

GLsizei OpenGLRenderer::GetLimitedMultisampleSize() const
{
	u32 deviceMultisamples = this->_deviceInfo.maxSamples;
	u32 workingMultisamples = (u32)this->_selectedMultisampleSize;
	
	if (workingMultisamples == 1)
	{
		// If this->_selectedMultisampleSize is 1, then just set workingMultisamples to 2
		// by default. This is done to prevent the multisampled FBOs from being resized to
		// a meaningless sample size of 1.
		//
		// As an side, if someone wants to bring back automatic MSAA sample size selection
		// in the future, then this would be the place to reimplement it.
		workingMultisamples = 2;
	}
	else
	{
		// Ensure that workingMultisamples is a power-of-two, which is what OpenGL likes.
		//
		// If workingMultisamples is not a power-of-two, then workingMultisamples is
		// increased to the next largest power-of-two.
		workingMultisamples--;
		workingMultisamples |= workingMultisamples >> 1;
		workingMultisamples |= workingMultisamples >> 2;
		workingMultisamples |= workingMultisamples >> 4;
		workingMultisamples |= workingMultisamples >> 8;
		workingMultisamples |= workingMultisamples >> 16;
		workingMultisamples++;
	}
	
	if (deviceMultisamples > workingMultisamples)
	{
		deviceMultisamples = workingMultisamples;
	}
	
	return (GLsizei)deviceMultisamples;
}

OpenGLTexture* OpenGLRenderer::GetLoadedTextureFromPolygon(const POLY &thePoly, bool enableTexturing)
{
	OpenGLTexture *theTexture = (OpenGLTexture *)texCache.GetTexture(thePoly.texParam, thePoly.texPalette);
	const bool isNewTexture = (theTexture == NULL);
	
	if (isNewTexture)
	{
		theTexture = new OpenGLTexture(thePoly.texParam, thePoly.texPalette);
		theTexture->SetUnpackBuffer(this->_workingTextureUnpackBuffer);
		
		texCache.Add(theTexture);
	}
	
	const NDSTextureFormat packFormat = theTexture->GetPackFormat();
	const bool isTextureEnabled = ( (packFormat != TEXMODE_NONE) && enableTexturing );
	
	theTexture->SetSamplingEnabled(isTextureEnabled);
	
	if (theTexture->IsLoadNeeded() && isTextureEnabled)
	{
		const size_t previousScalingFactor = theTexture->GetScalingFactor();
		
		theTexture->SetDeposterizeBuffer(this->_workingTextureUnpackBuffer, this->_textureDeposterizeDstSurface.workingSurface[0]);
		theTexture->SetUpscalingBuffer(this->_textureUpscaleBuffer);
		
		theTexture->SetUseDeposterize(this->_enableTextureDeposterize);
		theTexture->SetScalingFactor(this->_textureScalingFactor);
		
		theTexture->Load(isNewTexture || (previousScalingFactor != this->_textureScalingFactor));
	}
	
	return theTexture;
}

template <OGLPolyDrawMode DRAWMODE>
size_t OpenGLRenderer::DrawPolygonsForIndexRange(const POLY *rawPolyList, const CPoly *clippedPolyList, const size_t clippedPolyCount, size_t firstIndex, size_t lastIndex, size_t &indexOffset, POLYGON_ATTR &lastPolyAttr)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (lastIndex > (clippedPolyCount - 1))
	{
		lastIndex = clippedPolyCount - 1;
	}
	
	if (firstIndex > lastIndex)
	{
		return 0;
	}
	
	// Map GFX3D_QUADS and GFX3D_QUAD_STRIP to GL_TRIANGLES since we will convert them.
	//
	// Also map GFX3D_TRIANGLE_STRIP to GL_TRIANGLES. This is okay since this is actually
	// how the POLY struct stores triangle strip vertices, which is in sets of 3 vertices
	// each. This redefinition is necessary since uploading more than 3 indices at a time
	// will cause glDrawElements() to draw the triangle strip incorrectly.
	static const GLenum oglPrimitiveType[]	= {
		GL_TRIANGLES, GL_TRIANGLES, GL_TRIANGLES, GL_TRIANGLES, GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_STRIP, GL_LINE_STRIP, // Normal polygons
		GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_LOOP, GL_LINE_LOOP    // Wireframe polygons
	};
	
	static const GLsizei indexIncrementLUT[] = {
		3, 6, 3, 6, 3, 4, 3, 4, // Normal polygons
		3, 4, 3, 4, 3, 4, 3, 4  // Wireframe polygons
	};
	
	// Set up the initial polygon
	const CPoly &initialClippedPoly = clippedPolyList[firstIndex];
	const POLY &initialRawPoly = rawPolyList[initialClippedPoly.index];
	TEXIMAGE_PARAM lastTexParams = initialRawPoly.texParam;
	u32 lastTexPalette = initialRawPoly.texPalette;
	GFX3D_Viewport lastViewport = initialRawPoly.viewport;
	
	this->SetupTexture(initialRawPoly, firstIndex);
	this->SetupViewport(initialRawPoly.viewport);
	
	// Enumerate through all polygons and render
	GLsizei vertIndexCount = 0;
	GLushort *indexBufferPtr = (this->_feature.supportVBO) ? (GLushort *)NULL + indexOffset : OGLRef.vertIndexBuffer + indexOffset;
	
	for (size_t i = firstIndex; i <= lastIndex; i++)
	{
		const CPoly &clippedPoly = clippedPolyList[i];
		const POLY &rawPoly = rawPolyList[clippedPoly.index];
		
		// Set up the polygon if it changed
		if (lastPolyAttr.value != rawPoly.attribute.value)
		{
			lastPolyAttr = rawPoly.attribute;
			this->SetupPolygon(rawPoly, (DRAWMODE != OGLPolyDrawMode_DrawOpaquePolys), (DRAWMODE != OGLPolyDrawMode_ZeroAlphaPass), clippedPoly.isPolyBackFacing);
		}
		
		// Set up the texture if it changed
		if (lastTexParams.value != rawPoly.texParam.value || lastTexPalette != rawPoly.texPalette)
		{
			lastTexParams = rawPoly.texParam;
			lastTexPalette = rawPoly.texPalette;
			this->SetupTexture(rawPoly, i);
		}
		
		// Set up the viewport if it changed
		if (lastViewport.value != rawPoly.viewport.value)
		{
			lastViewport = rawPoly.viewport;
			this->SetupViewport(rawPoly.viewport);
		}
		
		// In wireframe mode, redefine all primitives as GL_LINE_LOOP rather than
		// setting the polygon mode to GL_LINE though glPolygonMode(). Not only is
		// drawing more accurate this way, but it also allows GFX3D_QUADS and
		// GFX3D_QUAD_STRIP primitives to properly draw as wireframe without the
		// extra diagonal line.
		const size_t LUTIndex = (!GFX3D_IsPolyWireframe(rawPoly)) ? rawPoly.vtxFormat : (0x08 | rawPoly.vtxFormat);
		const GLenum polyPrimitive = oglPrimitiveType[LUTIndex];
		
		// Increment the vertex count
		vertIndexCount += indexIncrementLUT[LUTIndex];
		
		// Look ahead to the next polygon to see if we can simply buffer the indices
		// instead of uploading them now. We can buffer if all polygon states remain
		// the same and we're not drawing a line loop or line strip.
		if (i+1 <= lastIndex)
		{
			const CPoly &nextClippedPoly = clippedPolyList[i+1];
			const POLY &nextRawPoly = rawPolyList[nextClippedPoly.index];
			
			if (lastPolyAttr.value == nextRawPoly.attribute.value &&
				lastTexParams.value == nextRawPoly.texParam.value &&
				lastTexPalette == nextRawPoly.texPalette &&
				lastViewport.value == nextRawPoly.viewport.value &&
				polyPrimitive == oglPrimitiveType[nextRawPoly.vtxFormat] &&
				polyPrimitive != GL_LINE_LOOP &&
				polyPrimitive != GL_LINE_STRIP &&
				oglPrimitiveType[nextRawPoly.vtxFormat] != GL_LINE_LOOP &&
				oglPrimitiveType[nextRawPoly.vtxFormat] != GL_LINE_STRIP &&
				clippedPoly.isPolyBackFacing == nextClippedPoly.isPolyBackFacing)
			{
				continue;
			}
		}
		
		// Render the polygons
		this->SetPolygonIndex(i);
		
		if (rawPoly.attribute.Mode == POLYGON_MODE_SHADOW)
		{
			if ((DRAWMODE != OGLPolyDrawMode_ZeroAlphaPass) && this->_emulateShadowPolygon)
			{
				this->DrawShadowPolygon(polyPrimitive,
				                        vertIndexCount,
				                        indexBufferPtr,
				                        rawPoly.attribute.DepthEqualTest_Enable,
				                        rawPoly.attribute.TranslucentDepthWrite_Enable,
				                        (DRAWMODE == OGLPolyDrawMode_DrawTranslucentPolys), rawPoly.attribute.PolygonID);
			}
		}
		else if ( (rawPoly.texParam.PackedFormat == TEXMODE_A3I5) || (rawPoly.texParam.PackedFormat == TEXMODE_A5I3) )
		{
			this->DrawAlphaTexturePolygon<DRAWMODE>(polyPrimitive,
			                                        vertIndexCount,
			                                        indexBufferPtr,
			                                        rawPoly.attribute.DepthEqualTest_Enable,
			                                        rawPoly.attribute.TranslucentDepthWrite_Enable,
			                                        GFX3D_IsPolyWireframe(rawPoly) || GFX3D_IsPolyOpaque(rawPoly),
			                                        rawPoly.attribute.PolygonID,
			                                        !clippedPoly.isPolyBackFacing);
		}
		else
		{
			this->DrawOtherPolygon<DRAWMODE>(polyPrimitive,
			                                 vertIndexCount,
			                                 indexBufferPtr,
			                                 rawPoly.attribute.DepthEqualTest_Enable,
			                                 rawPoly.attribute.TranslucentDepthWrite_Enable,
			                                 rawPoly.attribute.PolygonID,
			                                 !clippedPoly.isPolyBackFacing);
		}
		
		indexBufferPtr += vertIndexCount;
		indexOffset += vertIndexCount;
		vertIndexCount = 0;
	}
	
	return indexOffset;
}

template <OGLPolyDrawMode DRAWMODE>
Render3DError OpenGLRenderer::DrawAlphaTexturePolygon(const GLenum polyPrimitive,
													  const GLsizei vertIndexCount,
													  const GLushort *indexBufferPtr,
													  const bool performDepthEqualTest,
													  const bool enableAlphaDepthWrite,
													  const bool canHaveOpaqueFragments,
													  const u8 opaquePolyID,
													  const bool isPolyFrontFacing)
{
	const OGLRenderRef &OGLRef = *this->ref;
	
	if (this->_feature.supportShaders)
	{
		if ((DRAWMODE != OGLPolyDrawMode_ZeroAlphaPass) && performDepthEqualTest && this->_emulateNDSDepthCalculation)
		{
			if (DRAWMODE == OGLPolyDrawMode_DrawTranslucentPolys)
			{
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthMask(GL_FALSE);
				
				// Use the stencil buffer to determine which fragments pass the lower-side tolerance.
				glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
				glDepthFunc(GL_LEQUAL);
				glStencilFunc(GL_ALWAYS, 0x80, 0x80);
				glStencilOp(GL_ZERO, GL_ZERO, GL_REPLACE);
				glStencilMask(0x80);
				
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				if (canHaveOpaqueFragments)
				{
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
				}
				
				// Use the stencil buffer to determine which fragments pass the higher-side tolerance.
				glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)-DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
				glDepthFunc(GL_GEQUAL);
				glStencilFunc(GL_EQUAL, 0x80, 0x80);
				glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
				glStencilMask(0x80);
				
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				if (canHaveOpaqueFragments)
				{
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
				}
				
				// Set up the actual drawing of the polygon, using the mask within the stencil buffer to determine which fragments should pass.
				glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], 0.0f);
				glDepthFunc(GL_ALWAYS);
				
				// First do the transparent polygon ID check for the translucent fragments.
				glStencilFunc(GL_NOTEQUAL, 0x40 | opaquePolyID, 0x7F);
				glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
				glStencilMask(0x80);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				// Draw the translucent fragments.
				glStencilFunc(GL_EQUAL, 0xC0 | opaquePolyID, 0x80);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glStencilMask(0x7F);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask((enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
				
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				// Draw the opaque fragments if they might exist.
				if (canHaveOpaqueFragments)
				{
					glStencilFunc(GL_EQUAL, 0x80 | opaquePolyID, 0x80);
					glDepthMask(GL_TRUE);
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
				}
				
				// Clear bit 7 (0x80) now so that future polygons don't get confused.
				glStencilFunc(GL_ALWAYS, 0x80, 0x80);
				glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
				glStencilMask(0x80);
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthMask(GL_FALSE);
				
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				if (canHaveOpaqueFragments)
				{
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
				}
				
				// Finally, reset the rendering states.
				glStencilFunc(GL_NOTEQUAL, 0x40 | opaquePolyID, 0x7F);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glStencilMask(0xFF);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask((enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
			}
			else
			{
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthMask(GL_FALSE);
				
				glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
				
				// Use the stencil buffer to determine which fragments pass the lower-side tolerance.
				glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
				glDepthFunc(GL_LEQUAL);
				glStencilFunc(GL_ALWAYS, 0x80, 0x80);
				glStencilOp(GL_ZERO, GL_ZERO, GL_REPLACE);
				glStencilMask(0x80);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				// Use the stencil buffer to determine which fragments pass the higher-side tolerance.
				glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)-DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
				glDepthFunc(GL_GEQUAL);
				glStencilFunc(GL_EQUAL, 0x80, 0x80);
				glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
				glStencilMask(0x80);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				// Set up the actual drawing of the polygon, using the mask within the stencil buffer to determine which fragments should pass.
				glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], 0.0f);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask(GL_TRUE);
				glDepthFunc(GL_ALWAYS);
				glStencilFunc(GL_EQUAL, 0x80 | opaquePolyID, 0x80);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glStencilMask(0x7F);
				
				// Draw the polygon as completely opaque.
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				// Clear bit 7 (0x80) now so that future polygons don't get confused.
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthMask(GL_FALSE);
				
				glStencilFunc(GL_ALWAYS, 0x80, 0x80);
				glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
				glStencilMask(0x80);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				// Finally, reset the rendering states.
				glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glStencilMask(0xFF);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask(GL_TRUE);
				
				glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
			}
		}
		else if (DRAWMODE != OGLPolyDrawMode_DrawOpaquePolys)
		{
			// Draw the translucent fragments.
			if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported && isPolyFrontFacing)
			{
				glDepthFunc(GL_EQUAL);
				glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[this->_geometryProgramFlags.value], GL_TRUE);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				glDepthFunc(GL_LESS);
				glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[this->_geometryProgramFlags.value], GL_FALSE);
			}
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			
			// Draw the opaque fragments if they might exist.
			if (canHaveOpaqueFragments)
			{
				if (DRAWMODE != OGLPolyDrawMode_ZeroAlphaPass)
				{
					glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
					glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
					glDepthMask(GL_TRUE);
				}
				
				glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
				if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported && isPolyFrontFacing)
				{
					glDepthFunc(GL_EQUAL);
					glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[this->_geometryProgramFlags.value], GL_TRUE);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					
					glDepthFunc(GL_LESS);
					glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[this->_geometryProgramFlags.value], GL_FALSE);
				}
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
				
				if (DRAWMODE != OGLPolyDrawMode_ZeroAlphaPass)
				{
					glStencilFunc(GL_NOTEQUAL, 0x40 | opaquePolyID, 0x7F);
					glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
					glDepthMask((enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
				}
			}
		}
		else // Draw the polygon as completely opaque.
		{
			glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_TRUE);
			
			if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported)
			{
				if (isPolyFrontFacing)
				{
					glDepthFunc(GL_EQUAL);
					glStencilFunc(GL_EQUAL, 0x40 | opaquePolyID, 0x40);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					
					glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
					glDepthMask(GL_FALSE);
					glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
					glStencilMask(0x40);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					
					glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
					glDepthMask(GL_TRUE);
					glDepthFunc(GL_LESS);
					glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
					glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
					glStencilMask(0xFF);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				}
				else
				{
					glStencilFunc(GL_ALWAYS, 0x40 | opaquePolyID, 0x40);
					glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
					
					glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
				}
			}
			else
			{
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			}
			
			glUniform1i(OGLRef.uniformTexDrawOpaque[this->_geometryProgramFlags.value], GL_FALSE);
		}
	}
	else
	{
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	}
	
	return OGLERROR_NOERR;
}

template <OGLPolyDrawMode DRAWMODE>
Render3DError OpenGLRenderer::DrawOtherPolygon(const GLenum polyPrimitive,
											   const GLsizei vertIndexCount,
											   const GLushort *indexBufferPtr,
											   const bool performDepthEqualTest,
											   const bool enableAlphaDepthWrite,
											   const u8 opaquePolyID,
											   const bool isPolyFrontFacing)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if ((DRAWMODE != OGLPolyDrawMode_ZeroAlphaPass) && performDepthEqualTest && this->_emulateNDSDepthCalculation && this->_feature.supportShaders)
	{
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDepthMask(GL_FALSE);
		
		// Use the stencil buffer to determine which fragments pass the lower-side tolerance.
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
		glDepthFunc(GL_LEQUAL);
		glStencilFunc(GL_ALWAYS, 0x80, 0x80);
		glStencilOp(GL_ZERO, GL_ZERO, GL_REPLACE);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		
		// Use the stencil buffer to determine which fragments pass the higher-side tolerance.
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)-DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
		glDepthFunc(GL_GEQUAL);
		glStencilFunc(GL_EQUAL, 0x80, 0x80);
		glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		
		// Set up the actual drawing of the polygon.
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], 0.0f);
		glDepthFunc(GL_ALWAYS);
		
		// If this is a transparent polygon, then we need to do the transparent polygon ID check.
		if (DRAWMODE == OGLPolyDrawMode_DrawTranslucentPolys)
		{
			glStencilFunc(GL_NOTEQUAL, 0x40 | opaquePolyID, 0x7F);
			glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
			glStencilMask(0x80);
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		}
		
		// Draw the polygon using the mask within the stencil buffer to determine which fragments should pass.
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(((DRAWMODE == OGLPolyDrawMode_DrawOpaquePolys) || enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
		
		glStencilFunc(GL_EQUAL, (DRAWMODE == OGLPolyDrawMode_DrawTranslucentPolys) ? 0xC0 | opaquePolyID : 0x80 | opaquePolyID, 0x80);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0x7F);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		
		// Clear bit 7 (0x80) now so that future polygons don't get confused.
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDepthMask(GL_FALSE);
		
		glStencilFunc(GL_ALWAYS, 0x80, 0x80);
		glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		
		// Finally, reset the rendering states.
		if (DRAWMODE == OGLPolyDrawMode_DrawTranslucentPolys)
		{
			glStencilFunc(GL_NOTEQUAL, 0x40 | opaquePolyID, 0x7F);
		}
		else
		{
			glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
		}
		
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glStencilMask(0xFF);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(((DRAWMODE == OGLPolyDrawMode_DrawOpaquePolys) || enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
	}
	else if (DRAWMODE == OGLPolyDrawMode_DrawOpaquePolys)
	{
		if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported)
		{
			if (isPolyFrontFacing)
			{
				glDepthFunc(GL_EQUAL);
				glStencilFunc(GL_EQUAL, 0x40 | opaquePolyID, 0x40);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthMask(GL_FALSE);
				glStencilOp(GL_KEEP, GL_KEEP, GL_ZERO);
				glStencilMask(0x40);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask(GL_TRUE);
				glDepthFunc(GL_LESS);
				glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
				glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
				glStencilMask(0xFF);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			}
			else
			{
				glStencilFunc(GL_ALWAYS, 0x40 | opaquePolyID, 0x40);
				glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
				
				glStencilFunc(GL_ALWAYS, opaquePolyID, 0x3F);
			}
		}
		else
		{
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		}
	}
	else
	{
		if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported && isPolyFrontFacing)
		{
			glDepthFunc(GL_EQUAL);
			glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[this->_geometryProgramFlags.value], GL_TRUE);
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			
			glDepthFunc(GL_LESS);
			glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[this->_geometryProgramFlags.value], GL_FALSE);
		}
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer::ApplyRenderingSettings(const GFX3D_State &renderState)
{
	Render3DError error = RENDER3DERROR_NOERR;
	
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	const bool didSelectedMultisampleSizeChange = (this->_selectedMultisampleSize != CommonSettings.GFX3D_Renderer_MultisampleSize);
	const bool didEmulateNDSDepthCalculationChange = (this->_emulateNDSDepthCalculation != CommonSettings.OpenGL_Emulation_NDSDepthCalculation);
	const bool didEnableTextureSmoothingChange = (this->_enableTextureSmoothing != CommonSettings.GFX3D_Renderer_TextureSmoothing);
	const bool didEmulateDepthLEqualPolygonFacingChange = (this->_emulateDepthLEqualPolygonFacing != (CommonSettings.OpenGL_Emulation_DepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported));
	
	this->_emulateShadowPolygon = CommonSettings.OpenGL_Emulation_ShadowPolygon;
	this->_emulateSpecialZeroAlphaBlending = CommonSettings.OpenGL_Emulation_SpecialZeroAlphaBlending;
	this->_emulateNDSDepthCalculation = CommonSettings.OpenGL_Emulation_NDSDepthCalculation;
	this->_emulateDepthLEqualPolygonFacing = CommonSettings.OpenGL_Emulation_DepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported;
	
	this->_selectedMultisampleSize = CommonSettings.GFX3D_Renderer_MultisampleSize;
	
	const bool oldMultisampleShadingFlag = this->_willUseMultisampleShaders;
	this->_enableMultisampledRendering = ((this->_selectedMultisampleSize >= 2) && this->_feature.supportMultisampledFBO);
	this->_willUseMultisampleShaders = this->_feature.supportSampleShading && this->_enableMultisampledRendering;
	if (this->_willUseMultisampleShaders != oldMultisampleShadingFlag)
	{
		// Fog program IDs don't have their own multisampled versions of the IDs, and so we
		// need to reset all of the existing IDs so that the fog programs can be regenerated
		this->DestroyFogPrograms();
	}
	
	error = Render3D::ApplyRenderingSettings(renderState);
	if (error != RENDER3DERROR_NOERR)
	{
		glFinish();
		ENDGL();
		return error;
	}
	
	if (didSelectedMultisampleSizeChange ||
		didEmulateNDSDepthCalculationChange ||
		didEnableTextureSmoothingChange ||
		didEmulateDepthLEqualPolygonFacingChange)
	{
		if (didSelectedMultisampleSizeChange)
		{
			GLsizei sampleSize = this->GetLimitedMultisampleSize();
			this->ResizeMultisampledFBOs(sampleSize);
		}
		
		if ( this->_feature.supportShaders &&
			(didEmulateNDSDepthCalculationChange ||
			 didEnableTextureSmoothingChange ||
			 didEmulateDepthLEqualPolygonFacingChange) )
		{
			glUseProgram(0);
			this->DestroyGeometryPrograms();
			
			error = this->CreateGeometryPrograms();
			if (error != OGLERROR_NOERR)
			{
				glUseProgram(0);
				this->DestroyGeometryPrograms();
				this->_feature.supportShaders = false;
				
				glFinish();
				ENDGL();
				return error;
			}
		}
	}
	
	ENDGL();
	return error;
}

OpenGLRenderer_1_2::OpenGLRenderer_1_2()
{
	_deviceInfo.renderID = RENDERID_OPENGL_LEGACY;
	_feature.variantID = OpenGLVariantID_Legacy_1_2;
	
	_geometryDrawBuffersEnum         = GeometryDrawBuffersEnumStandard;
	_geometryAttachmentWorkingBuffer = GeometryAttachmentWorkingBufferStandard;
	_geometryAttachmentPolyID        = GeometryAttachmentPolyIDStandard;
	_geometryAttachmentFogAttributes = GeometryAttachmentFogAttributesStandard;
	
#if defined(GL_VERSION_1_2)
	#if MSB_FIRST
	ref->textureSrcTypeCIColor   = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	ref->textureSrcTypeCIFog     = GL_UNSIGNED_INT_8_8_8_8_REV;
	ref->textureSrcTypeEdgeColor = GL_UNSIGNED_INT_8_8_8_8;
	ref->textureSrcTypeToonTable = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	#else
	ref->textureSrcTypeCIColor   = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	ref->textureSrcTypeCIFog     = GL_UNSIGNED_INT_8_8_8_8_REV;
	ref->textureSrcTypeEdgeColor = GL_UNSIGNED_INT_8_8_8_8_REV;
	ref->textureSrcTypeToonTable = GL_UNSIGNED_SHORT_1_5_5_5_REV;
	#endif
#endif
}

OpenGLRenderer_1_2::~OpenGLRenderer_1_2()
{
	glFinish();
	
	free_aligned(ref->position4fBuffer);
	ref->position4fBuffer = NULL;
	
	free_aligned(ref->texCoord2fBuffer);
	ref->texCoord2fBuffer = NULL;
	
	free_aligned(ref->color4fBuffer);
	ref->color4fBuffer = NULL;
	
	if (this->_feature.supportShaders)
	{
		glUseProgram(0);
		
		this->DestroyGeometryPrograms();
		this->DestroyGeometryZeroDstAlphaProgram();
		this->DestroyEdgeMarkProgram();
		this->DestroyFogPrograms();
		
		this->_feature.supportShaders = false;
	}
	
	DestroyVAOs();
	DestroyVBOs();
	DestroyFBOs();
	DestroyMultisampledFBO();
	
	// Kill the texture cache now before all of our texture IDs disappear.
	texCache.Reset();
	
	glDeleteTextures(1, &ref->texFinalColorID);
	ref->texFinalColorID = 0;
	
	glFinish();
}

Render3DError OpenGLRenderer_1_2::InitExtensions()
{
	OGLRenderRef &OGLRef = *this->ref;
	Render3DError error = OGLERROR_NOERR;
	
	// Get OpenGL extensions
	std::set<std::string> oglExtensionSet;
	this->GetExtensionSet(&oglExtensionSet);
	
#if defined(GL_VERSION_1_2)
	if (!this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_multitexture"))
	{
		return OGLERROR_DRIVER_VERSION_TOO_OLD;
	}
	else
	{
		GLint maxFixedFunctionTexUnitsOGL = 0;
		glGetIntegerv(GL_MAX_TEXTURE_UNITS, &maxFixedFunctionTexUnitsOGL);
		
		if (maxFixedFunctionTexUnitsOGL < 4)
		{
			return OGLERROR_DRIVER_VERSION_TOO_OLD;
		}
	}
#endif
	
	// Apple-specific extensions
	this->_feature.supportTextureRange_APPLE  = this->IsExtensionPresent(&oglExtensionSet, "GL_APPLE_texture_range");
	this->_feature.supportClientStorage_APPLE = this->IsExtensionPresent(&oglExtensionSet, "GL_APPLE_client_storage");
	
	// Mirrored Repeat Mode Support
	this->_feature.supportTextureMirroredRepeat = this->IsVersionSupported(1, 4, 0) || this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_texture_mirrored_repeat");
	this->_feature.stateTexMirroredRepeat       = (this->_feature.supportTextureMirroredRepeat) ? GL_MIRRORED_REPEAT : GL_REPEAT;
	
	// Blending Support
	this->_feature.supportBlendFuncSeparate     = this->IsVersionSupported(1, 4, 0) || this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_blend_func_separate");
	this->_feature.supportBlendEquationSeparate = this->IsVersionSupported(2, 0, 0) || this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_blend_equation_separate");
	
	// Don't use ARB versions since we're using the EXT versions for backwards compatibility.
	this->_feature.supportFBO = this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_object") &&
	                            this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_packed_depth_stencil");
	
	// The internal format of FBOs is GL_RGBA, so we will match that format for glReadPixels.
	// But the traditional format before FBOs was GL_BGRA, which is also the fastest format
	// for glReadPixels when using the legacy default framebuffer.
	this->_feature.readPixelsBestFormat = (this->_feature.supportFBO) ? GL_RGBA : GL_BGRA;
	this->_feature.readPixelsBestDataType = GL_UNSIGNED_BYTE;
	
	// Need to generate this texture first because FBO creation needs it.
	// This texture is only required by shaders, and so if shader creation
	// fails, then we can immediately delete this texture if an error occurs.
	glGenTextures(1, &OGLRef.texFinalColorID);
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FinalColor);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texFinalColorID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
	glActiveTexture(GL_TEXTURE0);
	
	this->_feature.supportVBO = this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_buffer_object");
	if (this->_feature.supportVBO)
	{
		this->CreateVBOs();
	}
	else if (this->IsVersionSupported(1, 5, 0))
	{
		error = OGLERROR_VBO_UNSUPPORTED;
		return error;
	}
	this->_feature.supportPBO = false;
	
	this->_feature.supportPBO = this->_feature.supportVBO &&
	                           (this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_pixel_buffer_object") ||
	                            this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_pixel_buffer_object"));
	if (!this->_feature.supportPBO && this->IsVersionSupported(2, 1, 0))
	{
		error = OGLERROR_PBO_UNSUPPORTED;
		return error;
	}
	
	if (this->_feature.supportFBO)
	{
		GLint maxColorAttachments = 0;
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &maxColorAttachments);
		
		if (maxColorAttachments >= 4)
		{
			error = this->CreateFBOs();
			if (error != OGLERROR_NOERR)
			{
				this->_feature.supportFBO = false;
			}
		}
		else
		{
			INFO("OpenGL: Driver does not support at least 4 FBO color attachments.\n");
			this->_feature.supportFBO = false;
		}
	}
	
	if (!this->_feature.supportFBO)
	{
		INFO("OpenGL: FBOs are unsupported. Some emulation features will be disabled.\n");
	}
	
	this->_feature.supportFBOBlit = this->_feature.supportFBO &&
	                                this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_blit");
	if (!this->_feature.supportFBOBlit)
	{
		INFO("OpenGL: FBO blitting is unsupported. Some emulation features will be disabled.\n");
	}
	
	this->_selectedMultisampleSize = CommonSettings.GFX3D_Renderer_MultisampleSize;
	
	// Don't use ARB versions since we're using the EXT versions for backwards compatibility.
	this->_feature.supportMultisampledFBO = this->_feature.supportFBOBlit &&
	                                        this->IsExtensionPresent(&oglExtensionSet, "GL_EXT_framebuffer_multisample");
	if (this->_feature.supportMultisampledFBO)
	{
		GLint maxSamplesOGL = 0;
		glGetIntegerv(GL_MAX_SAMPLES_EXT, &maxSamplesOGL);
		this->_deviceInfo.maxSamples = (u8)maxSamplesOGL;
		
		if (this->_deviceInfo.maxSamples >= 2)
		{
			// Try and initialize the multisampled FBOs with the GFX3D_Renderer_MultisampleSize.
			// However, if the client has this set to 0, then set sampleSize to 2 in order to
			// force the generation and the attachments of the buffers at a meaningful sample
			// size. If GFX3D_Renderer_MultisampleSize is 0, then we can deallocate the buffer
			// memory afterwards.
			GLsizei sampleSize = this->GetLimitedMultisampleSize();
			if (sampleSize == 0)
			{
				sampleSize = 2;
			}
			
			error = this->CreateMultisampledFBO(sampleSize);
			if (error != OGLERROR_NOERR)
			{
				this->_feature.supportMultisampledFBO = false;
			}
			
			// If GFX3D_Renderer_MultisampleSize is 0, then we can deallocate the buffers now
			// in order to save some memory.
			if (this->_selectedMultisampleSize == 0)
			{
				this->ResizeMultisampledFBOs(0);
			}
		}
		else
		{
			this->_feature.supportMultisampledFBO = false;
			INFO("OpenGL: Driver does not support at least 2x multisampled FBOs.\n");
		}
	}
	
	if (!this->_feature.supportMultisampledFBO)
	{
		INFO("OpenGL: Multisampled FBOs are unsupported. Multisample antialiasing will be disabled.\n");
	}
	
	this->_feature.supportShaders = this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_shader_objects") &&
	                                this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_shader") &&
	                                this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_fragment_shader") &&
	                                this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_program");
	if (this->_feature.supportShaders)
	{
		GLint maxColorAttachmentsOGL = 0;
		GLint maxDrawBuffersOGL = 0;
		GLint maxShaderTexUnitsOGL = 0;
		glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS_EXT, &maxColorAttachmentsOGL);
		glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffersOGL);
		glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &maxShaderTexUnitsOGL);
		
		if ( (maxColorAttachmentsOGL >= 4) && (maxDrawBuffersOGL >= 4) && (maxShaderTexUnitsOGL >= 8) )
		{
			this->_enableTextureSmoothing = CommonSettings.GFX3D_Renderer_TextureSmoothing;
			this->_emulateShadowPolygon = CommonSettings.OpenGL_Emulation_ShadowPolygon;
			this->_emulateSpecialZeroAlphaBlending = CommonSettings.OpenGL_Emulation_SpecialZeroAlphaBlending;
			this->_emulateNDSDepthCalculation = CommonSettings.OpenGL_Emulation_NDSDepthCalculation;
			this->_emulateDepthLEqualPolygonFacing = CommonSettings.OpenGL_Emulation_DepthLEqualPolygonFacing;
			
			error = this->CreateGeometryPrograms();
			if (error == OGLERROR_NOERR)
			{
				error = this->CreateGeometryZeroDstAlphaProgram(GeometryZeroDstAlphaPixelMaskVtxShader_100, GeometryZeroDstAlphaPixelMaskFragShader_100);
				if (error == OGLERROR_NOERR)
				{
					INFO("OpenGL: Successfully created geometry shaders.\n");
					error = this->InitPostprocessingPrograms(EdgeMarkVtxShader_100, EdgeMarkFragShader_100);
				}
			}
			
			if (error != OGLERROR_NOERR)
			{
				glUseProgram(0);
				this->DestroyGeometryPrograms();
				this->DestroyGeometryZeroDstAlphaProgram();
				this->_feature.supportShaders = false;
			}
		}
		else
		{
			INFO("OpenGL: Driver does not support at least 4 color attachments, 4 draw buffers, and 8 texture image units.\n");
			this->_feature.supportShaders = false;
		}
	}
	
	if (!this->_feature.supportShaders)
	{
		INFO("OpenGL: Shaders are unsupported. Disabling shaders and using fixed-function pipeline. Some emulation features will be disabled.\n");
		
		glDeleteTextures(1, &OGLRef.texFinalColorID);
		OGLRef.texFinalColorID = 0;
		
		if (this->IsVersionSupported(2, 0, 0))
		{
			return error;
		}
		
		// If shaders aren't available, we need to take care of the vertex data format
		// conversion on the CPU instead of on the GPU using these client-side buffers.
		OGLRef.position4fBuffer = (GLfloat *)malloc_alignedPage(VERTLIST_SIZE * sizeof(GLfloat) * 4);
		OGLRef.texCoord2fBuffer = (GLfloat *)malloc_alignedPage(VERTLIST_SIZE * sizeof(GLfloat) * 2);
		OGLRef.color4fBuffer    = (GLfloat *)malloc_alignedPage(VERTLIST_SIZE * sizeof(GLfloat) * 4);
	}
	
	this->_feature.supportVAO = this->_feature.supportVBO &&
	                            this->IsExtensionPresent(&oglExtensionSet, "GL_ARB_vertex_array_object");
	this->_feature.supportVAO_APPLE = this->_feature.supportVBO &&
	                                  this->IsExtensionPresent(&oglExtensionSet, "GL_APPLE_vertex_array_object");
	if (this->_feature.supportVAO || this->_feature.supportVAO_APPLE)
	{
		this->CreateVAOs();
	}
	
	// Get host GPU device properties
	GLfloat maxAnisotropyOGL = 1.0f;
	glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropyOGL);
	this->_deviceInfo.maxAnisotropy = maxAnisotropyOGL;
	
	// Set rendering support flags based on driver features.
	this->_deviceInfo.isEdgeMarkSupported         = this->_feature.supportShaders && this->_feature.supportVBO && this->_feature.supportFBO;
	this->_deviceInfo.isFogSupported              = this->_feature.supportShaders && this->_feature.supportVBO && this->_feature.supportFBO;
	this->_deviceInfo.isTextureSmoothingSupported = this->_feature.supportShaders;
	this->_isDepthLEqualPolygonFacingSupported    = this->_feature.supportShaders && this->_feature.supportVBO && this->_feature.supportFBO;
	
	this->_enableMultisampledRendering = ((this->_selectedMultisampleSize >= 2) && this->_feature.supportMultisampledFBO);
	
	this->_colorOut = new OpenGLRenderColorOut(this->_feature, this->_framebufferWidth, this->_framebufferHeight);
	this->_colorOut->SetRenderer(this);
	((OpenGLRenderColorOut *)this->_colorOut)->SetFBORenderID(OGLRef.fboRenderID);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::CreateVBOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenBuffers(1, &OGLRef.vboGeometryVtxID);
	glGenBuffers(1, &OGLRef.iboGeometryIndexID);
	glGenBuffers(1, &OGLRef.vboPostprocessVtxID);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
	glBufferData(GL_ARRAY_BUFFER, VERTLIST_SIZE * sizeof(NDSVertex), NULL, GL_STREAM_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(OGLRef.vertIndexBuffer), NULL, GL_STREAM_DRAW);
	
	glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
	glBufferData(GL_ARRAY_BUFFER, sizeof(PostprocessVtxBuffer), PostprocessVtxBuffer, GL_STATIC_DRAW);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyVBOs()
{
	if (!this->_feature.supportVBO)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	
	glDeleteBuffers(1, &OGLRef.vboGeometryVtxID);
	glDeleteBuffers(1, &OGLRef.iboGeometryIndexID);
	glDeleteBuffers(1, &OGLRef.vboPostprocessVtxID);
	
	this->_feature.supportVBO = false;
}

Render3DError OpenGLRenderer_1_2::CreateVAOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenVertexArrays(1, &OGLRef.vaoGeometryStatesID);
	glGenVertexArrays(1, &OGLRef.vaoPostprocessStatesID);
	
	glBindVertexArray(OGLRef.vaoGeometryStatesID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
	
	if (this->_feature.supportShaders)
	{
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
		
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glEnableVertexAttribArray(OGLVertexAttributeID_Color);
		glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_INT, GL_FALSE, sizeof(NDSVertex), (const GLvoid *)offsetof(NDSVertex, position));
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_INT, GL_FALSE, sizeof(NDSVertex), (const GLvoid *)offsetof(NDSVertex, texCoord));
		glVertexAttribPointer(OGLVertexAttributeID_Color, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(NDSVertex), (const GLvoid *)offsetof(NDSVertex, color));
	}
#if defined(GL_VERSION_1_2)
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glVertexPointer(4, GL_FLOAT, 0, OGLRef.position4fBuffer);
		glTexCoordPointer(2, GL_FLOAT, 0, OGLRef.texCoord2fBuffer);
		glColorPointer(4, GL_FLOAT, 0, OGLRef.color4fBuffer);
	}
#endif
	
	glBindVertexArray(0);
	
	glBindVertexArray(OGLRef.vaoPostprocessStatesID);
	
	if (this->_feature.supportShaders)
	{
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboPostprocessVtxID);
		
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glVertexAttribPointer(OGLVertexAttributeID_Position, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_FLOAT, GL_FALSE, 0, (const GLvoid *)(sizeof(GLfloat) * 8));
	}
	else
	{
		// Do nothing. Framebuffer post-processing requires shaders.
	}
	
	glBindVertexArray(0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyVAOs()
{
	if (!this->_feature.supportVAO && !this->_feature.supportVAO_APPLE)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindVertexArray(0);
	glDeleteVertexArrays(1, &OGLRef.vaoGeometryStatesID);
	glDeleteVertexArrays(1, &OGLRef.vaoPostprocessStatesID);
	
	this->_feature.supportVAO = false;
	this->_feature.supportVAO_APPLE = false;
}

Render3DError OpenGLRenderer_1_2::CreateFBOs()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	// Set up FBO render targets
	glGenTextures(1, &OGLRef.texCIColorID);
	glGenTextures(1, &OGLRef.texCIFogAttrID);
	glGenTextures(1, &OGLRef.texCIDepthStencilID);
	
	glGenTextures(1, &OGLRef.texGColorID);
	glGenTextures(1, &OGLRef.texGFogAttrID);
	glGenTextures(1, &OGLRef.texGPolyID);
	glGenTextures(1, &OGLRef.texGDepthStencilID);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_DepthStencil);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGDepthStencilID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, NULL);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GColor);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGColorID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GPolyID);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGPolyID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FogAttr);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texGFogAttrID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
	
	GLint *tempClearImageBuffer = (GLint *)calloc(GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT, sizeof(GLint));
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_CIColor);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIColorID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_RGBA, OGLRef.textureSrcTypeCIColor, tempClearImageBuffer);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_CIDepth);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthStencilID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, tempClearImageBuffer);
	
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_CIFogAttr);
	glBindTexture(GL_TEXTURE_2D, OGLRef.texCIFogAttrID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, GL_RGBA, OGLRef.textureSrcTypeCIFog, tempClearImageBuffer);
	
	glBindTexture(GL_TEXTURE_2D, 0);
	free(tempClearImageBuffer);
	tempClearImageBuffer = NULL;
	
	// Set up FBOs
	glGenFramebuffersEXT(1, &OGLRef.fboClearImageID);
	glGenFramebuffersEXT(1, &OGLRef.fboRenderID);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboClearImageID);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, OGL_CI_COLOROUT_ATTACHMENT_ID, GL_TEXTURE_2D, OGLRef.texCIColorID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, OGL_CI_FOGATTRIBUTES_ATTACHMENT_ID, GL_TEXTURE_2D, OGLRef.texCIFogAttrID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texCIDepthStencilID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texCIDepthStencilID, 0);
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to create FBOs!\n");
		this->DestroyFBOs();
		
		return OGLERROR_FBO_CREATE_ERROR;
	}
	// Assign the default read/draw buffers.
	glReadBuffer(OGL_CI_COLOROUT_ATTACHMENT_ID);
	glDrawBuffer(GL_NONE);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, OGL_COLOROUT_ATTACHMENT_ID, GL_TEXTURE_2D, OGLRef.texGColorID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, OGL_POLYID_ATTACHMENT_ID, GL_TEXTURE_2D, OGLRef.texGPolyID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, OGL_FOGATTRIBUTES_ATTACHMENT_ID, GL_TEXTURE_2D, OGLRef.texGFogAttrID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, OGL_WORKING_ATTACHMENT_ID, GL_TEXTURE_2D, OGLRef.texFinalColorID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texGDepthStencilID, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_TEXTURE_2D, OGLRef.texGDepthStencilID, 0);
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to create FBOs!\n");
		this->DestroyFBOs();
		
		return OGLERROR_FBO_CREATE_ERROR;
	}
	// Assign the default read/draw buffers.
	glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	
	OGLRef.selectedRenderingFBO = OGLRef.fboRenderID;
	INFO("OpenGL: Successfully created FBOs.\n");
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyFBOs()
{
	if (!this->_feature.supportFBO)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDeleteFramebuffersEXT(1, &OGLRef.fboClearImageID);
	glDeleteFramebuffersEXT(1, &OGLRef.fboRenderID);
	glDeleteTextures(1, &OGLRef.texCIColorID);
	glDeleteTextures(1, &OGLRef.texCIFogAttrID);
	glDeleteTextures(1, &OGLRef.texCIDepthStencilID);
	glDeleteTextures(1, &OGLRef.texGColorID);
	glDeleteTextures(1, &OGLRef.texGPolyID);
	glDeleteTextures(1, &OGLRef.texGFogAttrID);
	glDeleteTextures(1, &OGLRef.texGDepthStencilID);
	
	OGLRef.fboClearImageID = 0;
	OGLRef.fboRenderID = 0;
	OGLRef.texCIColorID = 0;
	OGLRef.texCIFogAttrID = 0;
	OGLRef.texCIDepthStencilID = 0;
	OGLRef.texGColorID = 0;
	OGLRef.texGPolyID = 0;
	OGLRef.texGFogAttrID = 0;
	OGLRef.texGDepthStencilID = 0;
	
	this->_feature.supportFBO = false;
}

Render3DError OpenGLRenderer_1_2::CreateMultisampledFBO(GLsizei numSamples)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	// Set up FBO render targets
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGColorID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGWorkingID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGPolyID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGFogAttrID);
	glGenRenderbuffersEXT(1, &OGLRef.rboMSGDepthStencilID);
	
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGWorkingID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_DEPTH24_STENCIL8_EXT, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	
	// Set up multisampled rendering FBO
	glGenFramebuffersEXT(1, &OGLRef.fboMSIntermediateRenderID);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, OGL_COLOROUT_ATTACHMENT_ID, GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, OGL_POLYID_ATTACHMENT_ID, GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, OGL_FOGATTRIBUTES_ATTACHMENT_ID, GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, OGL_WORKING_ATTACHMENT_ID, GL_RENDERBUFFER_EXT, OGLRef.rboMSGWorkingID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	
	if (glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT) != GL_FRAMEBUFFER_COMPLETE_EXT)
	{
		INFO("OpenGL: Failed to create multisampled FBO!\n");
		this->DestroyMultisampledFBO();
		
		return OGLERROR_FBO_CREATE_ERROR;
	}
	
	glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	INFO("OpenGL: Successfully created multisampled FBO.\n");
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyMultisampledFBO()
{
	if (!this->_feature.supportMultisampledFBO)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
	glDeleteFramebuffersEXT(1, &OGLRef.fboMSIntermediateRenderID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGColorID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGWorkingID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGPolyID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGFogAttrID);
	glDeleteRenderbuffersEXT(1, &OGLRef.rboMSGDepthStencilID);
	
	OGLRef.fboMSIntermediateRenderID = 0;
	OGLRef.rboMSGColorID = 0;
	OGLRef.rboMSGWorkingID = 0;
	OGLRef.rboMSGPolyID = 0;
	OGLRef.rboMSGFogAttrID = 0;
	OGLRef.rboMSGDepthStencilID = 0;
	
	this->_feature.supportMultisampledFBO = false;
}

void OpenGLRenderer_1_2::ResizeMultisampledFBOs(GLsizei numSamples)
{
	OGLRenderRef &OGLRef = *this->ref;
	GLsizei w = (GLsizei)this->_framebufferWidth;
	GLsizei h = (GLsizei)this->_framebufferHeight;
	
	if ( !this->_feature.supportMultisampledFBO ||
		 (numSamples == 1) ||
		 (w < GPU_FRAMEBUFFER_NATIVE_WIDTH) || (h < GPU_FRAMEBUFFER_NATIVE_HEIGHT) )
	{
		return;
	}
	
	if (numSamples == 0)
	{
		w = 0;
		h = 0;
		numSamples = 2;
	}
	
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGColorID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, w, h);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGWorkingID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, w, h);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGPolyID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, w, h);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGFogAttrID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_RGBA8, w, h);
	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, OGLRef.rboMSGDepthStencilID);
	glRenderbufferStorageMultisampleEXT(GL_RENDERBUFFER_EXT, numSamples, GL_DEPTH24_STENCIL8_EXT, w, h);
}

Render3DError OpenGLRenderer_1_2::CreateGeometryPrograms()
{
	Render3DError error = OGLERROR_NOERR;
	OGLRenderRef &OGLRef = *this->ref;
	
	glGenTextures(1, &OGLRef.texToonTableID);
	glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
	glBindTexture(GL_TEXTURE_1D, OGLRef.texToonTableID);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 32, 0, GL_RGBA, OGLRef.textureSrcTypeToonTable, NULL);
	
	glGenTextures(1, &OGLRef.texEdgeColorTableID);
	glBindTexture(GL_TEXTURE_1D, OGLRef.texEdgeColorTableID);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 8, 0, GL_RGBA, OGLRef.textureSrcTypeEdgeColor, NULL);
	
	glGenTextures(1, &OGLRef.texFogDensityTableID);
	glBindTexture(GL_TEXTURE_1D, OGLRef.texFogDensityTableID);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_LUMINANCE, 32, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glActiveTexture(GL_TEXTURE0);
	
	OGLGeometryFlags programFlags;
	programFlags.value = 0;
	
	std::stringstream vtxShaderHeader;
	vtxShaderHeader << "#define DEPTH_EQUALS_TEST_TOLERANCE " << DEPTH_EQUALS_TEST_TOLERANCE << ".0\n";
	vtxShaderHeader << "\n";
	
	std::string vtxShaderCode  = vtxShaderHeader.str() + std::string(GeometryVtxShader_100);
	
	std::stringstream fragShaderHeader;
	fragShaderHeader << "#define FRAMEBUFFER_SIZE_X " << this->_framebufferWidth  << ".0 \n";
	fragShaderHeader << "#define FRAMEBUFFER_SIZE_Y " << this->_framebufferHeight << ".0 \n";
	fragShaderHeader << "\n";
	fragShaderHeader << "#define OUTFRAGCOLOR " << ((this->_feature.supportFBO) ? "gl_FragData[0]" : "gl_FragColor") << "\n";
	fragShaderHeader << "\n";
	
	for (size_t flagsValue = 0; flagsValue < 128; flagsValue++, programFlags.value++)
	{
		std::stringstream shaderFlags;
		shaderFlags << "#define USE_TEXTURE_SMOOTHING " << ((this->_enableTextureSmoothing) ? 1 : 0) << "\n";
		shaderFlags << "#define USE_NDS_DEPTH_CALCULATION " << ((this->_emulateNDSDepthCalculation) ? 1 : 0) << "\n";
		shaderFlags << "#define USE_DEPTH_LEQUAL_POLYGON_FACING " << ((this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported) ? 1 : 0) << "\n";
		shaderFlags << "\n";
		shaderFlags << "#define ENABLE_W_DEPTH " << ((programFlags.EnableWDepth) ? 1 : 0) << "\n";
		shaderFlags << "#define ENABLE_ALPHA_TEST " << ((programFlags.EnableAlphaTest) ? "true\n" : "false\n");
		shaderFlags << "#define ENABLE_TEXTURE_SAMPLING " << ((programFlags.EnableTextureSampling) ? "true\n" : "false\n");
		shaderFlags << "#define TOON_SHADING_MODE " << ((programFlags.ToonShadingMode) ? 1 : 0) << "\n";
		shaderFlags << "#define ENABLE_FOG " << ((programFlags.EnableFog && this->_feature.supportVBO && this->_feature.supportFBO) ? 1 : 0) << "\n"; // Do not rely on this->_deviceInfo.isFogSupported because it hasn't been set yet.
		shaderFlags << "#define ENABLE_EDGE_MARK " << ((programFlags.EnableEdgeMark && this->_feature.supportVBO && this->_feature.supportFBO) ? 1 : 0) << "\n"; // Do not rely on this->_deviceInfo.isEdgeMarkSupported because it hasn't been set yet.
		shaderFlags << "#define DRAW_MODE_OPAQUE " << ((programFlags.OpaqueDrawMode && this->_feature.supportVBO && this->_feature.supportFBO) ? 1 : 0) << "\n";
		shaderFlags << "\n";
		shaderFlags << "#define ATTACHMENT_WORKING_BUFFER " << this->_geometryAttachmentWorkingBuffer[programFlags.DrawBuffersMode] << "\n";
		shaderFlags << "#define ATTACHMENT_POLY_ID "        << this->_geometryAttachmentPolyID[programFlags.DrawBuffersMode]        << "\n";
		shaderFlags << "#define ATTACHMENT_FOG_ATTRIBUTES " << this->_geometryAttachmentFogAttributes[programFlags.DrawBuffersMode] << "\n";
		shaderFlags << "\n";
		
		std::string fragShaderCode = fragShaderHeader.str() + shaderFlags.str() + std::string(GeometryFragShader_100);
		
		error = ShaderProgramCreateOGL(OGLRef.vertexGeometryShaderID,
		                               OGLRef.fragmentGeometryShaderID[flagsValue],
		                               OGLRef.programGeometryID[flagsValue],
		                               vtxShaderCode.c_str(),
		                               fragShaderCode.c_str());
		if (error != OGLERROR_NOERR)
		{
			INFO("OpenGL: Failed to create the GEOMETRY shader program.\n");
			glUseProgram(0);
			this->DestroyGeometryPrograms();
			return error;
		}
		
		glBindAttribLocation(OGLRef.programGeometryID[flagsValue], OGLVertexAttributeID_Position, "inPosition");
		glBindAttribLocation(OGLRef.programGeometryID[flagsValue], OGLVertexAttributeID_TexCoord0, "inTexCoord0");
		glBindAttribLocation(OGLRef.programGeometryID[flagsValue], OGLVertexAttributeID_Color, "inColor");
		
		glLinkProgram(OGLRef.programGeometryID[flagsValue]);
		if (!ValidateShaderProgramLinkOGL(OGLRef.programGeometryID[flagsValue]))
		{
			INFO("OpenGL: Failed to link the GEOMETRY shader program.\n");
			glUseProgram(0);
			this->DestroyGeometryPrograms();
			return OGLERROR_SHADER_CREATE_ERROR;
		}
		
		glValidateProgram(OGLRef.programGeometryID[flagsValue]);
		glUseProgram(OGLRef.programGeometryID[flagsValue]);
		
		const GLint uniformTexRenderObject						= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "texRenderObject");
		glUniform1i(uniformTexRenderObject, 0);
		
		const GLint uniformTexToonTable							= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "texToonTable");
		glUniform1i(uniformTexToonTable, OGLTextureUnitID_LookupTable);
		
		if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported && !programFlags.OpaqueDrawMode)
		{
			const GLint uniformTexBackfacing					= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "inDstBackFacing");
			glUniform1i(uniformTexBackfacing, OGLTextureUnitID_FinalColor);
		}
		
		OGLRef.uniformStateAlphaTestRef[flagsValue]				= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "stateAlphaTestRef");
		
		OGLRef.uniformPolyTexScale[flagsValue]					= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyTexScale");
		OGLRef.uniformPolyMode[flagsValue]						= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyMode");
		OGLRef.uniformPolyIsWireframe[flagsValue]				= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyIsWireframe");
		OGLRef.uniformPolySetNewDepthForTranslucent[flagsValue]	= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polySetNewDepthForTranslucent");
		OGLRef.uniformPolyAlpha[flagsValue]						= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyAlpha");
		OGLRef.uniformPolyID[flagsValue]						= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyID");
		
		OGLRef.uniformPolyEnableTexture[flagsValue]				= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyEnableTexture");
		OGLRef.uniformPolyEnableFog[flagsValue]					= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyEnableFog");
		OGLRef.uniformTexSingleBitAlpha[flagsValue]				= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "texSingleBitAlpha");
		
		OGLRef.uniformTexDrawOpaque[flagsValue]					= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "texDrawOpaque");
		OGLRef.uniformDrawModeDepthEqualsTest[flagsValue]		= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "drawModeDepthEqualsTest");
		OGLRef.uniformPolyIsBackFacing[flagsValue]              = glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyIsBackFacing");
		OGLRef.uniformPolyDrawShadow[flagsValue]				= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyDrawShadow");
		OGLRef.uniformPolyDepthOffset[flagsValue]				= glGetUniformLocation(OGLRef.programGeometryID[flagsValue], "polyDepthOffset");
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyGeometryPrograms()
{
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	for (size_t flagsValue = 0; flagsValue < 128; flagsValue++)
	{
		if (OGLRef.programGeometryID[flagsValue] == 0)
		{
			continue;
		}
		
		glDetachShader(OGLRef.programGeometryID[flagsValue], OGLRef.vertexGeometryShaderID);
		glDetachShader(OGLRef.programGeometryID[flagsValue], OGLRef.fragmentGeometryShaderID[flagsValue]);
		glDeleteProgram(OGLRef.programGeometryID[flagsValue]);
		glDeleteShader(OGLRef.fragmentGeometryShaderID[flagsValue]);
		
		OGLRef.programGeometryID[flagsValue] = 0;
		OGLRef.fragmentGeometryShaderID[flagsValue] = 0;
	}
	
	glDeleteShader(OGLRef.vertexGeometryShaderID);
	OGLRef.vertexGeometryShaderID = 0;
	
	glDeleteTextures(1, &ref->texToonTableID);
	OGLRef.texToonTableID = 0;
	
	glDeleteTextures(1, &ref->texEdgeColorTableID);
	OGLRef.texEdgeColorTableID = 0;
	
	glDeleteTextures(1, &ref->texFogDensityTableID);
	OGLRef.texFogDensityTableID = 0;
}

Render3DError OpenGLRenderer_1_2::CreateGeometryZeroDstAlphaProgram(const char *vtxShaderCString, const char *fragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	OGLRenderRef &OGLRef = *this->ref;
	
	if ( (vtxShaderCString == NULL) || (fragShaderCString == NULL) )
	{
		return error;
	}
	
	error = ShaderProgramCreateOGL(OGLRef.vtxShaderGeometryZeroDstAlphaID,
	                               OGLRef.fragShaderGeometryZeroDstAlphaID,
	                               OGLRef.programGeometryZeroDstAlphaID,
	                               vtxShaderCString,
	                               fragShaderCString);
	if (error != OGLERROR_NOERR)
	{
		INFO("OpenGL: Failed to create the GEOMETRY ZERO DST ALPHA shader program.\n");
		glUseProgram(0);
		this->DestroyGeometryZeroDstAlphaProgram();
		return error;
	}
	
	glBindAttribLocation(OGLRef.programGeometryZeroDstAlphaID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programGeometryZeroDstAlphaID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	glLinkProgram(OGLRef.programGeometryZeroDstAlphaID);
	if (!ValidateShaderProgramLinkOGL(OGLRef.programGeometryZeroDstAlphaID))
	{
		INFO("OpenGL: Failed to link the GEOMETRY ZERO DST ALPHA shader program.\n");
		glUseProgram(0);
		this->DestroyGeometryZeroDstAlphaProgram();
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(OGLRef.programGeometryZeroDstAlphaID);
	glUseProgram(OGLRef.programGeometryZeroDstAlphaID);
	
	const GLint uniformTexGColor = glGetUniformLocation(OGLRef.programGeometryZeroDstAlphaID, "texInFragColor");
	glUniform1i(uniformTexGColor, OGLTextureUnitID_GColor);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyGeometryZeroDstAlphaProgram()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportShaders || (OGLRef.programGeometryZeroDstAlphaID == 0))
	{
		return;
	}
	
	glDetachShader(OGLRef.programGeometryZeroDstAlphaID, OGLRef.vtxShaderGeometryZeroDstAlphaID);
	glDetachShader(OGLRef.programGeometryZeroDstAlphaID, OGLRef.fragShaderGeometryZeroDstAlphaID);
	glDeleteProgram(OGLRef.programGeometryZeroDstAlphaID);
	glDeleteShader(OGLRef.vtxShaderGeometryZeroDstAlphaID);
	glDeleteShader(OGLRef.fragShaderGeometryZeroDstAlphaID);
	
	OGLRef.programGeometryZeroDstAlphaID = 0;
	OGLRef.vtxShaderGeometryZeroDstAlphaID = 0;
	OGLRef.fragShaderGeometryZeroDstAlphaID = 0;
}

Render3DError OpenGLRenderer_1_2::CreateClearImageProgram(const char *vsCString, const char *fsCString)
{
	Render3DError error = OGLERROR_NOERR;
	// TODO: Add support for ancient GPUs that support shaders but not GL_EXT_framebuffer_blit.
	return error;
}

void OpenGLRenderer_1_2::DestroyClearImageProgram()
{
	// Do nothing for now.
}

Render3DError OpenGLRenderer_1_2::CreateEdgeMarkProgram(const bool isMultisample, const char *vtxShaderCString, const char *fragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	OGLRenderRef &OGLRef = *this->ref;
	
	if ( (vtxShaderCString == NULL) || (fragShaderCString == NULL) )
	{
		return error;
	}
	
	std::stringstream shaderHeader;
	shaderHeader << "#define FRAMEBUFFER_SIZE_X " << this->_framebufferWidth  << ".0 \n";
	shaderHeader << "#define FRAMEBUFFER_SIZE_Y " << this->_framebufferHeight << ".0 \n";
	shaderHeader << "\n";
	
	std::string vtxShaderCode  = shaderHeader.str() + std::string(vtxShaderCString);
	std::string fragShaderCode = shaderHeader.str() + std::string(fragShaderCString);
	
	error = ShaderProgramCreateOGL(OGLRef.vertexEdgeMarkShaderID,
	                               OGLRef.fragmentEdgeMarkShaderID,
	                               OGLRef.programEdgeMarkID,
	                               vtxShaderCode.c_str(),
	                               fragShaderCode.c_str());
	if (error != OGLERROR_NOERR)
	{
		INFO("OpenGL: Failed to create the EDGE MARK shader program.\n");
		glUseProgram(0);
		this->DestroyEdgeMarkProgram();
		return error;
	}
	
	glBindAttribLocation(OGLRef.programEdgeMarkID, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(OGLRef.programEdgeMarkID, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	glLinkProgram(OGLRef.programEdgeMarkID);
	if (!ValidateShaderProgramLinkOGL(OGLRef.programEdgeMarkID))
	{
		INFO("OpenGL: Failed to link the EDGE MARK shader program.\n");
		glUseProgram(0);
		this->DestroyEdgeMarkProgram();
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(OGLRef.programEdgeMarkID);
	glUseProgram(OGLRef.programEdgeMarkID);
	
	const GLint uniformTexGDepth         = glGetUniformLocation(OGLRef.programEdgeMarkID, "texInFragDepth");
	const GLint uniformTexGPolyID        = glGetUniformLocation(OGLRef.programEdgeMarkID, "texInPolyID");
	const GLint uniformTexEdgeColorTable = glGetUniformLocation(OGLRef.programEdgeMarkID, "texEdgeColor");
	glUniform1i(uniformTexGDepth, OGLTextureUnitID_DepthStencil);
	glUniform1i(uniformTexGPolyID, OGLTextureUnitID_GPolyID);
	glUniform1i(uniformTexEdgeColorTable, OGLTextureUnitID_LookupTable);
	
	OGLRef.uniformStateClearPolyID       = glGetUniformLocation(OGLRef.programEdgeMarkID, "clearPolyID");
	OGLRef.uniformStateClearDepth        = glGetUniformLocation(OGLRef.programEdgeMarkID, "clearDepth");
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyEdgeMarkProgram()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	if (OGLRef.programEdgeMarkID != 0)
	{
		glDetachShader(OGLRef.programEdgeMarkID, OGLRef.vertexEdgeMarkShaderID);
		glDetachShader(OGLRef.programEdgeMarkID, OGLRef.fragmentEdgeMarkShaderID);
		glDeleteProgram(OGLRef.programEdgeMarkID);
		glDeleteShader(OGLRef.vertexEdgeMarkShaderID);
		glDeleteShader(OGLRef.fragmentEdgeMarkShaderID);
		
		OGLRef.programEdgeMarkID = 0;
		OGLRef.vertexEdgeMarkShaderID = 0;
		OGLRef.fragmentEdgeMarkShaderID = 0;
	}
	
	if (OGLRef.programMSEdgeMarkID != 0)
	{
		glDetachShader(OGLRef.programMSEdgeMarkID, OGLRef.vertexMSEdgeMarkShaderID);
		glDetachShader(OGLRef.programMSEdgeMarkID, OGLRef.fragmentMSEdgeMarkShaderID);
		glDeleteProgram(OGLRef.programMSEdgeMarkID);
		glDeleteShader(OGLRef.vertexMSEdgeMarkShaderID);
		glDeleteShader(OGLRef.fragmentMSEdgeMarkShaderID);
		
		OGLRef.programMSEdgeMarkID = 0;
		OGLRef.vertexMSEdgeMarkShaderID = 0;
		OGLRef.fragmentMSEdgeMarkShaderID = 0;
	}
}

Render3DError OpenGLRenderer_1_2::CreateFogProgram(const OGLFogProgramKey fogProgramKey, const bool isMultisample, const char *vtxShaderCString, const char *fragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	OGLRenderRef &OGLRef = *this->ref;
	
	if (vtxShaderCString == NULL)
	{
		INFO("OpenGL: The FOG vertex shader is unavailable.\n");
		error = OGLERROR_VERTEX_SHADER_PROGRAM_LOAD_ERROR;
		return error;
	}
	else if (fragShaderCString == NULL)
	{
		INFO("OpenGL: The FOG fragment shader is unavailable.\n");
		error = OGLERROR_FRAGMENT_SHADER_PROGRAM_LOAD_ERROR;
		return error;
	}
	
	const s32 fogOffset = fogProgramKey.offset;
	const GLfloat fogOffsetf = (GLfloat)fogOffset / 32767.0f;
	const s32 fogStep = 0x0400 >> fogProgramKey.shift;
	
	std::stringstream fragDepthConstants;
	fragDepthConstants << "#define FOG_OFFSET " << fogOffset << "\n";
	fragDepthConstants << "#define FOG_OFFSETF " << fogOffsetf << (((fogOffsetf == 0.0f) || (fogOffsetf == 1.0f)) ? ".0" : "") << "\n";
	fragDepthConstants << "#define FOG_STEP " << fogStep << "\n";
	fragDepthConstants << "\n";
	
	std::string fragShaderCode = fragDepthConstants.str() + std::string(fragShaderCString);
	
	OGLFogShaderID shaderID;
	shaderID.program = 0;
	shaderID.fragShader = 0;
	
	error = ShaderProgramCreateOGL(OGLRef.vertexFogShaderID,
	                               shaderID.fragShader,
	                               shaderID.program,
	                               vtxShaderCString,
	                               fragShaderCode.c_str());
	
	this->_fogProgramMap[fogProgramKey.key] = shaderID;
	
	if (error != OGLERROR_NOERR)
	{
		INFO("OpenGL: Failed to create the FOG shader program.\n");
		glUseProgram(0);
		this->DestroyFogProgram(fogProgramKey);
		return error;
	}
	
	glBindAttribLocation(shaderID.program, OGLVertexAttributeID_Position, "inPosition");
	glBindAttribLocation(shaderID.program, OGLVertexAttributeID_TexCoord0, "inTexCoord0");
	
	glLinkProgram(shaderID.program);
	if (!ValidateShaderProgramLinkOGL(shaderID.program))
	{
		INFO("OpenGL: Failed to link the FOG shader program.\n");
		glUseProgram(0);
		this->DestroyFogProgram(fogProgramKey);
		return OGLERROR_SHADER_CREATE_ERROR;
	}
	
	glValidateProgram(shaderID.program);
	glUseProgram(shaderID.program);
	
	const GLint uniformTexGDepth          = glGetUniformLocation(shaderID.program, "texInFragDepth");
	const GLint uniformTexGFog            = glGetUniformLocation(shaderID.program, "texInFogAttributes");
	const GLint uniformTexFogDensityTable = glGetUniformLocation(shaderID.program, "texFogDensityTable");
	glUniform1i(uniformTexGDepth, OGLTextureUnitID_DepthStencil);
	glUniform1i(uniformTexGFog, OGLTextureUnitID_FogAttr);
	glUniform1i(uniformTexFogDensityTable, OGLTextureUnitID_LookupTable);
	
	OGLRef.uniformStateEnableFogAlphaOnly = glGetUniformLocation(shaderID.program, "stateEnableFogAlphaOnly");
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::DestroyFogProgram(const OGLFogProgramKey fogProgramKey)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	std::map<u32, OGLFogShaderID>::iterator it = this->_fogProgramMap.find(fogProgramKey.key);
	if (it == this->_fogProgramMap.end())
	{
		return;
	}
	
	OGLFogShaderID shaderID = this->_fogProgramMap[fogProgramKey.key];
	glDetachShader(shaderID.program, OGLRef.vertexFogShaderID);
	glDetachShader(shaderID.program, shaderID.fragShader);
	glDeleteProgram(shaderID.program);
	glDeleteShader(shaderID.fragShader);
	
	this->_fogProgramMap.erase(it);
	
	if (this->_fogProgramMap.size() == 0)
	{
		glDeleteShader(OGLRef.vertexFogShaderID);
		OGLRef.vertexFogShaderID = 0;
	}
}

void OpenGLRenderer_1_2::DestroyFogPrograms()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	while (this->_fogProgramMap.size() > 0)
	{
		std::map<u32, OGLFogShaderID>::iterator it = this->_fogProgramMap.begin();
		OGLFogShaderID shaderID = it->second;
		
		glDetachShader(shaderID.program, OGLRef.vertexFogShaderID);
		glDetachShader(shaderID.program, shaderID.fragShader);
		glDeleteProgram(shaderID.program);
		glDeleteShader(shaderID.fragShader);
		
		this->_fogProgramMap.erase(it);
		
		if (this->_fogProgramMap.size() == 0)
		{
			glDeleteShader(OGLRef.vertexFogShaderID);
			OGLRef.vertexFogShaderID = 0;
		}
	}
}

Render3DError OpenGLRenderer_1_2::InitPostprocessingPrograms(const char *edgeMarkVtxShaderCString, const char *edgeMarkFragShaderCString)
{
	Render3DError error = OGLERROR_NOERR;
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->_feature.supportVBO && this->_feature.supportFBO)
	{
		error = this->CreateEdgeMarkProgram(false, edgeMarkVtxShaderCString, edgeMarkFragShaderCString);
		if (error != OGLERROR_NOERR)
		{
			return error;
		}
	}
	
	glUseProgram(OGLRef.programGeometryID[0]);
	INFO("OpenGL: Successfully created postprocess shaders.\n");
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::UploadClearImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 opaquePolyID)
{
	OGLRenderRef &OGLRef = *this->ref;
	this->_clearImageIndex ^= 0x01;
	
	if (this->_enableFog && this->_deviceInfo.isFogSupported)
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT; i++)
		{
			OGLRef.workingCIDepthStencilBuffer[this->_clearImageIndex][i] = (depthBuffer[i] << 8) | opaquePolyID;
			OGLRef.workingCIFogAttributesBuffer[this->_clearImageIndex][i] = (fogBuffer[i]) ? 0xFF0000FF : 0xFF000000;
		}
	}
	else
	{
		for (size_t i = 0; i < GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT; i++)
		{
			OGLRef.workingCIDepthStencilBuffer[this->_clearImageIndex][i] = (depthBuffer[i] << 8) | opaquePolyID;
		}
	}
	
	const bool didColorChange = (memcmp(OGLRef.workingCIColorBuffer16, colorBuffer, GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT * sizeof(u16)) != 0);
	const bool didDepthStencilChange = (memcmp(OGLRef.workingCIDepthStencilBuffer[this->_clearImageIndex], OGLRef.workingCIDepthStencilBuffer[this->_clearImageIndex ^ 0x01], GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT * sizeof(GLuint)) != 0);
	const bool didFogAttributesChange = this->_enableFog && this->_deviceInfo.isFogSupported && (memcmp(OGLRef.workingCIFogAttributesBuffer[this->_clearImageIndex], OGLRef.workingCIFogAttributesBuffer[this->_clearImageIndex ^ 0x01], GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT * sizeof(GLuint)) != 0);
	
	if (didColorChange)
	{
		memcpy(OGLRef.workingCIColorBuffer16, colorBuffer, GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT * sizeof(u16));
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_CIColor);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIColorID);
		
		if (OGLRef.textureSrcTypeCIColor == GL_UNSIGNED_BYTE)
		{
			ColorspaceConvertBuffer5551To8888<false, false, BESwapDst>(OGLRef.workingCIColorBuffer16, OGLRef.workingCIColorBuffer32, GPU_FRAMEBUFFER_NATIVE_WIDTH * GPU_FRAMEBUFFER_NATIVE_HEIGHT);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, OGLRef.textureSrcTypeCIColor, OGLRef.workingCIColorBuffer32);
		}
		else
		{
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, OGLRef.textureSrcTypeCIColor, OGLRef.workingCIColorBuffer16);
		}
	}
	
	if (didDepthStencilChange)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_CIDepth);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIDepthStencilID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, OGLRef.workingCIDepthStencilBuffer[this->_clearImageIndex]);
	}
	
	if (didFogAttributesChange)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_CIFogAttr);
		glBindTexture(GL_TEXTURE_2D, OGLRef.texCIFogAttrID);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, GL_RGBA, OGLRef.textureSrcTypeCIFog, OGLRef.workingCIFogAttributesBuffer[this->_clearImageIndex]);
	}
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::GetExtensionSet(std::set<std::string> *oglExtensionSet)
{
	const char *oglExtensionCStr = (const char *)glGetString(GL_EXTENSIONS);
	std::string oglExtensionString = std::string(oglExtensionCStr);
	
	size_t extStringStartLoc = 0;
	size_t delimiterLoc = oglExtensionString.find_first_of(' ', extStringStartLoc);
	while (delimiterLoc != std::string::npos)
	{
		std::string extensionName = oglExtensionString.substr(extStringStartLoc, delimiterLoc - extStringStartLoc);
		oglExtensionSet->insert(extensionName);
		
		extStringStartLoc = delimiterLoc + 1;
		delimiterLoc = oglExtensionString.find_first_of(' ', extStringStartLoc);
	}
	
	if (extStringStartLoc - oglExtensionString.length() > 0)
	{
		std::string extensionName = oglExtensionString.substr(extStringStartLoc, oglExtensionString.length() - extStringStartLoc);
		oglExtensionSet->insert(extensionName);
	}
}

void OpenGLRenderer_1_2::_SetupGeometryShaders(const OGLGeometryFlags flags)
{
	const OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportShaders)
	{
		return;
	}
	
	glUseProgram(OGLRef.programGeometryID[flags.value]);
	glUniform1f(OGLRef.uniformStateAlphaTestRef[flags.value], this->_pendingRenderStates.alphaTestRef);
	glUniform1i(OGLRef.uniformTexDrawOpaque[flags.value], GL_FALSE);
	glUniform1i(OGLRef.uniformDrawModeDepthEqualsTest[flags.value], GL_FALSE);
	glUniform1i(OGLRef.uniformPolyDrawShadow[flags.value], GL_FALSE);
}

void OpenGLRenderer_1_2::_RenderGeometryVertexAttribEnable()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->_feature.supportVAO || this->_feature.supportVAO_APPLE)
	{
		glBindVertexArray(OGLRef.vaoGeometryStatesID);
	}
	else if (this->_feature.supportShaders)
	{
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
		
		glEnableVertexAttribArray(OGLVertexAttributeID_Position);
		glEnableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glEnableVertexAttribArray(OGLVertexAttributeID_Color);
		glVertexAttribPointer(OGLVertexAttributeID_Position, 4, GL_INT, GL_FALSE, sizeof(NDSVertex), (const GLvoid *)offsetof(NDSVertex, position));
		glVertexAttribPointer(OGLVertexAttributeID_TexCoord0, 2, GL_INT, GL_FALSE, sizeof(NDSVertex), (const GLvoid *)offsetof(NDSVertex, texCoord));
		glVertexAttribPointer(OGLVertexAttributeID_Color, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(NDSVertex), (const GLvoid *)offsetof(NDSVertex, color));
	}
#if defined(GL_VERSION_1_2)
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
		
		glEnableClientState(GL_VERTEX_ARRAY);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnableClientState(GL_COLOR_ARRAY);
		glVertexPointer(4, GL_FLOAT, 0, OGLRef.position4fBuffer);
		glTexCoordPointer(2, GL_FLOAT, 0, OGLRef.texCoord2fBuffer);
		glColorPointer(4, GL_FLOAT, 0, OGLRef.color4fBuffer);
	}
#endif
}

void OpenGLRenderer_1_2::_RenderGeometryVertexAttribDisable()
{
	if (this->_feature.supportVAO || this->_feature.supportVAO_APPLE)
	{
		glBindVertexArray(0);
	}
	else if (this->_feature.supportShaders)
	{
		glDisableVertexAttribArray(OGLVertexAttributeID_Position);
		glDisableVertexAttribArray(OGLVertexAttributeID_TexCoord0);
		glDisableVertexAttribArray(OGLVertexAttributeID_Color);
	}
#if defined(GL_VERSION_1_2)
	else
	{
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
	}
#endif
}

Render3DError OpenGLRenderer_1_2::ZeroDstAlphaPass(const POLY *rawPolyList, const CPoly *clippedPolyList, const size_t clippedPolyCount, const size_t clippedPolyOpaqueCount, bool enableAlphaBlending, size_t indexOffset, POLYGON_ATTR lastPolyAttr)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportShaders || !this->_feature.supportFBO || !this->_feature.supportVBO)
	{
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	
	// Pre Pass: Fill in the stencil buffer based on the alpha of the current framebuffer color.
	// Fully transparent pixels (alpha == 0) -- Set stencil buffer to 0
	// All other pixels (alpha != 0) -- Set stencil buffer to 1
	
	const bool isRunningMSAA = this->_feature.supportMultisampledFBO && (OGLRef.selectedRenderingFBO == OGLRef.fboMSIntermediateRenderID);
	
	if (isRunningMSAA)
	{
		// Just downsample the color buffer now so that we have some texture data to sample from in the non-multisample shader.
		// This is not perfectly pixel accurate, but it's better than nothing.
		glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
		glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
		glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
	
	glUseProgram(OGLRef.programGeometryZeroDstAlphaID);
	glViewport(0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	glDisable(GL_BLEND);
	glEnable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);
	
	glStencilFunc(GL_ALWAYS, 0x40, 0x40);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glStencilMask(0x40);
	glDepthMask(GL_FALSE);
	glDrawBuffer(GL_NONE);
	
	FramebufferProcessVertexAttribEnableLEGACYOGL(this->_feature, OGLRef.vaoPostprocessStatesID, OGLRef.vboPostprocessVtxID);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	
	// Setup for multiple pass alpha poly drawing
	OGLGeometryFlags oldGProgramFlags = this->_geometryProgramFlags;
	this->_geometryProgramFlags.EnableEdgeMark = 0;
	this->_geometryProgramFlags.EnableFog = 0;
	this->_geometryProgramFlags.OpaqueDrawMode = 0;
	this->_SetupGeometryShaders(this->_geometryProgramFlags);
	glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	
	this->_RenderGeometryVertexAttribEnable();
	
	// Draw the alpha polys, touching fully transparent pixels only once.
	glEnable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
	glStencilFunc(GL_NOTEQUAL, 0x40, 0x40);
	
	this->DrawPolygonsForIndexRange<OGLPolyDrawMode_ZeroAlphaPass>(rawPolyList, clippedPolyList, clippedPolyCount, clippedPolyOpaqueCount, clippedPolyCount - 1, indexOffset, lastPolyAttr);
	
	// Restore OpenGL states back to normal.
	this->_geometryProgramFlags = oldGProgramFlags;
	this->_SetupGeometryShaders(this->_geometryProgramFlags);
	glDrawBuffers(4, this->_geometryDrawBuffersEnum[this->_geometryProgramFlags.DrawBuffersMode]);
	glClear(GL_STENCIL_BUFFER_BIT);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glStencilMask(0xFF);
	
	if (enableAlphaBlending)
	{
		glEnable(GL_BLEND);
	}
	else
	{
		glDisable(GL_BLEND);
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::_ResolveWorkingBackFacing()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_emulateDepthLEqualPolygonFacing || !this->_isDepthLEqualPolygonFacingSupported || !this->_feature.supportMultisampledFBO || (OGLRef.selectedRenderingFBO != OGLRef.fboMSIntermediateRenderID))
	{
		return;
	}
	
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	
	glReadBuffer(OGL_WORKING_ATTACHMENT_ID);
	glDrawBuffer(OGL_WORKING_ATTACHMENT_ID);
	glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
}

void OpenGLRenderer_1_2::_ResolveGeometry()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_feature.supportMultisampledFBO || (OGLRef.selectedRenderingFBO != OGLRef.fboMSIntermediateRenderID))
	{
		return;
	}
	
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	
	if (this->_feature.supportShaders)
	{
		if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
		{
			glReadBuffer(OGL_POLYID_ATTACHMENT_ID);
			glDrawBuffer(OGL_POLYID_ATTACHMENT_ID);
			glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		
		if (this->_enableFog && this->_deviceInfo.isFogSupported)
		{
			glReadBuffer(OGL_FOGATTRIBUTES_ATTACHMENT_ID);
			glDrawBuffer(OGL_FOGATTRIBUTES_ATTACHMENT_ID);
			glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		
		// Blit the color and depth buffers
		glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
		glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
		glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
	else
	{
		// Blit the color buffer
		glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
	
	// Reset framebuffer targets
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
}

void OpenGLRenderer_1_2::_ResolveFinalFramebuffer()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!this->_enableMultisampledRendering || (OGLRef.selectedRenderingFBO != OGLRef.fboMSIntermediateRenderID))
	{
		return;
	}
	
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
}

Render3DError OpenGLRenderer_1_2::BeginRender(const GFX3D_State &renderState, const GFX3D_GeometryList &renderGList)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	this->_clippedPolyCount = renderGList.clippedPolyCount;
	this->_clippedPolyOpaqueCount = renderGList.clippedPolyOpaqueCount;
	this->_clippedPolyList = (CPoly *)renderGList.clippedPolyList;
	this->_rawPolyList = (POLY *)renderGList.rawPolyList;
	
	this->_enableAlphaBlending = (renderState.DISP3DCNT.EnableAlphaBlending) ? true : false;
	
	if (this->_clippedPolyCount > 0)
	{
		if (this->_feature.supportVBO)
		{
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
			
			if (this->_feature.supportShaders)
			{
				glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
				glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(NDSVertex) * renderGList.rawVertCount, renderGList.rawVtxList);
			}
			else
			{
				// If shaders aren't supported, we need to use the client-side buffers here.
				glBindBuffer(GL_ARRAY_BUFFER, 0);
			}
		}
		
		// Generate the clipped polygon list.
		bool renderNeedsToonTable = false;
		
		for (size_t i = 0, vertIndexCount = 0; i < this->_clippedPolyCount; i++)
		{
			const CPoly &cPoly = this->_clippedPolyList[i];
			const POLY &rawPoly = this->_rawPolyList[cPoly.index];
			const size_t polyType = rawPoly.type;
			
			if (this->_feature.supportShaders)
			{
				for (size_t j = 0; j < polyType; j++)
				{
					const GLushort vertIndex = rawPoly.vertIndexes[j];
					
					// While we're looping through our vertices, add each vertex index to
					// a buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
					// vertices here to convert them to GL_TRIANGLES, which are much easier
					// to work with and won't be deprecated in future OpenGL versions.
					OGLRef.vertIndexBuffer[vertIndexCount++] = vertIndex;
					if (!GFX3D_IsPolyWireframe(rawPoly) && (rawPoly.vtxFormat == GFX3D_QUADS || rawPoly.vtxFormat == GFX3D_QUAD_STRIP))
					{
						if (j == 2)
						{
							OGLRef.vertIndexBuffer[vertIndexCount++] = vertIndex;
						}
						else if (j == 3)
						{
							OGLRef.vertIndexBuffer[vertIndexCount++] = rawPoly.vertIndexes[0];
						}
					}
				}
			}
			else
			{
				const GLfloat thePolyAlpha = (GFX3D_IsPolyWireframe(rawPoly)) ? 1.0f : divide5bitBy31_LUT[rawPoly.attribute.Alpha];
				
				for (size_t j = 0; j < polyType; j++)
				{
					const GLushort vtxIndex = rawPoly.vertIndexes[j];
					const size_t positionIndex = vtxIndex * 4;
					const size_t texCoordIndex = vtxIndex * 2;
					const size_t colorIndex = vtxIndex * 4;
					const NDSVertex &vtx = renderGList.rawVtxList[vtxIndex];
					
					// Since we can't use shaders, we can't perform any data format conversions
					// of the vertex data on the GPU. Therefore, we need to do the conversions
					// on the CPU instead.
					OGLRef.position4fBuffer[positionIndex+0] = (float)vtx.position.x / 4096.0f;
					OGLRef.position4fBuffer[positionIndex+1] = (float)vtx.position.y / 4096.0f;
					OGLRef.position4fBuffer[positionIndex+2] = (float)vtx.position.z / 4096.0f;
					OGLRef.position4fBuffer[positionIndex+3] = (float)vtx.position.w / 4096.0f;
					
					OGLRef.texCoord2fBuffer[texCoordIndex+0] = (float)vtx.texCoord.s / 16.0f;
					OGLRef.texCoord2fBuffer[texCoordIndex+1] = (float)vtx.texCoord.t / 16.0f;
					
					OGLRef.color4fBuffer[colorIndex+0] = divide6bitBy63_LUT[vtx.color.r];
					OGLRef.color4fBuffer[colorIndex+1] = divide6bitBy63_LUT[vtx.color.g];
					OGLRef.color4fBuffer[colorIndex+2] = divide6bitBy63_LUT[vtx.color.b];
					OGLRef.color4fBuffer[colorIndex+3] = thePolyAlpha;
					
					// While we're looping through our vertices, add each vertex index to a
					// buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
					// vertices here to convert them to GL_TRIANGLES, which are much easier
					// to work with and won't be deprecated in future OpenGL versions.
					OGLRef.vertIndexBuffer[vertIndexCount++] = vtxIndex;
					if (!GFX3D_IsPolyWireframe(rawPoly) && (rawPoly.vtxFormat == GFX3D_QUADS || rawPoly.vtxFormat == GFX3D_QUAD_STRIP))
					{
						if (j == 2)
						{
							OGLRef.vertIndexBuffer[vertIndexCount++] = vtxIndex;
						}
						else if (j == 3)
						{
							OGLRef.vertIndexBuffer[vertIndexCount++] = rawPoly.vertIndexes[0];
						}
					}
				}
			}
			
			renderNeedsToonTable = (renderNeedsToonTable || (rawPoly.attribute.Mode == POLYGON_MODE_TOONHIGHLIGHT)) && this->_feature.supportShaders;
			
			// Get the texture that is to be attached to this polygon.
			this->_textureList[i] = this->GetLoadedTextureFromPolygon(rawPoly, this->_enableTextureSampling);
		}
		
		if (this->_feature.supportVBO)
		{
			// Replace the entire index buffer as a hint to the driver that we can orphan the index buffer and
			// avoid a synchronization cost.
			glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(OGLRef.vertIndexBuffer), OGLRef.vertIndexBuffer);
		}
		
		// Set up rendering states that will remain constant for the entire frame.
		this->_pendingRenderStates.enableAntialiasing = (renderState.DISP3DCNT.EnableAntialiasing) ? GL_TRUE : GL_FALSE;
		this->_pendingRenderStates.enableFogAlphaOnly = (renderState.DISP3DCNT.FogOnlyAlpha) ? GL_TRUE : GL_FALSE;
		this->_pendingRenderStates.clearPolyID = this->_clearAttributes.opaquePolyID;
		this->_pendingRenderStates.clearDepth = (GLfloat)this->_clearAttributes.depth / (GLfloat)0x00FFFFFF;
		this->_pendingRenderStates.alphaTestRef = divide5bitBy31_LUT[renderState.alphaTestRef];
		
		if (this->_enableFog && this->_deviceInfo.isFogSupported)
		{
			this->_fogProgramKey.key = 0;
			this->_fogProgramKey.offset = renderState.fogOffset & 0x7FFF;
			this->_fogProgramKey.shift = renderState.fogShift;
			
			this->_pendingRenderStates.fogColor.r = divide5bitBy31_LUT[(renderState.fogColor      ) & 0x0000001F];
			this->_pendingRenderStates.fogColor.g = divide5bitBy31_LUT[(renderState.fogColor >>  5) & 0x0000001F];
			this->_pendingRenderStates.fogColor.b = divide5bitBy31_LUT[(renderState.fogColor >> 10) & 0x0000001F];
			this->_pendingRenderStates.fogColor.a = divide5bitBy31_LUT[(renderState.fogColor >> 16) & 0x0000001F];
			this->_pendingRenderStates.fogOffset = (GLfloat)(renderState.fogOffset & 0x7FFF) / 32767.0f;
			this->_pendingRenderStates.fogStep = (GLfloat)(0x0400 >> renderState.fogShift) / 32767.0f;
			
			u8 fogDensityTable[32];
			for (size_t i = 0; i < 32; i++)
			{
				fogDensityTable[i] = (renderState.fogDensityTable[i] == 127) ? 255 : renderState.fogDensityTable[i] << 1;
			}
			
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texFogDensityTableID);
			glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RED, GL_UNSIGNED_BYTE, fogDensityTable);
		}
		
		if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
		{
			const u8 alpha8 = (renderState.DISP3DCNT.EnableAntialiasing) ? 0x80 : 0xFF;
			Color4u8 edgeColor32[8];
			
			for (size_t i = 0; i < 8; i++)
			{
				edgeColor32[i].value = COLOR555TO8888(renderState.edgeMarkColorTable[i] & 0x7FFF, alpha8);
			}
			
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texEdgeColorTableID);
			glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 8, GL_RGBA, OGLRef.textureSrcTypeEdgeColor, edgeColor32);
		}
		
		if (this->_feature.supportShaders)
		{
			this->_geometryProgramFlags.value = 0;
			this->_geometryProgramFlags.EnableWDepth = renderState.SWAP_BUFFERS.DepthMode;
			this->_geometryProgramFlags.EnableAlphaTest = renderState.DISP3DCNT.EnableAlphaTest;
			this->_geometryProgramFlags.EnableTextureSampling = (this->_enableTextureSampling) ? 1 : 0;
			this->_geometryProgramFlags.ToonShadingMode = renderState.DISP3DCNT.PolygonShading;
			this->_geometryProgramFlags.EnableFog = (this->_enableFog && this->_deviceInfo.isFogSupported) ? 1 : 0;
			this->_geometryProgramFlags.EnableEdgeMark = (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported) ? 1 : 0;
			this->_geometryProgramFlags.OpaqueDrawMode = 1;
			
			this->_SetupGeometryShaders(this->_geometryProgramFlags);
			
			if (renderNeedsToonTable)
			{
				glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
				glBindTexture(GL_TEXTURE_1D, OGLRef.texToonTableID);
				
				if (OGLRef.textureSrcTypeToonTable == GL_UNSIGNED_BYTE)
				{
					ColorspaceConvertBuffer555xTo8888Opaque<false, false, BESwapDst>(renderState.toonTable16, OGLRef.toonTable32, 32);
					glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RGBA, OGLRef.textureSrcTypeToonTable, OGLRef.toonTable32);
				}
				else
				{
					glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RGBA, OGLRef.textureSrcTypeToonTable, renderState.toonTable16);
				}
			}
		}
#if defined(GL_VERSION_1_2)
		else
		{
			if (renderState.DISP3DCNT.EnableAlphaTest && (renderState.alphaTestRef > 0))
			{
				glAlphaFunc(GL_GEQUAL, divide5bitBy31_LUT[renderState.alphaTestRef]);
			}
			else
			{
				glAlphaFunc(GL_GREATER, 0);
			}
		}
#endif
	}
	else
	{
		// Even with no polygons to draw, we always need to set these 3 flags so that
		// glDrawBuffers() can reference the correct set of FBO attachments using
		// OGLGeometryFlags.DrawBuffersMode.
		this->_geometryProgramFlags.EnableFog = (this->_enableFog && this->_deviceInfo.isFogSupported) ? 1 : 0;
		this->_geometryProgramFlags.EnableEdgeMark = (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported) ? 1 : 0;
		this->_geometryProgramFlags.OpaqueDrawMode = 1;
	}
	
	this->_needsZeroDstAlphaPass = true;
	
	this->_lastBoundColorOut = this->_colorOut->BindRenderer();
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::_RenderGeometryLoopBegin()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	glDisable(GL_CULL_FACE); // Polygons should already be culled before we get here.
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	
	if (this->_enableAlphaBlending)
	{
		glEnable(GL_BLEND);
		
		if (this->_feature.supportBlendFuncSeparate)
		{
			if (this->_feature.supportBlendEquationSeparate)
			{
				// we want to use alpha destination blending so we can track the last-rendered alpha value
				// test: new super mario brothers renders the stormclouds at the beginning
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
				glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
			}
			else
			{
				glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_DST_ALPHA);
			}
		}
		else
		{
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}
	}
	else
	{
		glDisable(GL_BLEND);
	}
	
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	
#if defined(GL_VERSION_1_2)
	if (!this->_feature.supportShaders)
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glScalef(1.0f, -1.0f, 1.0f);
	}
#endif
	
	glActiveTexture(GL_TEXTURE0);
	
	if (this->_feature.supportFBO)
	{
		if (this->_enableMultisampledRendering)
		{
			OGLRef.selectedRenderingFBO = OGLRef.fboMSIntermediateRenderID;
		}
		else
		{
			OGLRef.selectedRenderingFBO = OGLRef.fboRenderID;
		}
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
		
		if (this->_feature.supportShaders)
		{
			glDrawBuffers(4, this->_geometryDrawBuffersEnum[this->_geometryProgramFlags.DrawBuffersMode]);
		}
		else
		{
			glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
		}
		
		glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
	}
	
	this->_RenderGeometryVertexAttribEnable();
}

void OpenGLRenderer_1_2::_RenderGeometryLoopEnd()
{
	this->_RenderGeometryVertexAttribDisable();
	
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	
#if defined(GL_VERSION_1_2)
	if (!this->_feature.supportShaders)
	{
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glScalef(1.0f, 1.0f, 1.0f);
	}
#endif
}

Render3DError OpenGLRenderer_1_2::RenderGeometry()
{
	if (this->_clippedPolyCount > 0)
	{
		this->_RenderGeometryLoopBegin();
		
		size_t indexOffset = 0;
		
		const CPoly &firstCPoly = this->_clippedPolyList[0];
		const POLY *rawPolyList = this->_rawPolyList;
		const POLY &firstPoly = rawPolyList[firstCPoly.index];
		POLYGON_ATTR lastPolyAttr = firstPoly.attribute;
		
		if (this->_clippedPolyOpaqueCount > 0)
		{
			this->SetupPolygon(firstPoly, false, true, firstCPoly.isPolyBackFacing);
			this->DrawPolygonsForIndexRange<OGLPolyDrawMode_DrawOpaquePolys>(rawPolyList, this->_clippedPolyList, this->_clippedPolyCount, 0, this->_clippedPolyOpaqueCount - 1, indexOffset, lastPolyAttr);
		}
		
		if (this->_clippedPolyOpaqueCount < this->_clippedPolyCount)
		{
			// We are now rendering the transparent polys.
			this->_geometryProgramFlags.OpaqueDrawMode = 0;
			
			// Geometry flags have changed, so we need to update shader uniforms and also
			// update draw buffers based on the flags.
			this->_SetupGeometryShaders(this->_geometryProgramFlags);
			if (this->_feature.supportFBO && this->_feature.supportShaders)
			{
				glDrawBuffers(4, this->_geometryDrawBuffersEnum[this->_geometryProgramFlags.DrawBuffersMode]);
			}
			
			if (this->_needsZeroDstAlphaPass && this->_emulateSpecialZeroAlphaBlending)
			{
				if (this->_clippedPolyOpaqueCount == 0)
				{
					this->SetupPolygon(firstPoly, true, false, firstCPoly.isPolyBackFacing);
				}
				
				this->ZeroDstAlphaPass(rawPolyList, this->_clippedPolyList, this->_clippedPolyCount, this->_clippedPolyOpaqueCount, this->_enableAlphaBlending, indexOffset, lastPolyAttr);
				
				if (this->_clippedPolyOpaqueCount > 0)
				{
					const CPoly &lastOpaqueCPoly = this->_clippedPolyList[this->_clippedPolyOpaqueCount - 1];
					const POLY &lastOpaquePoly = rawPolyList[lastOpaqueCPoly.index];
					lastPolyAttr = lastOpaquePoly.attribute;
					this->SetupPolygon(lastOpaquePoly, false, true, lastOpaqueCPoly.isPolyBackFacing);
				}
			}
			else
			{
				// If we're not doing the zero-dst-alpha pass, then we need to make sure to
				// clear the stencil bit that we will use to mark transparent fragments.
				glStencilMask(0x40);
				glClearStencil(0);
				glClear(GL_STENCIL_BUFFER_BIT);
				glStencilMask(0xFF);
			}
			
			if (this->_clippedPolyOpaqueCount == 0)
			{
				this->SetupPolygon(firstPoly, true, true, firstCPoly.isPolyBackFacing);
			}
			else
			{
				this->_ResolveWorkingBackFacing();
			}
			
			this->DrawPolygonsForIndexRange<OGLPolyDrawMode_DrawTranslucentPolys>(rawPolyList, this->_clippedPolyList, this->_clippedPolyCount, this->_clippedPolyOpaqueCount, this->_clippedPolyCount - 1, indexOffset, lastPolyAttr);
		}
		
		this->_RenderGeometryLoopEnd();
	}
	
	if (!this->_willUseMultisampleShaders)
	{
		this->_ResolveGeometry();
	}
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::PostprocessFramebuffer()
{
	if (this->_clippedPolyCount < 1)
	{
		return OGLERROR_NOERR;
	}
	
	if ( !(this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported) &&
		 !(this->_enableFog && this->_deviceInfo.isFogSupported) )
	{
		return OGLERROR_NOERR;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	// Set up the postprocessing states
	if (this->_feature.supportFBO)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
		glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
		glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
	}
	
	glViewport(0, 0, (GLsizei)this->_framebufferWidth, (GLsizei)this->_framebufferHeight);
	glDisable(GL_DEPTH_TEST);
	
	FramebufferProcessVertexAttribEnableLEGACYOGL(this->_feature, OGLRef.vaoPostprocessStatesID, OGLRef.vboPostprocessVtxID);
	
	if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
	{
		if (this->_needsZeroDstAlphaPass && this->_emulateSpecialZeroAlphaBlending)
		{
			// Pass 1: Determine the pixels with zero alpha
			glDrawBuffer(GL_NONE);
			glDisable(GL_BLEND);
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(GL_ALWAYS, 0x40, 0x40);
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0x40);
			
			glUseProgram(OGLRef.programGeometryZeroDstAlphaID);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			
			// Pass 2: Unblended edge mark colors to zero-alpha pixels
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texEdgeColorTableID);
			glActiveTexture(GL_TEXTURE0);
			
			glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
			glUseProgram(OGLRef.programEdgeMarkID);
			glUniform1i(OGLRef.uniformStateClearPolyID, this->_pendingRenderStates.clearPolyID);
			glUniform1f(OGLRef.uniformStateClearDepth, this->_pendingRenderStates.clearDepth);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
			glStencilFunc(GL_NOTEQUAL, 0x40, 0x40);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			
			// Pass 3: Blended edge mark
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
			glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
			glDisable(GL_STENCIL_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
		else
		{
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texEdgeColorTableID);
			glActiveTexture(GL_TEXTURE0);
			
			glUseProgram(OGLRef.programEdgeMarkID);
			glUniform1i(OGLRef.uniformStateClearPolyID, this->_pendingRenderStates.clearPolyID);
			glUniform1f(OGLRef.uniformStateClearDepth, this->_pendingRenderStates.clearDepth);
			glEnable(GL_BLEND);
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA, GL_DST_ALPHA);
			glBlendEquationSeparate(GL_FUNC_ADD, GL_MAX);
			glDisable(GL_STENCIL_TEST);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}
	
	if (this->_enableFog && this->_deviceInfo.isFogSupported)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
		glBindTexture(GL_TEXTURE_1D, OGLRef.texFogDensityTableID);
		glActiveTexture(GL_TEXTURE0);
		
		std::map<u32, OGLFogShaderID>::iterator it = this->_fogProgramMap.find(this->_fogProgramKey.key);
		if (it == this->_fogProgramMap.end())
		{
			Render3DError error = this->CreateFogProgram(this->_fogProgramKey, false, FogVtxShader_100, FogFragShader_100);
			if (error != OGLERROR_NOERR)
			{
				return error;
			}
		}
		
		OGLFogShaderID shaderID = this->_fogProgramMap[this->_fogProgramKey.key];
		glUseProgram(shaderID.program);
		glUniform1i(OGLRef.uniformStateEnableFogAlphaOnly, this->_pendingRenderStates.enableFogAlphaOnly);
		
		glBlendFuncSeparate(GL_CONSTANT_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendColor( this->_pendingRenderStates.fogColor.r,
		              this->_pendingRenderStates.fogColor.g,
		              this->_pendingRenderStates.fogColor.b,
		              this->_pendingRenderStates.fogColor.a );
		
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_STENCIL_TEST);
		glEnable(GL_BLEND);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	
	FramebufferProcessVertexAttribDisableLEGACYOGL(this->_feature);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::EndRender()
{
	if (this->_willUseMultisampleShaders)
	{
		this->_ResolveFinalFramebuffer();
	}
	
	this->_colorOut->UnbindRenderer(this->_lastBoundColorOut);
	this->_lastBoundColorOut = RENDER3D_RESOURCE_INDEX_NONE;
	
	//needs to happen before endgl because it could free some textureids for expired cache items
	texCache.Evict();
	
	ENDGL();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::ClearUsingImage(const u16 *__restrict colorBuffer, const u32 *__restrict depthBuffer, const u8 *__restrict fogBuffer, const u8 opaquePolyID)
{
	if (!this->_feature.supportFBO || !this->_feature.supportFBOBlit)
	{
		return OGLERROR_FEATURE_UNSUPPORTED;
	}
	
	OGLRenderRef &OGLRef = *this->ref;
	
	this->UploadClearImage(colorBuffer, depthBuffer, fogBuffer, opaquePolyID);
	
	glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboClearImageID);
	glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
	
	// It might seem wasteful to be doing a separate glClear(GL_STENCIL_BUFFER_BIT) instead
	// of simply blitting the stencil buffer with everything else.
	//
	// We do this because glBlitFramebufferEXT() for GL_STENCIL_BUFFER_BIT has been tested
	// to be unsupported on ATI/AMD GPUs running in compatibility mode. So we do the separate
	// glClear() for GL_STENCIL_BUFFER_BIT to keep these GPUs working.
	glClearStencil(opaquePolyID);
	glClear(GL_STENCIL_BUFFER_BIT);
	
	if (this->_feature.supportShaders)
	{
		if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported)
		{
			glDrawBuffer(OGL_WORKING_ATTACHMENT_ID);
			glClearColor(0.0, 0.0, 0.0, 0.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
		{
			glDrawBuffer(OGL_POLYID_ATTACHMENT_ID);
			glClearColor((GLfloat)opaquePolyID/63.0f, 0.0, 0.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		if (this->_enableFog && this->_deviceInfo.isFogSupported)
		{
			glReadBuffer(OGL_CI_FOGATTRIBUTES_ATTACHMENT_ID);
			glDrawBuffer(OGL_FOGATTRIBUTES_ATTACHMENT_ID);
			glBlitFramebufferEXT(0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
		
		// Blit the color buffer. Do this last so that color attachment 0 is set to the read FBO.
		glReadBuffer(OGL_CI_COLOROUT_ATTACHMENT_ID);
		glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
		glBlitFramebufferEXT(0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
	else
	{
		glBlitFramebufferEXT(0, 0, GPU_FRAMEBUFFER_NATIVE_WIDTH, GPU_FRAMEBUFFER_NATIVE_HEIGHT, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
	}
	
	if (this->_enableMultisampledRendering)
	{
		glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT, OGLRef.fboRenderID);
		glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT, OGLRef.fboMSIntermediateRenderID);
		
		// See above comment for why we need to get clear the stencil buffer separately.
		glClearStencil(opaquePolyID);
		glClear(GL_STENCIL_BUFFER_BIT);
		
		if (this->_feature.supportShaders)
		{
			if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported)
			{
				glDrawBuffer(OGL_WORKING_ATTACHMENT_ID);
				glClearColor(0.0, 0.0, 0.0, 0.0);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			
			if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
			{
				glDrawBuffer(OGL_POLYID_ATTACHMENT_ID);
				glClearColor((GLfloat)opaquePolyID/63.0f, 0.0, 0.0, 1.0);
				glClear(GL_COLOR_BUFFER_BIT);
			}
			
			if (this->_enableFog && this->_deviceInfo.isFogSupported)
			{
				glReadBuffer(OGL_FOGATTRIBUTES_ATTACHMENT_ID);
				glDrawBuffer(OGL_FOGATTRIBUTES_ATTACHMENT_ID);
				glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			}
			
			// Blit the color and depth buffers. Do this last so that color attachment 0 is set to the read FBO.
			glReadBuffer(OGL_COLOROUT_ATTACHMENT_ID);
			glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
			glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}
		else
		{
			// Blit the color and depth buffers.
			glBlitFramebufferEXT(0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, 0, 0, (GLint)this->_framebufferWidth, (GLint)this->_framebufferHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
		}
	}
	
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
	glDrawBuffers(4, this->_geometryDrawBuffersEnum[this->_geometryProgramFlags.DrawBuffersMode]);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::ClearUsingValues(const Color4u8 &clearColor6665, const FragmentAttributes &clearAttributes)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (this->_feature.supportShaders && this->_feature.supportFBO)
	{
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
		
		glDrawBuffer(OGL_COLOROUT_ATTACHMENT_ID);
		glClearColor(divide6bitBy63_LUT[clearColor6665.r], divide6bitBy63_LUT[clearColor6665.g], divide6bitBy63_LUT[clearColor6665.b], divide5bitBy31_LUT[clearColor6665.a]);
		glClearDepth((GLclampd)clearAttributes.depth / (GLclampd)0x00FFFFFF);
		glClearStencil(clearAttributes.opaquePolyID);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		
		if (this->_emulateDepthLEqualPolygonFacing && this->_isDepthLEqualPolygonFacingSupported)
		{
			glDrawBuffer(OGL_WORKING_ATTACHMENT_ID);
			glClearColor(0.0, 0.0, 0.0, 0.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
		{
			glDrawBuffer(OGL_POLYID_ATTACHMENT_ID);
			glClearColor((GLfloat)clearAttributes.opaquePolyID/63.0f, 0.0, 0.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		if (this->_enableFog && this->_deviceInfo.isFogSupported)
		{
			glDrawBuffer(OGL_FOGATTRIBUTES_ATTACHMENT_ID);
			glClearColor(clearAttributes.isFogged, 0.0, 0.0, 1.0);
			glClear(GL_COLOR_BUFFER_BIT);
		}
		
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, OGLRef.selectedRenderingFBO);
		glDrawBuffers(4, this->_geometryDrawBuffersEnum[this->_geometryProgramFlags.DrawBuffersMode]);
		this->_needsZeroDstAlphaPass = (clearColor6665.a == 0);
	}
	else
	{
		glClearColor(divide6bitBy63_LUT[clearColor6665.r], divide6bitBy63_LUT[clearColor6665.g], divide6bitBy63_LUT[clearColor6665.b], divide5bitBy31_LUT[clearColor6665.a]);
		glClearDepth((GLclampd)clearAttributes.depth / (GLclampd)0x00FFFFFF);
		glClearStencil(clearAttributes.opaquePolyID);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
	
	return OGLERROR_NOERR;
}

void OpenGLRenderer_1_2::SetPolygonIndex(const size_t index)
{
	this->_currentPolyIndex = index;
}

Render3DError OpenGLRenderer_1_2::SetupPolygon(const POLY &thePoly, bool treatAsTranslucent, bool willChangeStencilBuffer, bool isBackFacing)
{
	// Set up depth test mode
	glDepthFunc((thePoly.attribute.DepthEqualTest_Enable) ? GL_EQUAL : GL_LESS);
	
	if (willChangeStencilBuffer)
	{
		// Handle drawing states for the polygon
		if (thePoly.attribute.Mode == POLYGON_MODE_SHADOW)
		{
			if (this->_emulateShadowPolygon)
			{
				// Set up shadow polygon states.
				//
				// See comments in DrawShadowPolygon() for more information about
				// how this 5-pass process works in OpenGL.
				if (thePoly.attribute.PolygonID == 0)
				{
					// 1st pass: Use stencil buffer bit 7 (0x80) for the shadow volume mask.
					// Write only on depth-fail.
					glStencilFunc(GL_ALWAYS, 0x80, 0x80);
					glStencilOp(GL_KEEP, GL_REPLACE, GL_KEEP);
					glStencilMask(0x80);
				}
				else
				{
					// 2nd pass: Compare stencil buffer bits 0-5 (0x3F) with this polygon's ID. If this stencil
					// test fails, remove the fragment from the shadow volume mask by clearing bit 7.
					glStencilFunc(GL_NOTEQUAL, thePoly.attribute.PolygonID, 0x3F);
					glStencilOp(GL_ZERO, GL_KEEP, GL_KEEP);
					glStencilMask(0x80);
				}
				
				glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
				glDepthMask(GL_FALSE);
			}
		}
		else
		{
			// Polygon IDs are always written for every polygon, whether they are opaque or transparent, just as
			// long as they pass the stencil and depth tests.
			// - Polygon IDs are contained in stencil bits 0-5 (0x3F).
			// - The translucent fragment flag is contained in stencil bit 6 (0x40).
			//
			// Opaque polygons have no stencil conditions, so if they pass the depth test, then they write out
			// their polygon ID with a translucent fragment flag of 0.
			//
			// Transparent polygons have the stencil condition where they will not draw if they are drawing on
			// top of previously drawn translucent fragments with the same polygon ID. This condition is checked
			// using both polygon ID bits and the translucent fragment flag. If the polygon passes both stencil
			// and depth tests, it writes out its polygon ID with a translucent fragment flag of 1.
			if (treatAsTranslucent)
			{
				glStencilFunc(GL_NOTEQUAL, 0x40 | thePoly.attribute.PolygonID, 0x7F);
			}
			else
			{
				glStencilFunc(GL_ALWAYS, thePoly.attribute.PolygonID, 0x3F);
			}
			
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			glStencilMask(0xFF); // Drawing non-shadow polygons will implicitly reset the shadow volume mask.
			
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDepthMask((!treatAsTranslucent || thePoly.attribute.TranslucentDepthWrite_Enable) ? GL_TRUE : GL_FALSE);
		}
	}
	
	// Set up polygon attributes
	if (this->_feature.supportShaders)
	{
		OGLRenderRef &OGLRef = *this->ref;
		glUniform1i(OGLRef.uniformPolyMode[this->_geometryProgramFlags.value], thePoly.attribute.Mode);
		glUniform1i(OGLRef.uniformPolyEnableFog[this->_geometryProgramFlags.value], (thePoly.attribute.Fog_Enable) ? GL_TRUE : GL_FALSE);
		glUniform1f(OGLRef.uniformPolyAlpha[this->_geometryProgramFlags.value], (GFX3D_IsPolyWireframe(thePoly)) ? 1.0f : divide5bitBy31_LUT[thePoly.attribute.Alpha]);
		glUniform1i(OGLRef.uniformPolyID[this->_geometryProgramFlags.value], thePoly.attribute.PolygonID);
		glUniform1i(OGLRef.uniformPolyIsWireframe[this->_geometryProgramFlags.value], (GFX3D_IsPolyWireframe(thePoly)) ? GL_TRUE : GL_FALSE);
		glUniform1i(OGLRef.uniformPolySetNewDepthForTranslucent[this->_geometryProgramFlags.value], (thePoly.attribute.TranslucentDepthWrite_Enable) ? GL_TRUE : GL_FALSE);
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], 0.0f);
		glUniform1i(OGLRef.uniformPolyIsBackFacing[this->_geometryProgramFlags.value], (isBackFacing) ? GL_TRUE : GL_FALSE);
	}
#if defined(GL_VERSION_1_2)
	else
	{
		// Set the texture blending mode
		static const GLint oglTexBlendMode[4] = {GL_MODULATE, GL_DECAL, GL_MODULATE, GL_MODULATE};
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, oglTexBlendMode[thePoly.attribute.Mode]);
	}
#endif
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::SetupTexture(const POLY &thePoly, size_t polyRenderIndex)
{
	OpenGLTexture *theTexture = (OpenGLTexture *)this->_textureList[polyRenderIndex];
	const NDSTextureFormat packFormat = theTexture->GetPackFormat();
	const OGLRenderRef &OGLRef = *this->ref;
	
	// Check if we need to use textures
	if (!theTexture->IsSamplingEnabled())
	{
		if (this->_feature.supportShaders)
		{
			glUniform1i(OGLRef.uniformPolyEnableTexture[this->_geometryProgramFlags.value], GL_FALSE);
			glUniform1i(OGLRef.uniformTexSingleBitAlpha[this->_geometryProgramFlags.value], GL_FALSE);
			glUniform2f(OGLRef.uniformPolyTexScale[this->_geometryProgramFlags.value], theTexture->GetInvWidth(), theTexture->GetInvHeight());
		}
		else
		{
			glDisable(GL_TEXTURE_2D);
		}
		
		return OGLERROR_NOERR;
	}
	
	// Enable textures if they weren't already enabled
	if (this->_feature.supportShaders)
	{
		glUniform1i(OGLRef.uniformPolyEnableTexture[this->_geometryProgramFlags.value], GL_TRUE);
		glUniform1i(OGLRef.uniformTexSingleBitAlpha[this->_geometryProgramFlags.value], (packFormat != TEXMODE_A3I5 && packFormat != TEXMODE_A5I3) ? GL_TRUE : GL_FALSE);
		glUniform2f(OGLRef.uniformPolyTexScale[this->_geometryProgramFlags.value], theTexture->GetInvWidth(), theTexture->GetInvHeight());
	}
#if defined(GL_VERSION_1_2)
	else
	{
		glEnable(GL_TEXTURE_2D);
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glScalef(theTexture->GetInvWidth(), theTexture->GetInvHeight(), 1.0f);
	}
#endif
	
	glBindTexture(GL_TEXTURE_2D, theTexture->GetID());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ((thePoly.texParam.RepeatS_Enable) ? ((thePoly.texParam.MirroredRepeatS_Enable) ? this->_feature.stateTexMirroredRepeat : GL_REPEAT) : GL_CLAMP_TO_EDGE));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ((thePoly.texParam.RepeatT_Enable) ? ((thePoly.texParam.MirroredRepeatT_Enable) ? this->_feature.stateTexMirroredRepeat : GL_REPEAT) : GL_CLAMP_TO_EDGE));
	
	if (this->_enableTextureSmoothing)
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (this->_textureScalingFactor > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, this->_deviceInfo.maxAnisotropy);
	}
	else
	{
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
	}
	
	theTexture->ResetCacheAge();
	theTexture->IncreaseCacheUsageCount(1);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::SetupViewport(const GFX3D_Viewport viewport)
{
	const GLfloat wScalar = this->_framebufferWidth  / (GLfloat)GPU_FRAMEBUFFER_NATIVE_WIDTH;
	const GLfloat hScalar = this->_framebufferHeight / (GLfloat)GPU_FRAMEBUFFER_NATIVE_HEIGHT;
	
	glViewport(viewport.x * wScalar,
	           (-viewport.y + (192 - viewport.height)) * hScalar, // All vertex Y-coordinates will be flipped, so the viewport Y positioning needs to be flipped as well.
	           viewport.width * wScalar,
	           viewport.height * hScalar);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::DrawShadowPolygon(const GLenum polyPrimitive, const GLsizei vertIndexCount, const GLushort *indexBufferPtr, const bool performDepthEqualTest, const bool enableAlphaDepthWrite, const bool isTranslucent, const u8 opaquePolyID)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	// Shadow polygons are actually drawn over the course of multiple passes.
	// Note that the 1st and 2nd passes are performed using states from SetupPolygon().
	//
	// 1st pass (NDS driven): The NDS creates the shadow volume and updates only the
	// stencil buffer, writing to bit 7 (0x80). Color and depth writes are disabled for this
	// pass.
	//
	// 2nd pass (NDS driven): Normally, stencil buffer bits marked for shadow rendering
	// are supposed to be drawn in this step, but there are additional checks that need to
	// be made before writing out the fragment. Since OpenGL can only do one type of stencil
	// buffer check at a time, we need to do things differently from what the NDS does at
	// this point.
	//
	// In OpenGL, this pass is used only to update the stencil buffer for the polygon
	// ID check, checking bits 0x3F for the polygon ID, and clearing bit 7 (0x80) if this
	// check fails. Color and depth writes are disabled
	//
	// 3rd pass (emulator driven): This pass only occurs when the shadow polygon is
	// transparent, which is the typical case. Since transparent polygons have a rule for
	// which they cannot draw fragments on top of previously drawn translucent fragments with
	// the same polygon IDs, we also need to do an additional polygon ID check to ensure that
	// it isn't a transparent polygon ID. We continue to check bits 0x3F for the polygon ID,
	// in addition to also checking the translucent fragment flag at bit 6 (0x40). If this
	// check fails, then bit 7 (0x80) is cleared. Color and depth writes are disabled for this
	// pass.
	//
	// 4th pass (emulator driven): Use stencil buffer bit 7 (0x80) for the shadow volume
	// mask and write out the polygon ID and translucent fragment flag only to those fragments
	// within the mask. Color and depth writes are disabled for this pass.
	//
	// 5th pass (emulator driven): Use stencil buffer bit 7 (0x80) for the shadow volume
	// mask and draw the shadow polygon fragments only within the mask. Color writes are always
	// enabled and depth writes are enabled if the shadow polygon is opaque or if transparent
	// polygon depth writes are enabled.
	
	// 1st pass: Create the shadow volume.
	if (opaquePolyID == 0)
	{
		if (performDepthEqualTest && this->_emulateNDSDepthCalculation && this->_feature.supportShaders)
		{
			// Use the stencil buffer to determine which fragments fail the depth test using the lower-side tolerance.
			glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
			glDepthFunc(GL_LEQUAL);
			glStencilFunc(GL_ALWAYS, 0x80, 0x80);
			glStencilOp(GL_KEEP, GL_REPLACE, GL_KEEP);
			glStencilMask(0x80);
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			
			// Use the stencil buffer to determine which fragments fail the depth test using the higher-side tolerance.
			glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)-DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
			glDepthFunc(GL_GEQUAL);
			glStencilFunc(GL_NOTEQUAL, 0x80, 0x80);
			glStencilOp(GL_KEEP, GL_REPLACE, GL_KEEP);
			glStencilMask(0x80);
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
			
			glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], 0.0f);
		}
		else
		{
			glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		}
		
		return OGLERROR_NOERR;
	}
	
	// 2nd pass: Do the polygon ID check.
	if (performDepthEqualTest && this->_emulateNDSDepthCalculation && this->_feature.supportShaders)
	{
		// Use the stencil buffer to determine which fragments pass the lower-side tolerance.
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
		glDepthFunc(GL_LEQUAL);
		glStencilFunc(GL_EQUAL, 0x80, 0x80);
		glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		
		// Use the stencil buffer to determine which fragments pass the higher-side tolerance.
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], (float)-DEPTH_EQUALS_TEST_TOLERANCE / 16777215.0f);
		glDepthFunc(GL_GEQUAL);
		glStencilFunc(GL_EQUAL, 0x80, 0x80);
		glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		
		// Finally, do the polygon ID check.
		glUniform1f(OGLRef.uniformPolyDepthOffset[this->_geometryProgramFlags.value], 0.0f);
		glDepthFunc(GL_ALWAYS);
		glStencilFunc(GL_NOTEQUAL, opaquePolyID, 0x3F);
		glStencilOp(GL_ZERO, GL_ZERO, GL_KEEP);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	}
	else
	{
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	}
	
	// 3rd pass: Do the transparent polygon ID check. For transparent shadow polygons, we need to
	// also ensure that we're not drawing over translucent fragments with the same polygon IDs.
	if (isTranslucent)
	{
		glStencilFunc(GL_NOTEQUAL, 0xC0 | opaquePolyID, 0x7F);
		glStencilOp(GL_ZERO, GL_KEEP, GL_KEEP);
		glStencilMask(0x80);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	}
	
	// 4th pass: Update the polygon IDs in the stencil buffer.
	glStencilFunc(GL_EQUAL, (isTranslucent) ? 0xC0 | opaquePolyID : 0x80 | opaquePolyID, 0x80);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	glStencilMask(0x7F);
	glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	
	// 5th pass: Draw the shadow polygon.
	glStencilFunc(GL_EQUAL, 0x80, 0x80);
	// Technically, a depth-fail result should also clear the shadow volume mask, but
	// Mario Kart DS draws shadow polygons better when it doesn't clear bits on depth-fail.
	// I have no idea why this works. - rogerman 2016/12/21
	glStencilOp(GL_ZERO, GL_KEEP, GL_ZERO);
	glStencilMask(0x80);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask((!isTranslucent || enableAlphaDepthWrite) ? GL_TRUE : GL_FALSE);
	
	if (this->_feature.supportShaders)
	{
		glUniform1i(OGLRef.uniformPolyDrawShadow[this->_geometryProgramFlags.value], GL_TRUE);
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
		glUniform1i(OGLRef.uniformPolyDrawShadow[this->_geometryProgramFlags.value], GL_FALSE);
	}
	else
	{
		glDrawElements(polyPrimitive, vertIndexCount, GL_UNSIGNED_SHORT, indexBufferPtr);
	}
	
	// Reset the OpenGL states back to their original shadow polygon states.
	glStencilFunc(GL_NOTEQUAL, opaquePolyID, 0x3F);
	glStencilOp(GL_ZERO, GL_KEEP, GL_KEEP);
	glStencilMask(0x80);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glDepthMask(GL_FALSE);
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::Reset()
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	glFinish();
	
#if defined(GL_VERSION_1_2)
	if (!this->_feature.supportShaders)
	{
		glEnable(GL_NORMALIZE);
		glEnable(GL_TEXTURE_1D);
		glEnable(GL_TEXTURE_2D);
		glAlphaFunc(GL_GREATER, 0);
		glEnable(GL_ALPHA_TEST);
		glEnable(GL_BLEND);
	}
#endif
	
	this->_colorOut->Reset();
	
	ENDGL();
	
	if (OGLRef.position4fBuffer != NULL)
	{
		memset(OGLRef.position4fBuffer, 0, VERTLIST_SIZE * 4 * sizeof(GLfloat));
	}
	
	if (OGLRef.texCoord2fBuffer != NULL)
	{
		memset(OGLRef.texCoord2fBuffer, 0, VERTLIST_SIZE * 2 * sizeof(GLfloat));
	}
	
	if (OGLRef.color4fBuffer != NULL)
	{
		memset(OGLRef.color4fBuffer, 0, VERTLIST_SIZE * 4 * sizeof(GLfloat));
	}
	
	this->_currentPolyIndex = 0;
	
	memset(&this->_pendingRenderStates, 0, sizeof(this->_pendingRenderStates));
	
	texCache.Reset();
	
	return OGLERROR_NOERR;
}

Render3DError OpenGLRenderer_1_2::RenderFlush(bool willFlushBuffer32, bool willFlushBuffer16)
{
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	Render3DError error = this->Render3D::RenderFlush(willFlushBuffer32, willFlushBuffer16);
	
	ENDGL();
	
	return error;
}

Render3DError OpenGLRenderer_1_2::SetFramebufferSize(size_t w, size_t h)
{
	Render3DError error = OGLERROR_NOERR;
	
	if (w < GPU_FRAMEBUFFER_NATIVE_WIDTH || h < GPU_FRAMEBUFFER_NATIVE_HEIGHT)
	{
		return error;
	}
	
	if (!BEGINGL())
	{
		error = OGLERROR_BEGINGL_FAILED;
		return error;
	}
	
	glFinish();
	
	const size_t newFramebufferColorSizeBytes = w * h * sizeof(Color4u8);
	this->_colorOut->SetSize(w, h);
	
	if (this->_feature.supportShaders || this->_feature.supportFBO)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FinalColor);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
	}
	
	if (this->_feature.supportFBO)
	{
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_DepthStencil);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8_EXT, (GLsizei)w, (GLsizei)h, 0, GL_DEPTH_STENCIL_EXT, GL_UNSIGNED_INT_24_8_EXT, NULL);
		
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GColor);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
		
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_GPolyID);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
		
		glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_FogAttr);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0, this->_feature.readPixelsBestFormat, this->_feature.readPixelsBestDataType, NULL);
	}
	
	glActiveTexture(GL_TEXTURE0);
	
	this->_framebufferWidth = w;
	this->_framebufferHeight = h;
	this->_framebufferPixCount = w * h;
	this->_framebufferColorSizeBytes = newFramebufferColorSizeBytes;
	
	// Call ResizeMultisampledFBOs() after _framebufferWidth and _framebufferHeight are set
	// since this method depends on them.
	GLsizei sampleSize = this->GetLimitedMultisampleSize();
	this->ResizeMultisampledFBOs(sampleSize);
	
	if (this->_feature.supportShaders)
	{
		// Recreate shaders that use the framebuffer size.
		glUseProgram(0);
		this->DestroyEdgeMarkProgram();
		this->DestroyGeometryPrograms();
		
		this->CreateGeometryPrograms();
		
		if (this->_feature.supportVBO && this->_feature.supportFBO)
		{
			this->CreateEdgeMarkProgram(false, EdgeMarkVtxShader_100, EdgeMarkFragShader_100);
		}
	}
	
	if (oglrender_framebufferDidResizeCallback != NULL)
	{
		bool clientResizeSuccess = oglrender_framebufferDidResizeCallback(this->_feature.supportFBO, w, h);
		if (!clientResizeSuccess)
		{
			error = OGLERROR_CLIENT_RESIZE_ERROR;
		}
	}
	
	glFinish();
	ENDGL();
	
	return error;
}

OpenGLRenderer_2_0::OpenGLRenderer_2_0()
{
	_feature.variantID = OpenGLVariantID_Legacy_2_0;
}

Render3DError OpenGLRenderer_2_0::BeginRender(const GFX3D_State &renderState, const GFX3D_GeometryList &renderGList)
{
	OGLRenderRef &OGLRef = *this->ref;
	
	if (!BEGINGL())
	{
		return OGLERROR_BEGINGL_FAILED;
	}
	
	this->_clippedPolyCount = renderGList.clippedPolyCount;
	this->_clippedPolyOpaqueCount = renderGList.clippedPolyOpaqueCount;
	this->_clippedPolyList = (CPoly *)renderGList.clippedPolyList;
	this->_rawPolyList = (POLY *)renderGList.rawPolyList;
	
	this->_enableAlphaBlending = (renderState.DISP3DCNT.EnableAlphaBlending) ? true : false;
	
	if (this->_clippedPolyCount > 0)
	{
		glBindBuffer(GL_ARRAY_BUFFER, OGLRef.vboGeometryVtxID);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, OGLRef.iboGeometryIndexID);
		
		// Only copy as much vertex data as we need to, since this can be a potentially large upload size.
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(NDSVertex) * renderGList.rawVertCount, renderGList.rawVtxList);
		
		// Generate the clipped polygon list.
		bool renderNeedsToonTable = false;
		
		for (size_t i = 0, vertIndexCount = 0; i < this->_clippedPolyCount; i++)
		{
			const CPoly &cPoly = this->_clippedPolyList[i];
			const POLY &rawPoly = this->_rawPolyList[cPoly.index];
			const size_t polyType = rawPoly.type;
			
			for (size_t j = 0; j < polyType; j++)
			{
				const GLushort vertIndex = rawPoly.vertIndexes[j];
				
				// While we're looping through our vertices, add each vertex index to
				// a buffer. For GFX3D_QUADS and GFX3D_QUAD_STRIP, we also add additional
				// vertices here to convert them to GL_TRIANGLES, which are much easier
				// to work with and won't be deprecated in future OpenGL versions.
				OGLRef.vertIndexBuffer[vertIndexCount++] = vertIndex;
				if (!GFX3D_IsPolyWireframe(rawPoly) && (rawPoly.vtxFormat == GFX3D_QUADS || rawPoly.vtxFormat == GFX3D_QUAD_STRIP))
				{
					if (j == 2)
					{
						OGLRef.vertIndexBuffer[vertIndexCount++] = vertIndex;
					}
					else if (j == 3)
					{
						OGLRef.vertIndexBuffer[vertIndexCount++] = rawPoly.vertIndexes[0];
					}
				}
			}
			
			renderNeedsToonTable = renderNeedsToonTable || (rawPoly.attribute.Mode == POLYGON_MODE_TOONHIGHLIGHT);
			
			// Get the texture that is to be attached to this polygon.
			this->_textureList[i] = this->GetLoadedTextureFromPolygon(rawPoly, this->_enableTextureSampling);
		}
		
		// Replace the entire index buffer as a hint to the driver that we can orphan the index buffer and
		// avoid a synchronization cost.
		glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, sizeof(OGLRef.vertIndexBuffer), OGLRef.vertIndexBuffer);
		
		// Set up rendering states that will remain constant for the entire frame.
		this->_pendingRenderStates.enableAntialiasing = (renderState.DISP3DCNT.EnableAntialiasing) ? GL_TRUE : GL_FALSE;
		this->_pendingRenderStates.enableFogAlphaOnly = (renderState.DISP3DCNT.FogOnlyAlpha) ? GL_TRUE : GL_FALSE;
		this->_pendingRenderStates.clearPolyID = this->_clearAttributes.opaquePolyID;
		this->_pendingRenderStates.clearDepth = (GLfloat)this->_clearAttributes.depth / (GLfloat)0x00FFFFFF;
		this->_pendingRenderStates.alphaTestRef = divide5bitBy31_LUT[renderState.alphaTestRef];
		
		if (this->_enableFog && this->_deviceInfo.isFogSupported)
		{
			this->_fogProgramKey.key = 0;
			this->_fogProgramKey.offset = renderState.fogOffset & 0x7FFF;
			this->_fogProgramKey.shift = renderState.fogShift;
			
			this->_pendingRenderStates.fogColor.r = divide5bitBy31_LUT[(renderState.fogColor      ) & 0x0000001F];
			this->_pendingRenderStates.fogColor.g = divide5bitBy31_LUT[(renderState.fogColor >>  5) & 0x0000001F];
			this->_pendingRenderStates.fogColor.b = divide5bitBy31_LUT[(renderState.fogColor >> 10) & 0x0000001F];
			this->_pendingRenderStates.fogColor.a = divide5bitBy31_LUT[(renderState.fogColor >> 16) & 0x0000001F];
			this->_pendingRenderStates.fogOffset = (GLfloat)(renderState.fogOffset & 0x7FFF) / 32767.0f;
			this->_pendingRenderStates.fogStep = (GLfloat)(0x0400 >> renderState.fogShift) / 32767.0f;
			
			u8 fogDensityTable[32];
			for (size_t i = 0; i < 32; i++)
			{
				fogDensityTable[i] = (renderState.fogDensityTable[i] == 127) ? 255 : renderState.fogDensityTable[i] << 1;
			}
			
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texFogDensityTableID);
			glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RED, GL_UNSIGNED_BYTE, fogDensityTable);
		}
		
		if (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported)
		{
			const u8 alpha8 = (renderState.DISP3DCNT.EnableAntialiasing) ? 0x80 : 0xFF;
			Color4u8 edgeColor32[8];
			
			for (size_t i = 0; i < 8; i++)
			{
				edgeColor32[i].value = COLOR555TO8888(renderState.edgeMarkColorTable[i] & 0x7FFF, alpha8);
			}
			
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texEdgeColorTableID);
			glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 8, GL_RGBA, OGLRef.textureSrcTypeEdgeColor, edgeColor32);
		}
		
		// Setup render states
		this->_geometryProgramFlags.value = 0;
		this->_geometryProgramFlags.EnableWDepth = renderState.SWAP_BUFFERS.DepthMode;
		this->_geometryProgramFlags.EnableAlphaTest = renderState.DISP3DCNT.EnableAlphaTest;
		this->_geometryProgramFlags.EnableTextureSampling = (this->_enableTextureSampling) ? 1 : 0;
		this->_geometryProgramFlags.ToonShadingMode = renderState.DISP3DCNT.PolygonShading;
		this->_geometryProgramFlags.EnableFog = (this->_enableFog && this->_deviceInfo.isFogSupported) ? 1 : 0;
		this->_geometryProgramFlags.EnableEdgeMark = (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported) ? 1 : 0;
		this->_geometryProgramFlags.OpaqueDrawMode = 1;
		
		this->_SetupGeometryShaders(this->_geometryProgramFlags);
		
		if (renderNeedsToonTable)
		{
			glActiveTexture(GL_TEXTURE0 + OGLTextureUnitID_LookupTable);
			glBindTexture(GL_TEXTURE_1D, OGLRef.texToonTableID);
			
			if (OGLRef.textureSrcTypeToonTable == GL_UNSIGNED_BYTE)
			{
				ColorspaceConvertBuffer555xTo8888Opaque<false, false, BESwapDst>(renderState.toonTable16, OGLRef.toonTable32, 32);
				glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RGBA, OGLRef.textureSrcTypeToonTable, OGLRef.toonTable32);
			}
			else
			{
				glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 32, GL_RGBA, OGLRef.textureSrcTypeToonTable, renderState.toonTable16);
			}
		}
	}
	else
	{
		// Even with no polygons to draw, we always need to set these 3 flags so that
		// glDrawBuffers() can reference the correct set of FBO attachments using
		// OGLGeometryFlags.DrawBuffersMode.
		this->_geometryProgramFlags.EnableFog = (this->_enableFog && this->_deviceInfo.isFogSupported) ? 1 : 0;
		this->_geometryProgramFlags.EnableEdgeMark = (this->_enableEdgeMark && this->_deviceInfo.isEdgeMarkSupported) ? 1 : 0;
		this->_geometryProgramFlags.OpaqueDrawMode = 1;
	}
	
	this->_needsZeroDstAlphaPass = true;
	
	this->_lastBoundColorOut = this->_colorOut->BindRenderer();
	
	return OGLERROR_NOERR;
}

OpenGLRenderer_2_1::OpenGLRenderer_2_1()
{
	_feature.variantID = OpenGLVariantID_Legacy_2_1;
}

template size_t OpenGLRenderer::DrawPolygonsForIndexRange<OGLPolyDrawMode_DrawOpaquePolys>(const POLY *rawPolyList, const CPoly *clippedPolyList, const size_t clippedPolyCount, size_t firstIndex, size_t lastIndex, size_t &indexOffset, POLYGON_ATTR &lastPolyAttr);
template size_t OpenGLRenderer::DrawPolygonsForIndexRange<OGLPolyDrawMode_DrawTranslucentPolys>(const POLY *rawPolyList, const CPoly *clippedPolyList, const size_t clippedPolyCount, size_t firstIndex, size_t lastIndex, size_t &indexOffset, POLYGON_ATTR &lastPolyAttr);
template size_t OpenGLRenderer::DrawPolygonsForIndexRange<OGLPolyDrawMode_ZeroAlphaPass>(const POLY *rawPolyList, const CPoly *clippedPolyList, const size_t clippedPolyCount, size_t firstIndex, size_t lastIndex, size_t &indexOffset, POLYGON_ATTR &lastPolyAttr);
