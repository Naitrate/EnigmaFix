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
#include "PatchManager.h"
#include "../Settings/PlayerSettings.h"

// Plugins
#include "../Plugins/Plugin_DERQ.h"

// Third Party Libraries
//#include "../ThirdParty/ModUtils/MemoryMgr.h"
#include <spdlog/spdlog.h>

auto& PlayerSettingsPm = EnigmaFix::PlayerSettings::Get();
auto& PluginDERQPm     = EnigmaFix::Plugin_DERQ::Get();

// Singleton Instance
EnigmaFix::PatchManager EnigmaFix::PatchManager::pm_Instance;

namespace EnigmaFix {
    void PatchManager::InitPatch() { // Instead of running our game specific patches in InitPatch(), we will do that in RunPatches(). We just want to initialize the desired game mode and do any preliminary work here.
        //ScopedUnprotect::FullModule UnProtect(BaseModule);
        if (BaseModule = GetModuleHandleA("Application.exe")) { // We probably could do a prettier way of detecting the game (Such as looking for certain AOBs in the executable), where there's less margin for error (I.E: with executables that use the same executable name).
            spdlog::info("Detected Death end re;Quest.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::DERQ;
        }
        else if (BaseModule = GetModuleHandleA("DeathEndReQuest2.exe")) {
            spdlog::info("Detected Death end re;Quest 2.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::DERQ2;
        }
        else if (BaseModule = GetModuleHandleA("DragonStarVarnir.exe")) {
            spdlog::info("Detected Dragon Star Varnir.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::Varnir;
        }
        else if (BaseModule = GetModuleHandleA("v2r.exe")) {
            spdlog::info("Detected Neptunia VIIR.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::VIIR;
        }
        else if (BaseModule = GetModuleHandleA("NeptuniaVirtualStars.exe")) {
            spdlog::info("Detected Neptunia Virtual Stars.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::NVS;
        }
        else if (BaseModule = GetModuleHandleA("MarySkelter2.exe")) {
            spdlog::info("Detected Mary Skelter 2.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::MS2;
        }
        else if (BaseModule = GetModuleHandleA("MarySkelterFinale.exe")) {
            spdlog::info("Detected Mary Skelter Finale.");
            PlayerSettingsPm.GameMode = PlayerSettingsPm.E_GameMode::MSF;
        }
        else {
            // Throw up a window prompt indicating that no compatible game was detected, and when OK is pressed, the process is closed.
            spdlog::critical("No compatible game was detected.");
            MessageBox(NULL, "No Compatible game was detected.", "Error", MB_OK | MB_ICONERROR);
            exit(0);
        }
    }

    void PatchManager::RunPatches() {
        switch (PlayerSettingsPm.GameMode) {
            // NOTE: For now, we are going to work on the DERQ plugin, and then make it a template to work from with the others.
            case PlayerSettingsPm.E_GameMode::DERQ: {
                PluginDERQPm.ResolutionPatches(BaseModule);
                PluginDERQPm.CameraPatches(BaseModule);
                PluginDERQPm.AspectRatioPatches(BaseModule);
                PluginDERQPm.GraphicsSettingsPatches(BaseModule);
                PluginDERQPm.FOVPatches(BaseModule);
                PluginDERQPm.PhotoModePatches(BaseModule);
                PluginDERQPm.FrameratePatches(BaseModule);
                PluginDERQPm.UIPatches(BaseModule);
                PluginDERQPm.LoggingPatches(BaseModule);
                PluginDERQPm.SchedulerPatches(BaseModule);
            }
            case PlayerSettingsPm.E_GameMode::DERQ2: {
            }
            case PlayerSettingsPm.E_GameMode::Varnir: {
            }
            case PlayerSettingsPm.E_GameMode::VIIR: {
            }
            case PlayerSettingsPm.E_GameMode::NVS: {
            }
            case PlayerSettingsPm.E_GameMode::MS2: {
            }
            case PlayerSettingsPm.E_GameMode::MSF: {
            }
            default: break;
        }
    }
}