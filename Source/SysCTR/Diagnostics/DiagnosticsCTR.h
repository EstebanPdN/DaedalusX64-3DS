// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace CTRDiagnostics
{
uint32_t PollInput(uint32_t held);
void PollMenu(uint32_t held);
void MarkCPUStopped();
bool RunPending();
void Cancel();
}
void CTR_BeginDiagnosticAudioPause();
void CTR_EndDiagnosticAudioPause();
unsigned CTR_DiagnosticBufferedAudioSamples();
