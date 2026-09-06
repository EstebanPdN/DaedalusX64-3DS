/*
Copyright (C) 2003 Azimer
Copyright (C) 2001,2006 StrmnNrmn

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

//
//	N.B. This source code is derived from Azimer's Audio plugin (v0.55?)
//	and modified by StrmnNrmn to work with Daedalus PSP. Thanks Azimer!
//	Drop me a line if you get chance :)
//

#include "stdafx.h"

#include <3ds.h>
#include <atomic>

#include "AudioPluginCTR.h"
#include "AudioOutput.h"
#include "HLEAudio/audiohle.h"

#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/Interrupt.h"
#include "Core/Memory.h"
#include "Core/ROM.h"
#include "Core/RSP_HLE.h"

#define RSP_AUDIO_INTR_CYCLES     1

#define DEFAULT_FREQUENCY 44100	// Taken from Mupen64 : )

extern bool isN3DS;
// FIXME: Hack!

static std::atomic<bool> runThread(false);
static std::atomic<bool> audioBusy(false);
static Thread asyncThread = nullptr;
static Handle audioRequest = 0;
static Handle audioFinished = 0;

extern "C" void CTR_WaitForAudioTask()
{
    while (audioBusy.load(std::memory_order_acquire))
        svcWaitSynchronization(audioFinished, U64_MAX);
}

static void asyncProcess(void *)
{
    for (;;)
    {
        svcWaitSynchronization(audioRequest, U64_MAX);
        if (!runThread.load(std::memory_order_acquire)) break;
        Audio_Ucode();
        audioBusy.store(false, std::memory_order_release);
        svcSignalEvent(audioFinished);
    }
}

static void StopAsyncAudio()
{
    if (asyncThread)
    {
        CTR_WaitForAudioTask();
        runThread.store(false, std::memory_order_release);
        svcSignalEvent(audioRequest);
        threadJoin(asyncThread, U64_MAX);
        threadFree(asyncThread);
        asyncThread = nullptr;
    }
    if (audioRequest) { svcCloseHandle(audioRequest); audioRequest = 0; }
    if (audioFinished) { svcCloseHandle(audioFinished); audioFinished = 0; }
}

//*****************************************************************************
//
//*****************************************************************************
EAudioPluginMode gAudioPluginEnabled( APM_DISABLED );
//bool gAdaptFrequency( false );

//*****************************************************************************
//
//*****************************************************************************
CAudioPluginCTR::CAudioPluginCTR()
:	mAudioOutput( new AudioOutput )
{

    if (isN3DS)
    {
        if (R_SUCCEEDED(svcCreateEvent(&audioRequest, RESET_ONESHOT)) &&
            R_SUCCEEDED(svcCreateEvent(&audioFinished, RESET_STICKY)))
        {
            runThread.store(true, std::memory_order_release);
            asyncThread = threadCreate(asyncProcess, nullptr, 8 * 1024, 0x18, 2, false);
        }
        if (!asyncThread) StopAsyncAudio(); // Synchronous fallback remains available.
    }
}

//*****************************************************************************
//
//*****************************************************************************
CAudioPluginCTR::~CAudioPluginCTR()
{
	StopEmulation();

	delete mAudioOutput;
}

//*****************************************************************************
//
//*****************************************************************************
CAudioPluginCTR *	CAudioPluginCTR::Create()
{
	return new CAudioPluginCTR();
}

//*****************************************************************************
//
//*****************************************************************************
/*
void	CAudioPluginCTR::SetAdaptFrequecy( bool adapt )
{
	mAudioOutput->SetAdaptFrequency( adapt );
}
*/
//*****************************************************************************
//
//*****************************************************************************
bool		CAudioPluginCTR::StartEmulation()
{
	return true;
}

//*****************************************************************************
//
//*****************************************************************************
void	CAudioPluginCTR::StopEmulation()
{
	StopAsyncAudio();
	Audio_Reset();
	mAudioOutput->StopAudio();
}

void	CAudioPluginCTR::DacrateChanged( int SystemType )
{
//	printf( "DacrateChanged( %s )\n", (SystemType == ST_NTSC) ? "NTSC" : "PAL" );
	u32 type = (u32)((SystemType == ST_NTSC) ? VI_NTSC_CLOCK : VI_PAL_CLOCK);
	u32 dacrate = Memory_AI_GetRegister(AI_DACRATE_REG) & 0x3FFF;
	u32	frequency = type / (dacrate + 1);

	mAudioOutput->SetFrequency( frequency );
}


//*****************************************************************************
//
//*****************************************************************************
void	CAudioPluginCTR::LenChanged()
{
	if( gAudioPluginEnabled > APM_DISABLED )
	{
		//mAudioOutput->SetAdaptFrequency( gAdaptFrequency );

		u32		address( Memory_AI_GetRegister(AI_DRAM_ADDR_REG) & 0xFFFFFF );
		u32		length(Memory_AI_GetRegister(AI_LEN_REG));

		if (address <= gRamSize && length <= gRamSize - address)
			mAudioOutput->AddBuffer(g_pu8RamBase + address, length);
	}
	else
	{
		mAudioOutput->StopAudio();
	}
}

//*****************************************************************************
//
//*****************************************************************************
u32		CAudioPluginCTR::ReadLength()
{
	return 0;
}

//*****************************************************************************
//
//*****************************************************************************
EProcessResult	CAudioPluginCTR::ProcessAList()
{
	Memory_SP_SetRegisterBits(SP_STATUS_REG, SP_STATUS_HALT);

	EProcessResult	result( PR_NOT_STARTED );

	switch( gAudioPluginEnabled )
	{
		case APM_DISABLED:
			result = PR_COMPLETED;
			break;
		case APM_ENABLED_ASYNC:
			if(asyncThread) {
				CTR_WaitForAudioTask();
				svcClearEvent(audioFinished);
				audioBusy.store(true, std::memory_order_release);
				svcSignalEvent(audioRequest);

				CPU_AddEvent(RSP_AUDIO_INTR_CYCLES, CPU_EVENT_AUDIO);
				result = PR_STARTED;
			}
			else
			{
				Audio_Ucode();
				result = PR_COMPLETED;
			}
			break;
		case APM_ENABLED_SYNC:
			Audio_Ucode();
			result = PR_COMPLETED;
			break;
	}

	return result;
}

//*****************************************************************************
//
//*****************************************************************************
CAudioPlugin *		CreateAudioPlugin()
{
	return CAudioPluginCTR::Create();
}
