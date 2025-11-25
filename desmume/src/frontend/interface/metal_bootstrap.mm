/*
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

#ifdef ENABLE_APPLE_METAL

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

// Minimal wrapper for Metal resources needed by the 3D renderer
// This provides only the essential properties that MetalRender requires:
// - device: The Metal device for creating resources
// - commandQueue: The command queue for GPU work
@interface MinimalMetalSharedData : NSObject
{
	id<MTLDevice> _device;
	id<MTLCommandQueue> _commandQueue;
}

@property (readonly, nonatomic) id<MTLDevice> device;
@property (readonly, nonatomic) id<MTLCommandQueue> commandQueue;

- (instancetype)init;
- (void)dealloc;

@end

@implementation MinimalMetalSharedData

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	
	// Create system default Metal device
	_device = MTLCreateSystemDefaultDevice();
	if (_device == nil) {
		NSLog(@"Metal Bootstrap: Failed to create Metal device");
		return nil;
	}
	
	// Create command queue
	_commandQueue = [_device newCommandQueue];
	if (_commandQueue == nil) {
		NSLog(@"Metal Bootstrap: Failed to create command queue");
		return nil;
	}
	
	NSLog(@"Metal Bootstrap: Initialized with device: %s", [[_device name] UTF8String]);
	
	return self;
}

- (void)dealloc
{
	if (_commandQueue != nil) {
		[_commandQueue release];
		_commandQueue = nil;
	}
	
	if (_device != nil) {
		[_device release];
		_device = nil;
	}
	
	[super dealloc];
}

@end

// Global pointer to hold the shared Metal data
static MinimalMetalSharedData *g_MinimalMetalSharedData = nil;

// C interface for initializing Metal resources
extern "C" int desmume_metal_bootstrap_init(void)
{
	@autoreleasepool {
		if (g_MinimalMetalSharedData != nil) {
			// Already initialized
			return 0;
		}
		
		g_MinimalMetalSharedData = [[MinimalMetalSharedData alloc] init];
		if (g_MinimalMetalSharedData == nil) {
			return -1;
		}
		
		// Inject into Metal renderer
		extern void metal_setSharedResources(void *sharedData);
		metal_setSharedResources(g_MinimalMetalSharedData);
		
		return 0;
	}
}

// C interface for cleaning up Metal resources
extern "C" void desmume_metal_bootstrap_cleanup(void)
{
	@autoreleasepool {
		if (g_MinimalMetalSharedData != nil) {
			// Clear shared resources in Metal renderer
			extern void metal_setSharedResources(void *sharedData);
			metal_setSharedResources(nil);
			
			[g_MinimalMetalSharedData release];
			g_MinimalMetalSharedData = nil;
		}
	}
}

#endif // ENABLE_APPLE_METAL
