/*
	Copyright (C) 2013-2025 DeSmuME team

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

#if defined(PORT_VERSION_OS_X_APP)
	#define ENABLE_ASYNC_FETCH
	#define ENABLE_DISPLAYLINK_FETCH
	#define ENABLE_SHARED_FETCH_OBJECT
#endif

#ifdef ENABLE_ASYNC_FETCH
	#include <pthread.h>
	#include <mach/task.h>
	#include <mach/semaphore.h>
	#include <mach/sync_policy.h>

	// This symbol only exists in the kernel headers, but not in the user headers.
	// Manually define the symbol here, since we will be Mach semaphores in the user-space.
	#ifndef SYNC_POLICY_PREPOST
		#define SYNC_POLICY_PREPOST 0x4
	#endif
#endif

#ifdef ENABLE_DISPLAYLINK_FETCH
	#import <CoreVideo/CoreVideo.h>
#endif

#import <Foundation/Foundation.h>
#include <map>
#include <vector>
#include "utilities.h"

#include "ClientEmulationOutput.h"
#include "ClientVideoOutput.h"

#import "cocoa_util.h"
#include "../../GPU.h"

#ifdef BOOL
#undef BOOL
#endif

#if defined(ENABLE_ASYNC_FETCH) && defined(ENABLE_DISPLAYLINK_FETCH) && !defined(METAL_DISABLE_FOR_BUILD_TARGET) && defined(MAC_OS_X_VERSION_10_11) && (MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_11)
	#define ENABLE_APPLE_METAL
#endif

#define VIDEO_FLUSH_TIME_LIMIT_OFFSET	8	// The amount of time, in seconds, to wait for a flush to occur on a given CVDisplayLink before stopping it.

enum ClientDisplayBufferState
{
	ClientDisplayBufferState_Idle			= 0,	// The buffer has already been read and is currently idle. It is a candidate for a read or write operation.
	ClientDisplayBufferState_Writing		= 1,	// The buffer is currently being written. It cannot be accessed.
	ClientDisplayBufferState_Ready			= 2,	// The buffer was just written to, but has not been read yet. It is a candidate for a read or write operation.
	ClientDisplayBufferState_PendingRead	= 3,	// The buffer has been marked that it will be read. It must not be accessed.
	ClientDisplayBufferState_Reading		= 4		// The buffer is currently being read. It cannot be accessed.
};

struct NDSFrameInfo;

#ifdef ENABLE_ASYNC_FETCH

class MacGPUFetchObjectAsync : public GPUClientFetchObject
{
protected:
	task_t _taskEmulationLoop;
	
	apple_unfairlock_t _unfairlockFramebufferStates[MAX_FRAMEBUFFER_PAGES];
	semaphore_t _semFramebuffer[MAX_FRAMEBUFFER_PAGES];
	volatile ClientDisplayBufferState _framebufferState[MAX_FRAMEBUFFER_PAGES];
	
	bool _pauseState;
	uint32_t _threadMessageID;
	uint8_t _fetchIndex;
	pthread_t _threadFetch;
	pthread_cond_t _condSignalFetch;
	pthread_mutex_t _mutexFetchExecute;
	
public:
	MacGPUFetchObjectAsync();
	~MacGPUFetchObjectAsync();
	
	virtual void Init();
	virtual void SetPauseState(bool theState);
	bool GetPauseState();
	
	void SemaphoreFramebufferCreate();
	void SemaphoreFramebufferDestroy();
	uint8_t SelectBufferIndex(const uint8_t currentIndex, size_t pageCount);
	semaphore_t SemaphoreFramebufferPageAtIndex(const u8 bufferIndex);
	ClientDisplayBufferState FramebufferStateAtIndex(uint8_t index);
	void SetFramebufferState(ClientDisplayBufferState bufferState, uint8_t index);
	
	void FetchSynchronousAtIndex(uint8_t index);
	void SignalFetchAtIndex(uint8_t index, int32_t messageID);
	void RunFetchLoop();
	virtual void DoPostFetchActions();
};

#ifdef ENABLE_DISPLAYLINK_FETCH

typedef std::map<CGDirectDisplayID, CVDisplayLinkRef> DisplayLinksActiveMap;
typedef std::map<CGDirectDisplayID, int64_t> DisplayLinkFlushTimeLimitMap;

class MacGPUFetchObjectDisplayLink : public MacGPUFetchObjectAsync, public ClientGPUFetchObjectMultiDisplayView
{
protected:
	pthread_mutex_t _mutexDisplayLinkLists;
	DisplayLinksActiveMap _displayLinksActiveList;
	DisplayLinksActiveMap _displayLinksStartedList;
	DisplayLinkFlushTimeLimitMap _displayLinkFlushTimeList;
	
public:
	MacGPUFetchObjectDisplayLink();
	~MacGPUFetchObjectDisplayLink();
	
	void DisplayLinkStartUsingID(CGDirectDisplayID displayID);
	void DisplayLinkListUpdate();
	
	virtual void FlushAllViewsOnDisplayLink(CVDisplayLinkRef displayLinkRef, const CVTimeStamp *timeStampNow, const CVTimeStamp *timeStampOutput);
	
	// MacGPUEventHandlerAsync methods
	virtual void SetPauseState(bool theState);
	virtual void DoPostFetchActions();
};

@interface MacClientSharedObject : NSObject
{
	MacGPUFetchObjectDisplayLink *GPUFetchObject;
}

@property (assign, nonatomic) MacGPUFetchObjectDisplayLink *GPUFetchObject;

@end

#endif // ENABLE_DISPLAYLINK_FETCH

#endif // ENABLE_ASYNC_FETCH

class MacGPUEventHandlerAsync : public GPUEventHandlerDefault
{
private:
	GPUClientFetchObject *_fetchObject;
	
	NDSColorFormat _colorFormatPending;
	size_t _widthPending;
	size_t _heightPending;
	size_t _pageCountPending;
	
	bool _didColorFormatChange;
	bool _didWidthChange;
	bool _didHeightChange;
	bool _didPageCountChange;
	
	pthread_mutex_t _mutexApplyGPUSettings;
	pthread_mutex_t _mutexApplyRender3DSettings;
	int _cpuCoreCountRestoreValue;
	
public:
	MacGPUEventHandlerAsync();
	~MacGPUEventHandlerAsync();
	
	GPUClientFetchObject* GetFetchObject() const;
	void SetFetchObject(GPUClientFetchObject *fetchObject);
	
	void SetFramebufferPageCount(size_t pageCount);
	size_t GetFramebufferPageCount();
	
	void SetFramebufferDimensions(size_t w, size_t h);
	void GetFramebufferDimensions(size_t &w, size_t &h);
	
	void SetColorFormat(NDSColorFormat colorFormat);
	NDSColorFormat GetColorFormat();
	
	void ApplyGPUSettingsLock();
	void ApplyGPUSettingsUnlock();
	void ApplyRender3DSettingsLock();
	void ApplyRender3DSettingsUnlock();
	
	void SetTempThreadCount(int threadCount);
	
#ifdef ENABLE_ASYNC_FETCH
	virtual void DidFrameBegin(const size_t line, const bool isFrameSkipRequested, const size_t pageCount, u8 &selectedBufferIndexInOut);
	virtual void DidFrameEnd(bool isFrameSkipped, const NDSDisplayInfo &latestDisplayInfo);
#endif
	
	virtual void DidApplyGPUSettingsBegin();
	virtual void DidApplyGPUSettingsEnd();
	virtual void DidApplyRender3DSettingsBegin();
	virtual void DidApplyRender3DSettingsEnd();
};

@interface CocoaDSGPU : NSObject
{
	UInt32 gpuStateFlags;
	uint8_t _gpuScale;
	NSUInteger openglDeviceMaxMultisamples;
	NSString *render3DMultisampleSizeString;
	BOOL isCPUCoreCountAuto;
	int _render3DThreadsRequested;
	int _render3DThreadCount;
	
	apple_unfairlock_t _unfairlockGpuState;
	MacGPUEventHandlerAsync *gpuEvent;
	
	GPUClientFetchObject *fetchObject;
}

@property (assign) UInt32 gpuStateFlags;
@property (assign) NSSize gpuDimensions;
@property (assign) NSUInteger gpuScale;
@property (assign) NSUInteger gpuColorFormat;

@property (readonly) NSUInteger openglDeviceMaxMultisamples;

@property (assign) BOOL layerMainGPU;
@property (assign) BOOL layerMainBG0;
@property (assign) BOOL layerMainBG1;
@property (assign) BOOL layerMainBG2;
@property (assign) BOOL layerMainBG3;
@property (assign) BOOL layerMainOBJ;
@property (assign) BOOL layerSubGPU;
@property (assign) BOOL layerSubBG0;
@property (assign) BOOL layerSubBG1;
@property (assign) BOOL layerSubBG2;
@property (assign) BOOL layerSubBG3;
@property (assign) BOOL layerSubOBJ;

@property (assign) NSInteger render3DRenderingEngine;
@property (readonly) NSInteger render3DRenderingEngineApplied;
@property (readonly) NSInteger render3DRenderingEngineAppliedHostRendererID;
@property (readonly) NSString *render3DRenderingEngineAppliedHostRendererName;
@property (assign) BOOL render3DHighPrecisionColorInterpolation;
@property (assign) BOOL render3DEdgeMarking;
@property (assign) BOOL render3DFog;
@property (assign) BOOL render3DTextures;
@property (assign) NSUInteger render3DThreads;
@property (assign) BOOL render3DLineHack;
@property (assign) NSUInteger render3DMultisampleSize;
@property (retain) NSString *render3DMultisampleSizeString;
@property (assign) BOOL render3DTextureDeposterize;
@property (assign) BOOL render3DTextureSmoothing;
@property (assign) NSUInteger render3DTextureScalingFactor;
@property (assign) BOOL render3DFragmentSamplingHack;
@property (assign) BOOL openGLEmulateShadowPolygon;
@property (assign) BOOL openGLEmulateSpecialZeroAlphaBlending;
@property (assign) BOOL openGLEmulateNDSDepthCalculation;
@property (assign) BOOL openGLEmulateDepthLEqualPolygonFacing;
@property (readonly, nonatomic) GPUClientFetchObject *fetchObject;

- (BOOL) gpuStateByBit:(const UInt32)stateBit;
- (void) clearWithColor:(const uint16_t)colorBGRA5551;

@end
