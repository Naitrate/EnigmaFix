/**
EnigmaFix Copyright (c) 2026 Bryce Q.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
**/

// Internal Functionality
#include "HookManager.h"
#include "ConfigManager.h"
#include "LogManager.h"
#include "PatchManager.h"
#include "RenderManager.h"
#include "FramerateManager.h"
// System Libraries
#include <windows.h>
#include <process.h>
// Third Party Libraries
#include <spdlog/spdlog.h>

// Variables
HANDLE PatchThread; // Creates a handle to the patch thread, so it can be closed easier.
HANDLE RenderThread; // Creates a handle to the D3D11 hooking thread, so it can be closed easier.
HANDLE FRManagerThread; // Creates a handle to the framerate manager thread.
auto& ConMan = EnigmaFix::ConfigManager::Get();
auto& LogMan = EnigmaFix::LogManager::Get();
auto& PatchMgr = EnigmaFix::PatchManager::Get();
auto& RenderMgr = EnigmaFix::RenderManager::Get();
auto& framerateMgr = EnigmaFix::FramerateManager::Get();
// Singleton Instance
EnigmaFix::HookManager EnigmaFix::HookManager::hm_Instance; // Seemingly need this declared in HookManager.cpp so a bunch of linker errors don't happen.

namespace EnigmaFix {
    // Simple Static Functions for calling our Managers easily in a thread.
    void HookManager::pmThread()  { PatchMgr.RunPatches(); }
    void HookManager::rmThread()  { RenderMgr.InitD3D11Hook(nullptr); } // Let's see if nullptr can fix the issue with lpReserved not being passed through.
    void HookManager::frmThread() { framerateMgr.Init(); framerateMgr.Update(); }

    void HookManager::BeginHook() {
        spdlog::info("Reading Config File...");
        ConMan.ReadConfig();
        LogMan.Init();
        spdlog::info("Initializing Patching (Checking what game is being ran)...");
        PatchMgr.InitPatch(); // Run InitPatch() first before we do RunPatches() in it's own thread. Reason being that we want to check for the game mode before we have stuff in their own specific threads checking for it.
        spdlog::info("Creating PatchManager Thread...");
        PatchThread     = (HANDLE)_beginthreadex(nullptr, 0, (unsigned(__stdcall*)(void*))pmThread,  nullptr, 0, 0); // Calls InitPatch() in a new thread on start.
        spdlog::info("Creating RenderManager Thread...");
        RenderThread    = (HANDLE)_beginthreadex(nullptr, 0, (unsigned(__stdcall*)(void*))rmThread,  nullptr, 0, 0);
        spdlog::info("Creating FramerateManager Thread...");
        FRManagerThread = (HANDLE)_beginthreadex(nullptr, 0, (unsigned(__stdcall*)(void*))frmThread, nullptr, 0, 0);
    }

    void HookManager::EndHook() {
        //Add Kiero Shutdown here.
        spdlog::info("Closing Patch Manager Thread...");
        CloseHandle(PatchThread);
        spdlog::info("Closing RenderManager Thread...");
        CloseHandle(RenderThread);
        spdlog::info("Closing FramerateManager Thread...");
        CloseHandle(FRManagerThread);
    }
}

