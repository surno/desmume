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
#import <dispatch/dispatch.h>

// Minimal wrapper for Metal resources needed by the 3D renderer
// This provides only the essential properties that MetalRender requires:
// - device: The Metal device for creating resources
// - commandQueue: The command queue for GPU work
@interface MinimalMetalSharedData : NSObject
{
	id<MTLDevice> _device;
	id<MTLCommandQueue> _commandQueue;
	id<MTLLibrary> _defaultLibrary;
}

@property (readonly, nonatomic) id<MTLDevice> device;
@property (readonly, nonatomic) id<MTLCommandQueue> commandQueue;
@property (readonly, nonatomic) id<MTLLibrary> defaultLibrary;

- (instancetype)init;
- (instancetype)initWithDevice:(id<MTLDevice>)device 
                  commandQueue:(id<MTLCommandQueue>)commandQueue 
                defaultLibrary:(id<MTLLibrary>)defaultLibrary;
- (void)dealloc;

@end

@implementation MinimalMetalSharedData

@synthesize device = _device;
@synthesize commandQueue = _commandQueue;
@synthesize defaultLibrary = _defaultLibrary;

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
	
	// Create default Metal library (contains compiled shaders)
	// Try to load from app bundle first (for app bundles)
	_defaultLibrary = [_device newDefaultLibrary];
	
	// If that fails, try to load from file paths (for static library builds)
	if (_defaultLibrary == nil) {
		NSFileManager *fileManager = [NSFileManager defaultManager];
		NSMutableArray *searchPaths = [NSMutableArray array];
		
		// Try app bundle resources first
		NSBundle *mainBundle = [NSBundle mainBundle];
		if (mainBundle != nil) {
			NSString *resourcePath = [mainBundle resourcePath];
			if (resourcePath != nil) {
				[searchPaths addObject:resourcePath];
			}
		}
		
		// Try executable directory (for Rust binaries)
		NSString *executablePath = [[NSProcessInfo processInfo] arguments][0];
		if (executablePath != nil) {
			NSString *executableDir = [executablePath stringByDeletingLastPathComponent];
			if (executableDir != nil) {
				[searchPaths addObject:executableDir];
			}
		}
		
		// Try current working directory
		NSString *cwd = [fileManager currentDirectoryPath];
		if (cwd != nil) {
			[searchPaths addObject:cwd];
		}
		
		// Try to find default.metallib in search paths
		for (NSString *searchPath in searchPaths) {
			NSString *metallibPath = [searchPath stringByAppendingPathComponent:@"default.metallib"];
			if ([fileManager fileExistsAtPath:metallibPath]) {
				NSError *error = nil;
				// Use newLibraryWithURL:error: instead of deprecated newLibraryWithFile:error:
				NSURL *metallibURL = [NSURL fileURLWithPath:metallibPath];
				_defaultLibrary = [_device newLibraryWithURL:metallibURL error:&error];
				if (_defaultLibrary != nil) {
					break;
				}
			}
		}
	}
	
	if (_defaultLibrary == nil) {
		NSLog(@"Metal Bootstrap: Failed to load Metal library from any location");
		// This is not fatal, but Metal rendering will fail
	}
	
	return self;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device 
                  commandQueue:(id<MTLCommandQueue>)commandQueue 
                defaultLibrary:(id<MTLLibrary>)defaultLibrary
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	
	_device = [device retain];
	_commandQueue = [commandQueue retain];
	_defaultLibrary = [defaultLibrary retain];
	
	return self;
}

- (void)dealloc
{
	if (_commandQueue != nil) {
		[_commandQueue release];
		_commandQueue = nil;
	}
	
	if (_defaultLibrary != nil) {
		[_defaultLibrary release];
		_defaultLibrary = nil;
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

// C interface for initializing Metal resources with embedded data
extern "C" int desmume_metal_bootstrap_init_with_data(const void *data, size_t len)
{
	@autoreleasepool {
		if (g_MinimalMetalSharedData != nil) {
			// Already initialized
			return 0;
		}
		
		// Create device and command queue first
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		if (device == nil) {
			NSLog(@"Metal Bootstrap: Failed to create Metal device");
			return -1;
		}
		
		id<MTLCommandQueue> commandQueue = [device newCommandQueue];
		if (commandQueue == nil) {
			NSLog(@"Metal Bootstrap: Failed to create command queue");
			return -1;
		}
		
		// Load library from embedded data
		NSError *error = nil;
		NSData *libraryData = [NSData dataWithBytes:data length:len];
		// Convert NSData to dispatch_data_t for Metal API
		dispatch_data_t dispatchData = dispatch_data_create(
			libraryData.bytes,
			libraryData.length,
			dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0),
			NULL
		);
		id<MTLLibrary> defaultLibrary = [device newLibraryWithData:dispatchData error:&error];
		dispatch_release(dispatchData);
		
		if (defaultLibrary == nil) {
			NSLog(@"Metal Bootstrap: Failed to load library from embedded data: %@", error);
			return -1;
		}
		
		// Create shared data object
		g_MinimalMetalSharedData = [[MinimalMetalSharedData alloc] initWithDevice:device 
		                                                             commandQueue:commandQueue 
		                                                           defaultLibrary:defaultLibrary];
		if (g_MinimalMetalSharedData == nil) {
			NSLog(@"Metal Bootstrap: Failed to create shared data object");
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
