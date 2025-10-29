/*
	Copyright (C) 2011 Roger Manuel
	Copyright (C) 2012-2022 DeSmuME team

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

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#include "utilities.h"


@interface CocoaDSUtil : NSObject
{
	
}

+ (NSInteger) getIBActionSenderTag:(id)sender;
+ (BOOL) getIBActionSenderButtonStateBool:(id)sender;

+ (void) endSheet:(NSWindow *)sheet returnCode:(NSInteger)code;

+ (NSColor *) NSColorFromRGBA8888:(uint32_t)theColor;
+ (uint32_t) RGBA8888FromNSColor:(NSColor *)theColor;

+ (NSString *) filePathFromCPath:(const char *)cPath;
+ (NSURL *) fileURLFromCPath:(const char *)cPath;
+ (const char *) cPathFromFilePath:(NSString *)filePath;
+ (const char *) cPathFromFileURL:(NSURL *)fileURL;

+ (NSInteger) appVersionNumeric;
+ (NSString *) appInternalVersionString;
+ (NSString *) appInternalNameAndVersionString;
+ (NSString *) appCompilerDetailString;

+ (BOOL) determineDarkModeAppearance;

+ (NSString *) operatingSystemString;
+ (NSString *) modelIdentifierString;
+ (uint32_t) hostIP4AddressAsUInt32;

@end

@protocol DirectoryURLDragDestTextFieldProtocol <NSObject>

@required
- (void) assignDirectoryPath:(NSString *)dirPath textField:(NSTextField *)textField;

@end

// Subclass NSTextField to override NSDraggingDestination methods for assigning directory paths using drag-and-drop
@interface DirectoryURLDragDestTextField : NSTextField
{ }
@end

@interface NSNotificationCenter (MainThread)

- (void)postNotificationOnMainThread:(NSNotification *)notification;
- (void)postNotificationOnMainThreadName:(NSString *)aName object:(id)anObject;
- (void)postNotificationOnMainThreadName:(NSString *)aName object:(id)anObject userInfo:(NSDictionary *)aUserInfo;

@end

@interface RGBA8888ToNSColorValueTransformer : NSValueTransformer
{ }
@end

