/*
	Copyright (C) 2006 Ben Jaques
	Copyright (C) 2008-2021 DeSmuME team

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

#include "gdbstub.h"
#include <rthreads/rthreads.h>

/*
 * Threading implementation for GDB stub using rthreads
 */

void *
createThread_gdb(void (*thread_function)(void *data), void *thread_data)
{
	sthread_t *thread = sthread_create(thread_function, thread_data);
	return (void *)thread;
}

void
joinThread_gdb(void *thread_handle)
{
	sthread_t *thread = (sthread_t *)thread_handle;
	if (thread != NULL) {
		sthread_join(thread);
		// Note: rthreads doesn't have a destroy function, memory is freed automatically
	}
}
