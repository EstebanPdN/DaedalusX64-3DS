// Copyright (C) 2026 EstebanPdN
// SPDX-License-Identifier: GPL-2.0-or-later
#include "SysCTR/Utility/TimeConversions.h"
#include "Utility/SpscRing.h"
#include "Utility/TransactionalFile.h"
#include "HLEAudio/AudioBuffer.h"
#include <cassert>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <string>

static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (false)
static std::vector<uint8_t> readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
static void timing() {
    CHECK(CTRTime::MillisecondsToNanoseconds(10) == 10000000);
    CHECK(CTRTime::MillisecondsToNanoseconds(UINT32_MAX) == 4294967295000000ULL);
    CHECK(CTRTime::TicksToNanoseconds(268123480) == 1000000000);
    CHECK(CTRTime::TicksToMilliseconds(268123480) == 1000);
    CHECK(CTRTime::TicksToMilliseconds(268123480ULL * 86400) == 86400000);
    CHECK(CTRTime::TicksToNanoseconds(0) == 0);
    // Upstream's incorrectly named microseconds conversion cancelled its tick-sleep factor.
    const uint32_t frame = 268123480 / 60;
    const double oldTickSleep = (double(frame) * 1000000 / 268123480.0) * 1000;
    CHECK(oldTickSleep - CTRTime::TicksToNanoseconds(frame) < 1.0);
}
static void ring() {
    SpscRing<int> invalid(0); int out[16] = {}, values[] = {1,2,3,4,5,6,7};
    CHECK(!invalid.IsValid()); CHECK(invalid.Write(values, 2) == 0);
    SpscRing<int> q(5);
    CHECK(q.Read(out, 8) == 0); CHECK(q.Write(values, 7) == 4);
    CHECK(q.Write(values, 1) == 0); CHECK(q.Read(out, 3) == 3);
    CHECK(out[0] == 1 && out[2] == 3); CHECK(q.Write(values+4, 3) == 3);
    CHECK(q.Read(out, 16) == 4);
    CHECK(out[0] == 4 && out[1] == 5 && out[2] == 6 && out[3] == 7);
    CHECK(q.Size() == 0); q.Reset(); CHECK(q.Size() == 0);
    SpscRing<uint32_t> concurrent(257);
    constexpr uint32_t total = 1000000;
    std::atomic<bool> failed(false);
    std::thread producer([&] {
        uint32_t next = 0, batch[113];
        while (next < total) {
            size_t n = std::min<uint32_t>(113, total-next);
            for (size_t i=0;i<n;++i) batch[i]=next+i;
            next += concurrent.Write(batch,n); std::this_thread::yield();
        }
    });
    uint32_t expected = 0, batch[71];
    while (expected < total) {
        size_t n = concurrent.Read(batch,71);
        for (size_t i=0;i<n;++i) if(batch[i] != expected++) failed=true;
        std::this_thread::yield();
    }
    producer.join(); CHECK(!failed); CHECK(concurrent.Size() == 0);
}
static void audio() {
    CAudioBuffer buffer(256);
    Sample input[100], output[256];
    for (int i=0;i<100;++i) { input[i].L=i*100; input[i].R=-i*100; }
    buffer.AddSamples(input,100,44100,44100);
    CHECK(buffer.GetNumBufferedSamples() == 99);
    CHECK(buffer.Drain(output,256) == 99);
    for (int i=0;i<99;++i) CHECK(output[i].L == input[i].L && output[i].R == input[i].R);
    for (int i=99;i<256;++i) CHECK(output[i].L == 0 && output[i].R == 0);
    buffer.AddSamples(input,100,22050,44100);
    CHECK(buffer.Drain(output,256) == 198); // Final interpolation requires a valid next sample.
    CHECK(output[1].L == 50 && output[1].R == -50);
    buffer.AddSamples(input,100,44100,22050);
    CHECK(buffer.Drain(output,256) == 49); CHECK(output[1].L == 200);
    buffer.AddSamples(input,0,44100,44100); buffer.AddSamples(input,1,44100,44100);
    buffer.AddSamples(input,100,0,44100); buffer.AddSamples(nullptr,100,44100,44100);
    buffer.AddSamples(input,100,1,UINT32_MAX);
    CHECK(buffer.GetNumBufferedSamples() == 0);
    buffer.Cancel(); buffer.AddSamples(input,100,44100,44100);
    CHECK(buffer.GetNumBufferedSamples() == 0); buffer.Reset();
    // A generated batch is larger than this ring: partial publication must progress.
    CAudioBuffer small(17);
    std::array<Sample,4096> source{};
    for (auto &s : source) { s.L=1234; s.R=-2345; }
    std::atomic<bool> done(false);
    std::thread worker([&] { small.AddSamples(source.data(),source.size(),44100,44100); done=true; });
    unsigned received=0;
    while (!done || small.GetNumBufferedSamples()) {
        unsigned n=small.Drain(output,13); received+=n;
        for (unsigned i=0;i<n;++i) CHECK(output[i].L==1234 && output[i].R==-2345);
        std::this_thread::yield();
    }
    worker.join(); CHECK(received==source.size()-1);
    CAudioBuffer stopped(2);
    std::thread blocked([&] { stopped.AddSamples(source.data(),source.size(),44100,44100); });
    std::this_thread::sleep_for(std::chrono::milliseconds(1)); stopped.Cancel(); blocked.join();
    CHECK(stopped.GetNumBufferedSamples() <= 1);
}
static bool denyOpen=false, denyWrite=false, denyFlush=false, denyClose=false;
static int moveCall=0, failMove=0; static bool failRollback=false;
static FILE *faultOpen(const char *p,const char *m) { if (denyOpen && *m=='w') {errno=EIO;return nullptr;} return fopen(p,m); }
static size_t faultWrite(const void *p,size_t s,size_t n,FILE *f) { return fwrite(p,s,denyWrite ? n/2 : n,f); }
static int faultFlush(FILE *f) { if(denyFlush) {errno=EIO;return EOF;} return fflush(f); }
static int faultClose(FILE *f) { int r=fclose(f); if(denyClose) {errno=EIO;return EOF;} return r; }
static int faultMove(const char *a,const char *b) { ++moveCall; if(moveCall==failMove || (failRollback && moveCall==3)) {errno=EIO;return -1;} return rename(a,b); }
static void saves(const std::filesystem::path &dir) {
    const std::string path=(dir/"test.sav").string();
    std::vector<uint8_t> first(4096),second(4096),twiddled(4096);
    for(size_t i=0;i<first.size();++i) { first[i]=i;second[i]=255-i;twiddled[i^3]=second[i]; }
    CHECK(SaveFile::Write(path.c_str(),first.data(),first.size()));
    CHECK(readFile(path)==first);
    CHECK(SaveFile::Write(path.c_str(),second.data(),second.size(),3));
    CHECK(readFile(path)==twiddled); CHECK(readFile(path+".bak")==first);
    SaveFile::Operations ops; ops.open=faultOpen;ops.write=faultWrite;ops.flush=faultFlush;ops.close=faultClose;ops.move=faultMove;
    for(bool *fault : {&denyOpen,&denyWrite,&denyFlush,&denyClose}) {
        *fault=true; CHECK(!SaveFile::Write(path.c_str(),first.data(),first.size(),0,ops)); *fault=false;
        CHECK(readFile(path)==twiddled);
    }
    moveCall=0; failMove=1;
    CHECK(!SaveFile::Write(path.c_str(),first.data(),first.size(),0,ops)); CHECK(readFile(path)==twiddled);
    moveCall=0;failMove=2;
    CHECK(!SaveFile::Write(path.c_str(),first.data(),first.size(),0,ops)); CHECK(readFile(path)==twiddled);
    moveCall=0;failRollback=true;
    CHECK(!SaveFile::Write(path.c_str(),first.data(),first.size(),0,ops));
    CHECK(!std::filesystem::exists(path)); CHECK(readFile(path+".bak")==twiddled);
    FILE *recovered=SaveFile::OpenForRead(path.c_str()); CHECK(recovered);
    std::vector<uint8_t> recoveredBytes(4096); CHECK(fread(recoveredBytes.data(),1,4096,recovered)==4096); fclose(recovered);
    CHECK(recoveredBytes==twiddled);
    failMove=0;failRollback=false;
    CHECK(SaveFile::Write(path.c_str(),first.data(),first.size())); CHECK(readFile(path)==first);
    CHECK(!SaveFile::Write(path.c_str(),first.data(),3));
}
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    std::filesystem::path dir=argv[1]; std::filesystem::create_directories(dir);
    timing();ring();audio();saves(dir);
    printf("PASS: %u assertions; time, FIFO/concurrency, actual CTR audio, save fault injection\n", checks);
}
