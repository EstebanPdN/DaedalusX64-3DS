// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#include "SysCTR/Diagnostics/DumpSupport.h"
#include <thread>
#include <vector>
#include <fstream>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <chrono>
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);abort();}}while(false)
static std::vector<uint8_t> Read(const std::string &p) {std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static uint32_t U32(const std::vector<uint8_t>&v,size_t p){return uint32_t(v[p])|(uint32_t(v[p+1])<<8)|(uint32_t(v[p+2])<<16)|(uint32_t(v[p+3])<<24);}
static bool failWrite=false,failFlush=false,failClose=false;
static size_t Write(const void*p,size_t s,size_t n,FILE*f){return fwrite(p,s,failWrite?n/2:n,f);}
static int Flush(FILE*f){return failFlush?EOF:fflush(f);}
static int Close(FILE*f){int r=fclose(f);return failClose?EOF:r;}
int main(int argc,char**argv)
{
    if(argc!=2)return 2;std::filesystem::create_directories(argv[1]);
    constexpr uint32_t l=1,r=2,select=4,other=8;
    for(auto order: {std::vector<uint32_t>{l,r,select},std::vector<uint32_t>{select,r,l},std::vector<uint32_t>{r,l,select}})
    {
        CTRDump::Chord chord(l|r|select);uint32_t held=0;
        for(unsigned i=0;i<3;++i){held|=order[i];CHECK(chord.Update(held)==(i==2));}
        for(unsigned i=0;i<120;++i)CHECK(!chord.Update(held));
        CHECK(chord.Filter(held|other)==other);
        CHECK(!chord.Update(l|r));CHECK(chord.Filter(l|r)==0);
        CHECK(!chord.Update(held)); // Releasing only SELECT must not retrigger.
        CHECK(!chord.Update(0));CHECK(chord.Filter(l)==l);
        CHECK(chord.Update(held|other));
    }
    CTRDump::CallbackGate gate;std::atomic<bool> quit{false},entered{false};
    unsigned samples=0;
    std::thread callback([&]{while(!quit.load()) {if(gate.Enter()){++samples;entered=true;gate.Leave();}std::this_thread::yield();}});
    while(!entered.load())std::this_thread::yield();
    for(unsigned i=0;i<100;++i)
    {
        gate.Pause([]{std::this_thread::yield();});
        unsigned snapshot=samples;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        CHECK(samples==snapshot);CHECK(!gate.Enter());
        gate.Resume();
    }
    quit=true;callback.join();
    CHECK(CTRDump::ApplicationRange(0x00100000,0x1000,true,true));
    CHECK(CTRDump::ApplicationRange(0x30000000,0x10000000,true,true));
    CHECK(!CTRDump::ApplicationRange(0x1f000000,0x1000,true,true));
    CHECK(!CTRDump::ApplicationRange(0x1efff000,0x2000,true,true));
    CHECK(!CTRDump::ApplicationRange(0x30000000,0xffffffff,true,true));
    CHECK(!CTRDump::ApplicationRange(0xfffff000,0x2000,true,true));
    CHECK(!CTRDump::ApplicationRange(0x08000000,0x1000,false,true));
    CHECK(!CTRDump::ApplicationRange(0x08000000,0x1000,true,false));
    CHECK(!CTRDump::ApplicationRange(0,0x1000,true,true));
    for(unsigned width:{320u,400u,800u})
    {
        std::vector<uint8_t> pixels(width*240*3);
        for(unsigned x=0;x<width;++x)for(unsigned y=0;y<240;++y)
        {size_t off=(x*240+239-y)*3;pixels[off]=x%256;pixels[off+1]=y;pixels[off+2]=(x+y)%256;}
        const std::string path=std::string(argv[1])+"/screen-"+std::to_string(width)+".bmp";
        CHECK(CTRDump::WriteFramebufferBmp(path.c_str(),pixels.data(),240,width));
        auto bmp=Read(path);CHECK(bmp.size()==54+pixels.size());CHECK(bmp[0]=='B'&&bmp[1]=='M');
        CHECK(U32(bmp,18)==width);CHECK(int32_t(U32(bmp,22))==-240);
        for(unsigned x=0;x<width;++x)for(unsigned y=0;y<240;++y)
        {size_t off=54+(y*width+x)*3;CHECK(bmp[off]==x%256&&bmp[off+1]==y&&bmp[off+2]==(x+y)%256);}
        CHECK(!CTRDump::WriteFramebufferBmp(path.c_str(),pixels.data(),320,240));
    }
    std::string path=std::string(argv[1])+"/memory.bin";
    std::vector<uint8_t> bytes(180000);for(size_t i=0;i<bytes.size();++i)bytes[i]=i%253;
    CHECK(CTRDump::WriteBlob(path.c_str(),bytes.data(),bytes.size()));CHECK(Read(path)==bytes);
    CTRDump::FileOperations ops;ops.write=Write;ops.flush=Flush;ops.close=Close;
    for(bool *failure:{&failWrite,&failFlush,&failClose})
    {*failure=true;CHECK(!CTRDump::WriteBlob(path.c_str(),bytes.data(),bytes.size(),nullptr,ops));*failure=false;CHECK(!std::filesystem::exists(path));}
    CHECK(!CTRDump::WriteBlob(path.c_str(),bytes.data(),bytes.size(),[](size_t){return false;}));
    CHECK(!std::filesystem::exists(path));
    CHECK(!CTRDump::WriteBlob("/nonexistent/daedalus/file.bin",bytes.data(),bytes.size()));
    printf("PASS: %u diagnostic assertions; chord, callback quiescence, ranges, BMP pixels and write failures\n",checks);
}
