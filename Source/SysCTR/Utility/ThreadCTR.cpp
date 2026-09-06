/*
Copyright (C) 2005 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"
#include "Utility/Thread.h"
#include "Utility/Timing.h"

#include <3ds.h>
#include <new>
#include "TimeConversions.h"

static const int	gThreadPriorities[ TP_NUM_PRIORITIES ] =
{
	0x19,		// TP_LOW
	0x18,		// TP_NORMAL
	0x17,		// TP_HIGH
	0x16,		// TP_TIME_CRITICAL
};

const ThreadHandle	kInvalidThreadHandle = -1;

struct SDaedThreadDetails
{
	SDaedThreadDetails( DaedThread function, void * argument )
		:	ThreadFunction( function )
		,	Argument( argument )
	{
	}

	DaedThread		ThreadFunction;
	void *			Argument;
};

// The real thread is passed in as an argument. We call it and return the result
static void StartThreadFunc(  void *argp )
{
	SDaedThreadDetails * thread_details( static_cast< SDaedThreadDetails * >( argp ) );
	const SDaedThreadDetails details = *thread_details;
	delete thread_details;
	details.ThreadFunction(details.Argument);
}

ThreadHandle CreateThread( const char * name, DaedThread function, void * argument )
{
	SDaedThreadDetails *thread_details = new (std::nothrow) SDaedThreadDetails(function, argument);
	if (!thread_details) return kInvalidThreadHandle;

	Thread thid = threadCreate(StartThreadFunc, thread_details, 0x10000, gThreadPriorities[TP_NORMAL], -2, false);

	if (!thid) delete thread_details;
	return thid ? (ThreadHandle)thid : kInvalidThreadHandle;
}

void SetThreadPriority( ThreadHandle handle, EThreadPriority pri )
{
	// Nothing to do
}

void ReleaseThreadHandle( ThreadHandle handle )
{
	threadFree((Thread)handle);
}

// Wait the specified time for the thread to finish.
// Returns false if the thread didn't terminate
bool JoinThread( ThreadHandle handle, s32 timeout )
{
	Result ret = threadJoin((Thread)handle, timeout < 0 ? U64_MAX : CTRTime::MillisecondsToNanoseconds(timeout));

	return (ret >= 0);
}

void ThreadSleepMs( u32 ms )
{
	svcSleepThread(CTRTime::MillisecondsToNanoseconds(ms));
}

void ThreadSleepTicks( u32 ticks )
{
	svcSleepThread(CTRTime::TicksToNanoseconds(ticks));
}

void ThreadYield()
{
	svcSleepThread( 1 );				// Is 0 valid?
}
