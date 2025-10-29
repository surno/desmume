/*
	Copyright (C) 2007 Guillaume Duhamel
	Copyright (C) 2007-2022 DeSmuME team

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

#ifndef _ROMREADER_H_
#define _ROMREADER_H_

#ifdef HAVE_LIBZ
#include <zlib.h>
#endif
#include <string.h>

#include "types.h"

#define ROMREADER_DEFAULT -1
#define ROMREADER_STD	0
#define ROMREADER_GZIP	1
#define ROMREADER_ZIP	2
#define ROMREADER_MEM	3

typedef struct
{
	int id;
	const char * Name;
	void * (*Init)(const char * filename);
	void (*DeInit)(void * file);
	u32 (*Size)(void * file);
	int (*Seek)(void * file, int offset, int whence);
	int (*Read)(void * file, void * buffer, u32 size);
	int (*Write)(void * file, void * buffer, u32 size);
} ROMReader_struct;

extern ROMReader_struct STDROMReader;
#ifdef HAVE_LIBZ
extern ROMReader_struct GZIPROMReader;
#endif
#ifdef HAVE_LIBZZIP
extern ROMReader_struct ZIPROMReader;
#endif

ROMReader_struct * ROMReaderInit(char ** filename);
ROMReader_struct * MemROMReaderRead_TrueInit(void* buf, int length);

#endif // _ROMREADER_H_
