// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
// Diagnostic layout informed by the maintainer's MK64 and Minish Cap ports.
#include "stdafx.h"
#include "DiagnosticsCTR.h"
#include "DumpSupport.h"
#include <3ds.h>
#include <GL/picaGL.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <malloc.h>
#include "Core/CPU.h"
#include "Core/ROM.h"
#include "Core/Memory.h"
#include "Config/ConfigOptions.h"
#include "Utility/IO.h"
#include "Utility/FramerateLimiter.h"

extern bool isN3DS;
extern bool shouldQuit;
extern "C" void CTR_WaitForAudioTask();
namespace
{
CTRDump::Chord chord(KEY_L | KEY_R | KEY_SELECT);
bool requested = false, stopped = false;
size_t writtenBytes = 0;
u64 lastProgress = 0;

// Small direct-framebuffer status display: does not alter the emulator's GL state.
const uint8_t *Glyph(char c)
{
    static const char letters[] = "ADEFGILMNPRSTUVW";
    static const uint8_t glyphs[][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31},
        {31,16,16,30,16,16,16}, {14,17,16,23,17,17,14}, {31,4,4,4,4,4,31},
        {16,16,16,16,16,16,31}, {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17},
        {30,17,17,30,16,16,16}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30},
        {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}
    };
    const char *pos=strchr(letters,c);
    return pos ? glyphs[pos-letters] : nullptr;
}
void Status(const char *text)
{
    for(unsigned pass=0;pass<2;++pass)
    {
        u16 pw=0,ph=0;
        u8 *fb=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&pw,&ph);
        if(!fb || pw!=240 || ph!=320 || gfxGetScreenFormat(GFX_BOTTOM)!=GSP_BGR8_OES) return;
        auto pixel=[&](unsigned x,unsigned y,unsigned shade)
        {
            if(x>=ph || y>=pw) return;
            u8 *p=fb+((x*pw)+(pw-1-y))*3;
            p[0]=p[1]=p[2]=shade;
        };
        for(unsigned y=94;y<126;++y) for(unsigned x=12;x<308;++x) pixel(x,y,16);
        unsigned x=24;
        for(const char *c=text;*c && x<295;++c,x+=12)
        {
            const uint8_t *g=Glyph(*c); if(!g) continue;
            for(unsigned y=0;y<7;++y) for(unsigned b=0;b<5;++b)
                if(g[y]&(1<<(4-b))) for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx)
                    pixel(x+b*2+dx,102+y*2+dy,240);
        }
        if (text[0]=='W')
        {
            const unsigned start=24+(writtenBytes/(64*1024))%240;
            for(unsigned y=121;y<124;++y) for(unsigned x=start;x<start+16;++x) pixel(x,y,180);
        }
        GSPGPU_FlushDataCache(fb,size_t(pw)*ph*3);
        gfxScreenSwapBuffers(GFX_BOTTOM,false);
        gspWaitForVBlank();
    }
}
bool Progress(size_t bytes)
{
    writtenBytes+=bytes;
    const u64 now=osGetTime();
    if(now-lastProgress>1000)
    {
        if(!aptMainLoop()) { shouldQuit=true; return false; }
        glFinish(); // Drain any platform resume work before continuing the snapshot.
        Status("WRITING DUMP"); lastProgress=now;
    }
    return !shouldQuit;
}
bool SessionDirectory(char *path,size_t size)
{
    if(!IO::Directory::EnsureExists(DAEDALUS_CTR_PATH("Dumps/"))) return false;
    char stamp[48];
    time_t now=time(nullptr); struct tm *t=localtime(&now);
    if(!t || !strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",t))
        snprintf(stamp,sizeof(stamp),"tick-%llu",static_cast<unsigned long long>(svcGetSystemTick()));
    for(unsigned sequence=0;sequence<10000;++sequence)
    {
        int n=snprintf(path,size,DAEDALUS_CTR_PATH("Dumps/dump-%s-%04u"),stamp,sequence);
        if(n<0 || size_t(n)>=size) return false;
        if(mkdir(path,0777)==0) return true;
        if(errno!=EEXIST) return false;
    }
    return false;
}
bool Owned(const MemInfo &info)
{
    const bool owned=info.state==MEMSTATE_CODE || info.state==MEMSTATE_PRIVATE || info.state==MEMSTATE_CONTINUOUS;
    return CTRDump::ApplicationRange(info.base_addr,info.size,(info.perm&MEMPERM_READ)!=0,owned);
}
bool ReadableSpan(const void *data, size_t size)
{
    uint64_t address=reinterpret_cast<uintptr_t>(data);
    const uint64_t end=address+size;
    if(!size || end>0x40000000 || end<=address) return false;
    for(unsigned regions=0;address<end && regions<64;++regions)
    {
        MemInfo info={};PageInfo page={};
        if(R_FAILED(svcQueryMemory(&info,&page,uint32_t(address))) || !Owned(info) || info.base_addr>address) return false;
        const uint64_t next=uint64_t(info.base_addr)+info.size;
        if(next<=address) return false;
        address=next;
    }
    return address>=end;
}
bool Screens(const char *directory)
{
    glFinish();
    u16 tw=0,th=0,bw=0,bh=0;
    // Remember the transfer destinations BEFORE picaGL swaps the front/back pointers.
    u8 *top=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,&tw,&th);
    u8 *bottom=gfxGetFramebuffer(GFX_BOTTOM,GFX_LEFT,&bw,&bh);
    pglSwapBuffers(); glFinish(); gspWaitForVBlank();
    char path[384]; bool ok=true;
    if(gfxGetScreenFormat(GFX_TOP)==GSP_BGR8_OES && top && tw==240 && (th==400 || th==800))
    {
        GSPGPU_InvalidateDataCache(top,size_t(tw)*th*3);
        snprintf(path,sizeof(path),"%s/top.bmp",directory);
        ok=CTRDump::WriteFramebufferBmp(path,top,tw,th) && ok;
    }
    else ok=false;
    if(gfxGetScreenFormat(GFX_BOTTOM)==GSP_BGR8_OES && bottom && bw==240 && bh==320)
    {
        GSPGPU_InvalidateDataCache(bottom,size_t(bw)*bh*3);
        snprintf(path,sizeof(path),"%s/bottom.bmp",directory);
        ok=CTRDump::WriteFramebufferBmp(path,bottom,bw,bh) && ok;
    }
    else ok=false;
    return ok;
}
bool CPUState(const char *directory)
{
    char path[384];snprintf(path,sizeof(path),"%s/cpu.txt",directory);
    FILE *f=fopen(path,"wb");if(!f) return false;
    fprintf(f,"Guest CPU snapshot after execution core returned; hexadecimal register bit patterns.\n");
    fprintf(f,"PC=%08lX target=%08lX delay=%lu jobs=%08lX\n",
        (unsigned long)gCPUState.CurrentPC,(unsigned long)gCPUState.TargetPC,
        (unsigned long)gCPUState.Delay,(unsigned long)gCPUState.StuffToDo);
    fprintf(f,"HI=%016llX LO=%016llX\n",(unsigned long long)gCPUState.MultHi._u64,(unsigned long long)gCPUState.MultLo._u64);
    for(unsigned i=0;i<32;++i)
        fprintf(f,"r%02u=%016llX cp0[%02u]=%08lX fpu[%02u]=%08lX fcr[%02u]=%08lX\n",
            i,(unsigned long long)gCPUState.CPU[i]._u64,i,(unsigned long)gCPUState.CPUControl[i]._u32,
            i,(unsigned long)gCPUState.FPU[i]._u32,i,(unsigned long)gCPUState.FPUControl[i]._u32);
    fprintf(f,"events=%lu\n",(unsigned long)gCPUState.NumEvents);
    for(unsigned i=0;i<std::min<unsigned>(gCPUState.NumEvents,MAX_CPU_EVENTS);++i)
        fprintf(f,"event[%u] count=%ld type=%u\n",i,(long)gCPUState.Events[i].mCount,(unsigned)gCPUState.Events[i].mEventType);
    return CTRDump::CloseChecked(f);
}
bool GuestMemory(const char *directory)
{
    const char *names[NUM_MEM_BUFFERS]={"unused","rdram","rsp-dmem-imem","pif","rdram-regs","sp-regs","sp-pc-regs","dpc-regs","mi-regs","vi-regs","ai-regs","pi-regs","ri-regs","si-regs","save","mempak"};
    char path[384];snprintf(path,sizeof(path),"%s/guest-memory.txt",directory);
    FILE *manifest=fopen(path,"wb");if(!manifest) return false;
    fputs("Guest memory in emulator-native layout, not a save state.\nN64 byte address within RDRAM uses offset XOR 3 on this little-endian build.\n",manifest);
    bool ok=true;
    for(unsigned i=1;i<NUM_MEM_BUFFERS;++i)
    {
        const size_t bytes=i==MEM_RD_RAM ? gRamSize : MemoryRegionSizes[i];
        if(!g_pMemoryBuffers[i] || !bytes ||
           (i==MEM_RD_RAM && bytes>MemoryRegionSizes[i]) || !ReadableSpan(g_pMemoryBuffers[i],bytes)) { fprintf(manifest,"%s: unavailable\n",names[i]);ok=false;continue; }
        snprintf(path,sizeof(path),"%s/%s.bin",directory,names[i]);
        bool saved=CTRDump::WriteBlob(path,g_pMemoryBuffers[i],bytes,Progress);
        fprintf(manifest,"%s.bin address=%08lX bytes=%lu %s\n",names[i],(unsigned long)g_pMemoryBuffers[i],(unsigned long)bytes,saved?"saved":"FAILED");
        if(!saved) {ok=false;break;}
    }
    return CTRDump::CloseChecked(manifest) && ok;
}
bool ProcessMemory(const char *directory)
{
    char path[384];snprintf(path,sizeof(path),"%s/process-memory",directory);
    if(mkdir(path,0777)) return false;
    snprintf(path,sizeof(path),"%s/process-memory/manifest.txt",directory);
    FILE *manifest=fopen(path,"wb");if(!manifest) return false;
    fputs("Sequential readable application mappings. Guest execution stopped; audio worker idle; refill callback gated; GPU queue drained.\n"
          "Diagnostics, stdio, active stacks and platform service threads can change during capture. Not an atomic host snapshot.\n"
          "Excluded: MMIO, VRAM, shared/service memory, aliases, inaccessible mappings and kernel memory.\n",manifest);
    uint32_t address=0;bool ok=true;
    for(unsigned region=0;region<1024 && address<0x40000000;++region)
    {
        MemInfo info={};PageInfo page={};
        if(R_FAILED(svcQueryMemory(&info,&page,address)) || !info.size)
        {fprintf(manifest,"FAILED query at %08lX\n",(unsigned long)address);ok=false;break;}
        uint64_t next=uint64_t(info.base_addr)+info.size;
        if(next<=address) {ok=false;break;}
        const bool include=Owned(info);bool saved=false;
        if(include)
        {
            snprintf(path,sizeof(path),"%s/process-memory/%08lX.bin",directory,(unsigned long)info.base_addr);
            saved=CTRDump::WriteBlob(path,reinterpret_cast<const void *>(info.base_addr),info.size,Progress);
        }
        fprintf(manifest,"%08lX size=%lu state=%lu permissions=%lu %s\n",(unsigned long)info.base_addr,
            (unsigned long)info.size,(unsigned long)info.state,(unsigned long)info.perm,
            !include?"excluded":saved?"saved":"FAILED");
        if(include&&!saved) {ok=false;break;}
        if(next>=0x40000000) {address=0x40000000;break;}
        address=uint32_t(next);
    }
    if(address<0x40000000) ok=false;
    return CTRDump::CloseChecked(manifest) && ok;
}
void Capture(bool game)
{
    // Never capture inside an input callback, JIT fragment or graphics display list.
    CTR_WaitForAudioTask();
    CTR_BeginDiagnosticAudioPause();
    char directory[320]={};
    if(!SessionDirectory(directory,sizeof(directory)))
    {
        Status("DUMP FAILED");
        for(unsigned frame=0;frame<30;++frame) gspWaitForVBlank();
        CTR_EndDiagnosticAudioPause();return;
    }
    writtenBytes=0;lastProgress=osGetTime();
    const u64 started=osGetTime();
    const bool screens=Screens(directory);
    Status("WRITING DUMP");
    char path[384];snprintf(path,sizeof(path),"%s/info.txt",directory);
    FILE *info=fopen(path,"wb");bool metadata=info!=nullptr;
    if(info)
    {
        struct mallinfo heap=mallinfo();
        fprintf(info,"DaedalusX64 0.2 diagnostic dump\ndump_format=1\nsource_revision=%s\ntrigger=L+R+SELECT\n",DAEDALUS_BUILD_REVISION);
        fprintf(info,"model=%s\ncontext=%s\nram_bytes=%lu\nheap_used=%lu\nheap_free=%lu\nlinear_free=%lu\n",
            isN3DS?"New 3DS family":"Old 3DS family",game?"game paused outside execution core":"ROM selector",
            (unsigned long)gRamSize,(unsigned long)heap.uordblks,(unsigned long)heap.fordblks,(unsigned long)linearSpaceFree());
        fprintf(info,"audio_mode=%u\naudio_buffered_samples=%u\ndynarec=%u\nloop_optimization=%u\ndoubles_optimization=%u\n",
            unsigned(gAudioPluginEnabled),CTR_DiagnosticBufferedAudioSamples(),unsigned(gDynarecEnabled),unsigned(gDynarecLoopOptimisation),unsigned(gDynarecDoublesOptimisation));
        fprintf(info,"speed_sync_enabled=%lu\n",(unsigned long)gSpeedSyncEnabled);
        if(game)
            fprintf(info,"rom_file=%s\nrom_name=%s\nrom_header_crc1=%08lX\nrom_header_crc2=%08lX\nrom_country=%02X\nvi_count=%lu\n",
                g_ROM.mFileName,g_ROM.settings.GameName.c_str(),(unsigned long)g_ROM.mRomID.CRC[0],
                (unsigned long)g_ROM.mRomID.CRC[1],unsigned(g_ROM.mRomID.CountryID),(unsigned long)CPU_GetVideoInterruptEventCount());
        fputs("ROM header CRCs identify the header; they are not a full-file integrity hash.\n",info);
        metadata=CTRDump::CloseChecked(info);
    }
    const bool cpu=!game || CPUState(directory);
    const bool guest=!game || GuestMemory(directory);
    const bool process=!shouldQuit && metadata && guest && ProcessMemory(directory);
    bool complete=screens && metadata && cpu && guest && process;
    snprintf(path,sizeof(path),"%s/status.txt",directory);
    FILE *status=fopen(path,"wb");
    if(status)
    {
        fprintf(status,"status=%s\nscreens=%s\nmetadata=%s\ncpu=%s\nguest_memory=%s\nprocess_memory=%s\nbytes_written=%llu\nelapsed_ms=%llu\n",
            complete?"COMPLETE":"PARTIAL",screens?"ok":"FAILED",metadata?"ok":"FAILED",!game?"not_requested":cpu?"ok":"FAILED",
            !game?"not_requested":guest?"ok":"FAILED",process?"ok":"FAILED",(unsigned long long)writtenBytes,(unsigned long long)(osGetTime()-started));
        if(!CTRDump::CloseChecked(status)) { complete=false; remove(path); }
    }
    else complete=false;
    if(!shouldQuit)
    {
        Status(complete?"DUMP SAVED":"DUMP FAILED");
        for(unsigned frame=0;frame<30;++frame) gspWaitForVBlank();
    }
    // Exclude a long SD write from the limiter's elapsed-time history.
    if(game) FramerateLimiter_Reset();
    CTR_EndDiagnosticAudioPause();
}
}
namespace CTRDiagnostics
{
uint32_t PollInput(uint32_t held)
{
    if(chord.Update(held) && !requested && CPU_IsRunning())
    {
        requested=true;
        gCPUState.AddJob(CPU_DIAGNOSTIC_DUMP);
    }
    return chord.Filter(held);
}
void PollMenu(uint32_t held)
{
    if(chord.Update(held) && !CPU_IsRunning()) Capture(false);
}
void MarkCPUStopped() { stopped=true; }
bool RunPending()
{
    if(!requested || !stopped || shouldQuit) return false;
    requested=false;stopped=false;
    Capture(true);
    return true;
}
void Cancel()
{
    requested=false;stopped=false;
    gCPUState.ClearJob(CPU_DIAGNOSTIC_DUMP);
}
}
