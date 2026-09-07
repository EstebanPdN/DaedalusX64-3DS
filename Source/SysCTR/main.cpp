// Nintendo 3DS platform entry point: MasterFeizz (2020-2022).
// Stability and packaging changes: EstebanPdN (2026), GPL-2.0-or-later.
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <3ds.h>
#include <GL/picaGL.h>
#include "BuildOptions.h"
#include "Config/ConfigOptions.h"
#include "Core/CPU.h"
#include "Core/Save.h"
#include "System/Paths.h"
#include "System/System.h"
#include "Utility/IO.h"
#include "Utility/MemoryCTR.h"
#include "UI/UserInterface.h"
#include "UI/RomSelector.h"
#include "SysCTR/Diagnostics/DiagnosticsCTR.h"

bool isN3DS = false;
bool shouldQuit = false;
EAudioPluginMode enable_audio = APM_ENABLED_ASYNC;
static bool graphicsReady = false, picaReady = false, uiReady = false, romfsReady = false;
static bool callbackRegistered = false;

#ifdef DAEDALUS_LOG
void log2file(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    FILE *file = fopen(DAEDALUS_CTR_PATH("runtime.log"), "a");
    if (file) { vfprintf(file, format, args); fputc('\n', file); fclose(file); }
    va_end(args);
}
#endif

static void StartupError(const char *message)
{
    if (!graphicsReady) { gfxInitDefault(); graphicsReady = true; }
    consoleInit(GFX_BOTTOM, nullptr);
    printf("DaedalusX64 0.2 - development build\n\n%s\n\nPress START to exit.\n", message);
    while (aptMainLoop())
    {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
}

void HandleEndOfFrame()
{
    if (!aptMainLoop()) { shouldQuit = true; CPU_Halt("Application exit requested"); }
}
static void PollApplication(void *)
{
    HandleEndOfFrame();
    if (!shouldQuit) { hidScanInput(); CTRDiagnostics::PollInput(hidKeysHeld()); }
}

static bool Initialize()
{
    FILE *firmware = fopen("sdmc:/3ds/dspfirm.cdc", "rb");
    if (!firmware) { StartupError("DSP firmware not found:\nsdmc:/3ds/dspfirm.cdc"); return false; }
    fclose(firmware);
    if (!_InitializeSvcHack()) { StartupError("JIT service access is unavailable.\nCheck your homebrew/CFW setup."); return false; }
    if (R_FAILED(romfsInit())) { StartupError("Cannot open packaged RomFS resources."); return false; }
    romfsReady = true;
    APT_CheckNew3DS(&isN3DS);
    osSetSpeedupEnable(true);
    gfxInit(GSP_BGR8_OES, GSP_BGR8_OES, true);
    graphicsReady = true;
    if (isN3DS) gfxSetWide(true);
    pglInitEx(0x080000, 0x040000);
    picaReady = true;
    strcpy(gDaedalusExePath, DAEDALUS_CTR_PATH(""));
    strcpy(g_DaedalusConfig.mSaveDir, DAEDALUS_CTR_PATH("SaveGames/"));
    const bool directoriesReady = IO::Directory::EnsureExists(DAEDALUS_CTR_PATH("")) &&
        IO::Directory::EnsureExists(DAEDALUS_CTR_PATH("Roms/")) &&
        IO::Directory::EnsureExists(DAEDALUS_CTR_PATH("SaveGames/")) &&
        IO::Directory::EnsureExists(DAEDALUS_CTR_PATH("SaveStates/"));
    UI::Initialize();
    uiReady = true;
    if (!directoriesReady) { UI::ShowMessage("SD card error", "Cannot create application folders.\nCheck available space and write access."); return false; }
    if (!System_Init()) { UI::ShowMessage("Initialization failed", "Unable to initialize emulator resources."); return false; }
    CPU_RegisterVblCallback(PollApplication, nullptr);
    callbackRegistered = true;
    return true;
}

static void Shutdown()
{
    if (callbackRegistered) { CPU_UnregisterVblCallback(PollApplication, nullptr); callbackRegistered = false; }
    // GPU submissions must finish before graphics-owned memory is released.
    if (picaReady) glFinish();
    System_Finalize();
    if (uiReady) { UI::Shutdown(); uiReady = false; }
    if (picaReady) { pglExit(); picaReady = false; }
    if (graphicsReady) { gfxExit(); graphicsReady = false; }
    if (romfsReady) { romfsExit(); romfsReady = false; }
}

// Also protect the libctru GSP thread when an early process exit bypasses main.
extern "C" void userAppExit()
{
    if (graphicsReady) { gfxExit(); graphicsReady = false; }
}

int main(int argc, char *argv[])
{
    if (!Initialize()) { Shutdown(); return 1; }
    bool directLaunch = argc > 1 && argv[1] && *argv[1];
    while (!shouldQuit)
    {
        std::string path;
        if (directLaunch) { path = argv[1]; directLaunch = false; }
        else
        {
            const std::string selected = UI::DrawRomSelector();
            if (selected.empty()) break;
            path = std::string(DAEDALUS_CTR_PATH("Roms/")) + selected;
        }
        if (shouldQuit) break;
        if (!System_Open(path.c_str()))
        {
            UI::ShowMessage("ROM could not be opened", "Check the ROM and available memory.\nNo emulation was started.");
            continue;
        }
        do
        {
            CPU_Run();
            if (shouldQuit || !CTRDiagnostics::RunPending()) break;
        } while (!shouldQuit);
        CTRDiagnostics::Cancel();
        if (picaReady) glFinish();
        System_Close();
    }
    Shutdown();
    return 0;
}
