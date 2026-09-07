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

#include "stdafx.h"
#include "AudioOutput.h"
#include "SysCTR/Diagnostics/DiagnosticsCTR.h"
#include "SysCTR/Diagnostics/DumpSupport.h"

#include <stdio.h>
#include <new>

#include <3ds.h>

#include "Config/ConfigOptions.h"
#include "Debug/DBGConsole.h"
#include "HLEAudio/AudioBuffer.h"
#include "Utility/FramerateLimiter.h"
#include "Utility/Thread.h"

extern u32 gSoundSync;

static const u32 DESIRED_OUTPUT_FREQUENCY = 44100;

static const u32 CTR_BUFFER_SIZE  = 1024 * 2;
static const u32 CTR_BUFFER_COUNT = 6;
static const u32 CTR_NUM_SAMPLES  = 512;


static ndspWaveBuf waveBuf[CTR_BUFFER_COUNT];
static unsigned int waveBuf_id;

bool audioOpen = false;


CAudioBuffer *mAudioBuffer;

static CTRDump::CallbackGate diagnosticGate;
static bool diagnosticWasPaused = false;

void CTR_BeginDiagnosticAudioPause()
{
    diagnosticGate.Pause([] { svcSleepThread(50000); });
    diagnosticWasPaused = audioOpen && ndspChnIsPaused(0);
    if (audioOpen) ndspChnSetPaused(0, true);
}

void CTR_EndDiagnosticAudioPause()
{
    if (audioOpen) ndspChnSetPaused(0, diagnosticWasPaused);
    diagnosticGate.Resume();
}

unsigned CTR_DiagnosticBufferedAudioSamples()
{
    return mAudioBuffer ? mAudioBuffer->GetNumBufferedSamples() : 0;
}

static void audioCallback(void *)
{
    if (!diagnosticGate.Enter()) return;
    // Refill every completed buffer, using a fixed duration including silence.
    for (u32 count = 0; count < CTR_BUFFER_COUNT; ++count)
    {
        if (waveBuf[waveBuf_id].status != NDSP_WBUF_DONE) break;
        mAudioBuffer->Drain(reinterpret_cast<Sample *>(waveBuf[waveBuf_id].data_pcm16), CTR_NUM_SAMPLES);
        waveBuf[waveBuf_id].nsamples = CTR_NUM_SAMPLES;
        DSP_FlushDataCache(waveBuf[waveBuf_id].data_pcm16, CTR_NUM_SAMPLES * sizeof(Sample));
        ndspChnWaveBufAdd(0, &waveBuf[waveBuf_id]);
        waveBuf_id = (waveBuf_id + 1) % CTR_BUFFER_COUNT;
    }
    diagnosticGate.Leave();
}

static void AudioExit()
{
    if (!audioOpen) return;
    ndspSetCallback(nullptr, nullptr);
    ndspChnWaveBufClear(0);
    ndspExit(); // Joins the callback thread before freeing its data.
    for (u32 i = 0; i < CTR_BUFFER_COUNT; ++i)
    {
        if (waveBuf[i].data_vaddr) linearFree((void *)waveBuf[i].data_vaddr);
        waveBuf[i] = {};
    }
    audioOpen = false;
}

static bool AudioInit()
{
    if (!mAudioBuffer || !mAudioBuffer->IsValid() || R_FAILED(ndspInit())) return false;
    audioOpen = true;
    memset(waveBuf, 0, sizeof(waveBuf));
    for (u32 i = 0; i < CTR_BUFFER_COUNT; ++i)
    {
        waveBuf[i].data_vaddr = linearAlloc(CTR_NUM_SAMPLES * sizeof(Sample));
        if (!waveBuf[i].data_vaddr) { AudioExit(); return false; }
        memset((void *)waveBuf[i].data_vaddr, 0, CTR_NUM_SAMPLES * sizeof(Sample));
        DSP_FlushDataCache(waveBuf[i].data_vaddr, CTR_NUM_SAMPLES * sizeof(Sample));
        waveBuf[i].nsamples = CTR_NUM_SAMPLES;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetRate(0, DESIRED_OUTPUT_FREQUENCY);
    waveBuf_id = 0;
    for (u32 i = 0; i < CTR_BUFFER_COUNT; ++i) ndspChnWaveBufAdd(0, &waveBuf[i]);
    ndspSetCallback(audioCallback, nullptr);
    return true;
}

AudioOutput::AudioOutput()
:	mAudioPlaying( false )
,	mFrequency( 44100 )
{
	mAudioBuffer = new CAudioBuffer( CTR_BUFFER_SIZE );
}

AudioOutput::~AudioOutput( )
{
	StopAudio();
	delete mAudioBuffer;
	mAudioBuffer = nullptr;
}

void AudioOutput::SetFrequency( u32 frequency )
{
	mFrequency = frequency;
}

void AudioOutput::AddBuffer( u8 *start, u32 length )
{
	if (length < sizeof(Sample) * 2 || !start || !mFrequency)
		return;

	if (!mAudioPlaying)
		StartAudio();

	if (!mAudioPlaying) return;
	u32 num_samples = length / sizeof( Sample );

	u32 output_freq = DESIRED_OUTPUT_FREQUENCY;

	/*if (gAudioRateMatch)
	{
		if (gSoundSync > DESIRED_OUTPUT_FREQUENCY * 2)	
			output_freq = DESIRED_OUTPUT_FREQUENCY * 2;	//limit upper rate
		else if (gSoundSync < DESIRED_OUTPUT_FREQUENCY)
			output_freq = DESIRED_OUTPUT_FREQUENCY;	//limit lower rate
		else
			output_freq = gSoundSync;
	}*/

	mAudioBuffer->AddSamples( reinterpret_cast< const Sample * >( start ), num_samples, mFrequency, output_freq );
}

void AudioOutput::StartAudio()
{
	if (mAudioPlaying)
		return;

	mAudioBuffer->Reset();
	mAudioPlaying = AudioInit();
}

void AudioOutput::StopAudio()
{
	if (!mAudioPlaying)
		return;

	mAudioBuffer->Cancel();
	mAudioPlaying = false;
	AudioExit();
}
