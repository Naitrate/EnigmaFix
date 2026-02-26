/**
EnigmaFix Copyright (c) 2025 Bryce Q.

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
#include "Plugin_DERQ.h"
#include "../Settings/PlayerSettings.h"
#include "../Utilities/MemoryHelper.h"
#include "../Managers/PatchManager.h"

// Third Party Libraries
#include <codecvt>
#include <safetyhook.hpp>

#include "../Managers/FramerateManager.h"
#include "../Managers/LogManager.h"
#include "spdlog/spdlog.h"

auto& PlayerSettingsPDQ = EnigmaFix::PlayerSettings::Get();
auto& PatchManagerPDQ = EnigmaFix::PatchManager::Get();
auto& LogManagerPDQ = EnigmaFix::LogManager::Get();
auto& FramerateManagerPDQ = EnigmaFix::FramerateManager::Get();

EnigmaFix::Plugin_DERQ EnigmaFix::Plugin_DERQ::pq_Instance; // Seemingly need this declared in Plugin_DERQ.cpp so a bunch of linker errors don't happen.

namespace EnigmaFix
{

    struct ResolutionPtr {
        int* X;
        int* Y;

        // Constructor that takes offsets and calculates addresses dynamically
        ResolutionPtr(intptr_t xOffset, intptr_t yOffset) {
            intptr_t baseModule = reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule);
            X = reinterpret_cast<int*>(baseModule + xOffset);
            Y = reinterpret_cast<int*>(baseModule + yOffset);
        }
    };

    ResolutionPtr resolutionList[] = {
        { 0xF58720, 0xF58724 },  // 640x360 (0)
        { 0xF58728, 0xF5872C },  // 720x405 (1)
        { 0xF58730, 0xF58734 },  // 800x450 (2)
        { 0xF58738, 0xF5873C },  // 1024x576 (3)
        { 0xF58740, 0xF58744 },  // 1152x648 (4)
        { 0xF58748, 0xF5874C },  // 1280x720 (5)
        { 0xF58750, 0xF58754 },  // 1360x765 (6)
        { 0xF58758, 0xF5875C },  // 1366x768 (7)
        { 0xF58760, 0xF58764 },  // 1600x900 (8)
        { 0xF58768, 0xF5876C },  // 1920x1080 (9)
        { 0xF58770, 0xF58774 },  // 2560x1440 (10)
        { 0xF58778, 0xF5877C },  // 3840x2160 (11)
        //{ 0xF58780, 0xF58784 },        // 4K Native (12) (This one won't be used because we are overriding that with our custom resolution).
    };

    void NOPPattern(HMODULE baseModule, const std::string& pattern, size_t nopsCount, const std::string& patternName)
    {
        if (auto patternAddr = Memory::PatternScan(baseModule, pattern.c_str())) {
            spdlog::info("{} found at: {}", patternName, reinterpret_cast<void*>(patternAddr));

            // NOP out the specified number of bytes (replace with "90")
            for (size_t i = 0; i < nopsCount; ++i) {
                Memory::Write(reinterpret_cast<uintptr_t>(patternAddr + i), static_cast<uint8_t>(0x90));  // Write NOP byte
            }
            spdlog::info("Patched {} bytes of {} with NOPs.", nopsCount, patternName);
        }
    }

    using ResCheckFunctionType = void(*)(char*, int, int);
    ResCheckFunctionType OriginalResCheckFunction = nullptr;
    safetyhook::InlineHook ResolutionPatchHook{};

    void ResCheckFunctionHook(char* param_1, int param_2, int param_3)
    {
        // Signature Scan Horizontal Res: "80 07 ?? 00 C7 45 ?? 38 04 ?? 00 E8"
        // Signature Scan Vertical Res:   "38 04 ?? 00 E8 64 BF"
        auto hResPtr   = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x4858CC);
        auto vResPtr   = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x4858D3);

        // Signature Scan Horizontal Res 4K Native: "00 0F 00 00 C7 45 C3"
        // Signature Scan Vertical Res 4K Native:   "70 ?? 00 00 E8 ?? ?? ?? ?? EB ?? E8"
        // Signature Scan Horizontal Window Size 4K Native: "00 0F 00 00 70 ?? 00 00 C0 5D 00"
        // Signature Scan Vertical Window Size 4K Native: "70 ?? 00 00 C0 5D 00"
        // TODO: Set default custom resolution value to "0" in config, check on startup if it's "0", and if so, use the current desktop resolution for the 4K Native mode. Then adjust the resolution dropdown box in the imgui ui to choose the resolution closest to the one in the config (or to simply make it a temporary option if it doesn't exist). That way there's one less thing for the user to customize.
        auto hRes4KPtr     = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x4858A7);
        auto vRes4KPtr     = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x4858AE);
        auto hWinSize4KPtr = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0xF58780);
        auto vWinSize4KPtr = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0xF58784);

        // Print out our default internal resolution values, for debugging purposes.
        spdlog::info("Resolution: Default Internal Resolution : {}x{}", *hResPtr, *vResPtr);
        spdlog::info("Resolution: 4K Native Default Internal Resolution : {}x{}", *hRes4KPtr, *vRes4KPtr);

        // Grab the current resolution index, so we can adjust the memory value for the internal resolution below the 4K Native mode to their proper resolutions.
        auto currentResolutionIndexPtr = reinterpret_cast<int*>(*reinterpret_cast<intptr_t*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x01017E18) + 0xC4);
        if (int currentResolutionIndex = *currentResolutionIndexPtr; currentResolutionIndex >= 0 && currentResolutionIndex <= 11) {
            if (hResPtr != nullptr && vResPtr != nullptr) {
                Memory::Write(reinterpret_cast<uintptr_t>(hResPtr), *resolutionList[currentResolutionIndex].X);
                Memory::Write(reinterpret_cast<uintptr_t>(vResPtr), *resolutionList[currentResolutionIndex].Y);
                spdlog::info("Resolution: Patched Internal 1080p Resolution to {}x{}.", *hResPtr, *vResPtr);
            }
            else { spdlog::error("Resolution: Horizontal and Vertical Res Pointers came back as null pointers."); }
        }
        else {
            // TODO: Figure out why writing to the internal resolution and window size causes crashing.
            if (hRes4KPtr != nullptr && vRes4KPtr != nullptr) {
                Memory::Write(reinterpret_cast<uintptr_t>(hRes4KPtr), PlayerSettingsPDQ.RES.Resolution.x);
                Memory::Write(reinterpret_cast<uintptr_t>(vRes4KPtr), PlayerSettingsPDQ.RES.Resolution.y);
                spdlog::info("Resolution: Patched Internal 4K Native Resolution to {}x{}.", *hRes4KPtr, *vRes4KPtr);
            }
            else { spdlog::error("Resolution: 4K Native Horizontal and Vertical Res Pointers came back as null pointers."); }
            if (hWinSize4KPtr != nullptr && vWinSize4KPtr != nullptr) {
                Memory::Write(reinterpret_cast<uintptr_t>(hWinSize4KPtr), PlayerSettingsPDQ.RES.Resolution.x);
                Memory::Write(reinterpret_cast<uintptr_t>(vWinSize4KPtr), PlayerSettingsPDQ.RES.Resolution.y);
                spdlog::info("Resolution: Patched Internal 4K Native Window Size to {}x{}.", *hWinSize4KPtr, *vWinSize4KPtr);
            }
            else { spdlog::error("Resolution: 4K Native Horizontal and Vertical Window Size Pointers came back as null pointers."); }
        }

        // TODO: Add return function here.
        return ResolutionPatchHook.call<void>(param_1, param_2, param_3);
    }

    void Plugin_DERQ::ResolutionPatches(HMODULE baseModule)
    {
        // TODO: Find a good place to put this check inside of the game code, just before the resolution change occurs.
        if (auto resolutionCheckFunc = Memory::PatternScan(baseModule, "48 89 ?? ?? ?? 48 89 ?? ?? ?? 57 48 83 EC ?? 48 8B ?? ?? ?? ?? ?? 48 33 ?? 48 89 ?? ?? ?? 41 8B ?? 8B EA")) {
            spdlog::info("Resolution: Found Resolution Check Signature at: {}", reinterpret_cast<void*>(resolutionCheckFunc));

            // Store original function pointer
            OriginalResCheckFunction = reinterpret_cast<ResCheckFunctionType>(resolutionCheckFunc);
            ResolutionPatchHook = safetyhook::create_inline(OriginalResCheckFunction, ResCheckFunctionHook);
        }

        // Signature for 4K Native text: "34 ?? 20 4E 61 74 69 76 65"
        // TODO: Take into account the other parts of memory that will inevitably duplicate this during runtime. If at any point the in-game overlay changes the custom resolution, we need to run a loop that changes the memory values to reflect this in-game.
        if (auto native4kText = Memory::PatternScan(baseModule, "34 ?? 20 4E 61 74 69 76 65")) {
            spdlog::info("Aspect Ratio: Found First Block Function Signature at: {}", reinterpret_cast<void*>(native4kText));

            // Generate resolution string (max 9 bytes including null terminator)
            std::string replacement = std::to_string(PlayerSettingsPDQ.RES.Resolution.x) + "x" + std::to_string(PlayerSettingsPDQ.RES.Resolution.y);

            // Ensure we do not exceed 9 bytes
            if (replacement.size() > 9) {
                spdlog::error("Resolution string '{}' is too long! Defaulting to 'Custom'.", replacement);
                std::string replacement = "Custom";
                return;
            }

            // Write the replacement string directly to the memory address
            for (size_t i = 0; i < replacement.size(); ++i) {
                Memory::Write(reinterpret_cast<uintptr_t>(native4kText + i), replacement[i]);
            }

            // Null-pad remaining bytes if the replacement string is less than 9 bytes
            for (size_t i = replacement.size(); i < 9; ++i) {
                Memory::Write(reinterpret_cast<uintptr_t>(native4kText + i), '\0');
            }
            spdlog::info("Successfully patched '4K Native' -> '{}'.", replacement);
        }

        // NOP all of the opcodes that are responsible for blocking any higher resolution from being selected than your primary resolution.
        // "41 89 ?? ?? 48 83 C4 ?? 41 ?? 5F 5E C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 48 83 EC" ("Application.exe"+485576 - 41 89 7E 0C)
        NOPPattern(baseModule, "41 89 ?? ?? 48 83 C4 ?? 41 ?? 5F 5E C3 CC CC CC CC CC CC CC CC CC CC CC CC CC 48 83 EC", 4, "Resolution Blocker Opcode 1");
        // "83 3D 73 F2 78 00"       ("Application.exe"+7C9292 - 83 3D 73 F2 78 00 0A)
        NOPPattern(baseModule, "83 3D 73 F2 78 00 0A", 7, "Resolution Blocker Opcode 2");
        // "83 3D DC F1 78 00"       ("Application.exe"+7C9329 - 83 3D DC F1 78 00 0B)
        NOPPattern(baseModule, "83 3D DC F1 78 00 0B", 7, "Resolution Blocker Opcode 3");
        // "83 3D 21 F1 78 00"       ("Application.exe"+7C93E4 - 83 3D 21 F1 78 00 0B)
        NOPPattern(baseModule, "83 3D 21 F1 78 00 0B", 7, "Resolution Blocker Opcode 4");
        // "83 3D 51 19 79 00"       ("Application.exe"+7C6BB4 - 83 3D 51 19 79 00 0B)
        NOPPattern(baseModule, "83 3D 51 19 79 00 0B", 7, "Resolution Blocker Opcode 5");
        // "39 7B ?? 0F 4C"          ("Application.exe"+4852BA - 39 7B 0C)
        NOPPattern(baseModule, "39 7B ?? 0F 4C", 3, "Resolution Blocker Opcode 6");
        // "8B 53 ?? 8B 43 ?? 89 41" ("Application.exe"+48530A - 8B 53 0C)
        NOPPattern(baseModule, "8B 53 ?? 8B 43 ?? 89 41", 3, "Resolution Blocker Opcode 7");

        // Signature for currently selected screen mode index:
        // "00 00 00 00 00 00 00 00 FF 01 00 00 4C 00 ?? 00 01 00 02 00 04 ?? 05 ?? ?? ?? ?? 00 08 00 0E 00 0D ?? ?? ?? ?? 00 12 00 13 00 15 ?? ?? ?? ?? 00 19 00 1B 00 1C ?? 1F 00 21 00 23 00 25 ?? ?? ?? ?? 00 29 00 2A 00 2C ?? 2F 00 31 00 32 00 34 ?? 35 ?? ?? ?? ?? 00 3B 00 3C ?? 3D ?? ?? ?? ?? 00 42 ?? 43 00 ?? ?? 48 00 ?? ?? ?? 00 4A ?? 4B 00 ?? ?? 4E 00 ?? ?? 50 00 51 ?? 52 00 53 ?? 54 00 55 ?? 56 00 57 ?? 58 00 59 ?? 5A 00 5B ?? 5C 00 5D ?? 5E 00 5F ?? 60 00 14 ?? 2B 00 61 00 62 ?? 63 00 64 00 ?? 00 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 02 03 02 00 00 00 00 02 02 02 02 02 02 02 02 00 00 01 01 01 01 01 01 01 01 02 02 02 02 02 02 01 01 01 01 01 01 01 01 01 01 02 02 00 00 02 02 01 01 01 01 02 02 02 02 02 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 01 01 01 00 00 00 00 00 00 00 00 00 00 01 01 03 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 08 00 00 00 3D ?? ?? ?? ?? 4E 01 ?? 34 ?? 01 00 2C ?? 01 00 2B 4E ?? 00 27 4E 01 ?? 24 ?? 01 00 23 4E ?? 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"
        // Signature for currently selected resolution index:
        // "09 00 00 00 00 00 00 00 00 00 00 00 FF 01 00 00 4C 00 ?? 00 01 00 02 00 04 ?? 05 ?? ?? ?? ?? 00 08 00 0E 00 0D ?? ?? ?? ?? 00 12 00 13 00 15 ?? ?? ?? ?? 00 19 00 1B 00 1C ?? 1F 00 21 00 23 00 25 ?? ?? ?? ?? 00 29 00 2A 00 2C ?? 2F 00 31 00 32 00 34 ?? 35 ?? ?? ?? ?? 00 3B 00 3C ?? 3D ?? ?? ?? ?? 00 42 ?? 43 00 ?? ?? 48 00 ?? ?? ?? 00 4A ?? 4B 00 ?? ?? 4E 00 ?? ?? 50 00 51 ?? 52 00 53 ?? 54 00 55 ?? 56 00 57 ?? 58 00 59 ?? 5A 00 5B ?? 5C 00 5D ?? 5E 00 5F ?? 60 00 14 ?? 2B 00 61 00 62 ?? 63 00 64 00 ?? 00 10 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 02 03 02 00 00 00 00 02 02 02 02 02 02 02 02 00 00 01 01 01 01 01 01 01 01 02 02 02 02 02 02 01 01 01 01 01 01 01 01 01 01 02 02 00 00 02 02 01 01 01 01 02 02 02 02 02 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 01 01 01 00 00 00 00 00 00 00 00 00 00 01 01 03 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 08 00 00 00 3D ?? ?? ?? ?? 4E 01 ?? 34 ?? 01 00 2C ?? 01 00 2B 4E ?? 00 27 4E 01 ?? 24 ?? 01 00 23 4E ?? 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"


        //if (PlayerSettingsRef.RES.UseCustomRes) {
            //*hRes4KPtr = PlayerSettingsRef.RES.HorizontalRes;
            //*vRes4KPtr = PlayerSettingsRef.RES.VerticalRes;
            //spdlog::info("Patched 4K Option Internal Resolution to : {}x{}", *hRes4KPtr, *vRes4KPtr);
        //}
    }

    void Plugin_DERQ::AspectRatioPatches(HMODULE baseModule)
    {
        // TODO: Fix these
        // Disable aspect ratio values from being overwritten
        //NOPPattern(baseModule, "89 41 ?? 0F 10 ?? ?? 0F 11 ?? ?? 0F 10 ?? ?? 0F 11 ?? ?? 0F 10 ?? ?? 0F 11 ?? ?? 8B 82", 3, "Aspect Ratio Change Blocker Opcode 1"); // (89 41 50)
        //NOPPattern(baseModule, "F3 0F ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75", 5, "Aspect Ratio Change Blocker Opcode 2"); // (F3 0F 11 4F 50)
        //NOPPattern(baseModule, "F3 0F ?? ?? ?? 48 8B ?? ?? ?? ?? ?? 66 0F", 3, "Aspect Ratio Change Blocker Opcode 3"); // (F3 0F 11 4F 50)
        //NOPPattern(baseModule, "8B 42 ?? 89 41 ?? 0F 10 ?? ?? 0F 11 ?? ?? 0F 10 ?? ?? 0F 11 ?? ?? 0F 10 ?? ?? 0F 11 ?? ?? 8B 82", 3, "Aspect Ratio Change Blocker Opcode 4");// (8B 42 50)

        // TODO: Figure out what opcodes access these memory pointers, and update them to use our own internal aspect ratio variable.
        // Set up the pointer addresses for our aspect ratio variables

        auto aspectRatioPtr1     = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x25EFC6);
        auto aspectRatioPtr2     = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0x789B2C);
        auto aspectRatioPtr3     = reinterpret_cast<int*>(reinterpret_cast<intptr_t>(PatchManagerPDQ.BaseModule) + 0xE32A30);

        // Overwrite the aspect ratio values with our own.
        if (aspectRatioPtr1 != nullptr) {
            //Memory::Write(reinterpret_cast<uintptr_t>(aspectRatioPtr1), PlayerSettingsPDQ.RES.InternalAspectRatio);
            //spdlog::info("Aspect Ratio: Patched Aspect Ratio Pointer 1 to: {}", *aspectRatioPtr1);
        }
        else {spdlog::error("Aspect Ratio: Aspect Ratio Pointer 1 is NULL"); }

        if (aspectRatioPtr2 != nullptr) {
            //Memory::Write(reinterpret_cast<uintptr_t>(aspectRatioPtr2), PlayerSettingsPDQ.RES.InternalAspectRatio);
            //spdlog::info("Aspect Ratio: Patched Aspect Ratio Pointer 1 to: {}", *aspectRatioPtr2);
        }
        else {spdlog::error("Aspect Ratio: Aspect Ratio Pointer 2 is NULL"); }

        // Address that accesses aspect ratio ptr 3: "F3 0F ? ? ? ? ? ? F3 0F ? ? ? ? ? ? F3 0F ? ? ? ? F3 0F ? ? ? ? ? ? F3 0F ? ? ? F3 0F ? ? ? F3 0F ? ? ? ? 0F 57" - Application.exe + 0x007951F9 (movss xmm0,[140E32A30])
        if (aspectRatioPtr3 != nullptr) {
            //Memory::Write(reinterpret_cast<uintptr_t>(aspectRatioPtr3), PlayerSettingsPDQ.RES.InternalAspectRatio);
            //spdlog::info("Aspect Ratio: Patched Aspect Ratio Pointer 3 to: {}", *aspectRatioPtr3);
        }
        else {spdlog::error("Aspect Ratio: Aspect Ratio Pointer 3 is NULL"); }

        // Address that accesses object aspect ratio ptr: "8B 87 ? ? ? ? 89 87 ? ? ? ? 8B 87 ? ? ? ? 89 87 ? ? ? ? 8B 87 ? ? ? ? 89 87 ? ? ? ? E8" - Application.exe + 0x006E069B (mov eax,[rdi+000004D0])

        // Write to the object aspect ratio pointer
        // TODO: Figure out why the object aspect ratio pointer isn't working right.
        auto* objectAspectRatioPtr = reinterpret_cast<float*>(reinterpret_cast<intptr_t>(baseModule) + 0x01043B30 + 0xC90);
        if (objectAspectRatioPtr != nullptr) {
            spdlog::info("Object Aspect Ratio: {}", *objectAspectRatioPtr);
            // TODO: Find a way to change run resolution and aspect ratio checks every time the internal rendering resolution changes.
            //Memory::Write(reinterpret_cast<uintptr_t>(objectAspectRatioPtr), PlayerSettingsPDQ.RES.InternalAspectRatio);
            //spdlog::info("Aspect Ratio: Patched Aspect Ratio to: {}", *objectAspectRatioPtr);
        }
        else {
            spdlog::error("Aspect Ratio: Object Aspect Ratio pointer is NULL");
        }
    }

    void Plugin_DERQ::FOVPatches(HMODULE baseModule)
    {

    }

    void Plugin_DERQ::UIPatches(HMODULE baseModule)
    {

    }

    //void __attribute__((naked)) FramerateUnlockHook() {
        //asm volatile (
            //"movl $1, %eax\n\t"
            //"ret\n\t"
        //);
    //}

    void Plugin_DERQ::FrameratePatches(HMODULE baseModule)
    {
        safetyhook::InlineHook framerateHook;
        using FramerateLimiterFunc = int(__stdcall*)(void* gameInstance);
        FramerateLimiterFunc originalFramerateLimiter = nullptr;

        // Delta Time Address Signature: "89 88 ?? ?? ?? ?? 80 3F"

        if (auto framerateCapFunc = Memory::PatternScan(baseModule, "8B 80 ?? ?? ?? ?? 89 44 ?? ?? 83 7C 24 44 ?? 74 ?? 83 7C 24 44")) {
            spdlog::info("Found Framerate Limiter Signature at: {}", reinterpret_cast<void*>(framerateCapFunc));
            // Hook the function
            //framerateHook = safetyhook::create_inline(framerateCapFunc, FramerateManagerPDQ.Limit());
            //originalFramerateLimiter = framerateHook.original<FramerateLimiterFunc>();

            // TODO: Patch out the framerate limiter call, and replace it with our own framelimiter.
            // This seems to be the call: "8B 80 A0 00 00 00" ("Application.exe"+E88FA - mov eax,[rax+000000A0]) (8B 80 A0 00 00 00 are the bytes)

            // Address of signature = Application.exe + 0x000E8962
            // The modification after the framelimiter is called should jump to "F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? ?? ?? 48 8B ?? ?? ?? ?? ?? ?? 83 B8 A4 00 00 00" (Application.exe+E8962 - movss xmm0,[rsp+40])
            // The actual bytes of the jmp location should be "Application.exe+E8962 - F3 0F 10 44 24 40"
        }

        // For now, disable the framelimiter.
        NOPPattern(baseModule, "8B 80 ?? ?? ?? ?? 89 44 ?? ?? 83 7C 24 44 ?? 74 ?? 83 7C 24 44", 6, "Framerate Limiter");


        //safetyhook::create_inline()

        //asm volatile (
            //"nop"
        //);
    }

    void Plugin_DERQ::SchedulerPatches(HMODULE baseModule)
    {
        int cpuSchedulerMode = 0; // 0 : Original, 1: AMD, 2: Intel
        uint8_t cpuSchedulerModeByte;
        switch (cpuSchedulerMode) {
            case 0: break; // Original, do nothing
            case 1: { // AMD CPU Scheduling
                cpuSchedulerModeByte = 0x00;
                break;
            }
            case 2: { // Intel CPU Scheduling
                cpuSchedulerModeByte = 0x01;
                break;
            }
            default: break;  // Original, do nothing
        }
        if (cpuSchedulerMode != 0 && cpuSchedulerModeByte)
        {
            // TODO: Figure if there's any performance improvement or rammifications from this. If there's an improvement, keep it. If some notice it's better on their setup while others have issues with it, just make it a config option.
            // Use Intel CPU scheduling instead of AMD on AMD CPU (I'm morbidly curious since apparently Cyberpunk 2077 had some issue at launch revolving around it).
            if (auto cpuSchedulerPatchAMD = Memory::PatternScan(baseModule, "C7 44 24 24 ?? ?? ?? ?? EB ?? C7 44 24 24 ?? ?? ?? ?? 8B 44 ?? ?? 89 44 ?? ?? 83 7C 24 20")) {
                spdlog::info("{} found at: {}", "CPU Scheduler for AMD", reinterpret_cast<void*>(cpuSchedulerPatchAMD));
                // NOP out the specified number of bytes (replace with "01")
                for (size_t i = 0; i < 5; ++i) {
                    Memory::Write(reinterpret_cast<uintptr_t>(cpuSchedulerPatchAMD + i), static_cast<uint8_t>(cpuSchedulerModeByte));
                }
            }
            // Use AMD CPU scheduling instead of Intel on Intel CPU.
            if (auto cpuSchedulerPatchIntel = Memory::PatternScan(baseModule, "C7 44 24 24 ?? ?? ?? ?? 8B 44 ?? ?? 89 44 ?? ?? 83 7C 24 20")) {
                spdlog::info("{} found at: {}", "CPU Scheduler for Intel", reinterpret_cast<void*>(cpuSchedulerPatchIntel));
                // NOP out the specified number of bytes (replace with "00")
                for (size_t i = 0; i < 5; ++i) {
                    Memory::Write(reinterpret_cast<uintptr_t>(cpuSchedulerPatchIntel + i), static_cast<uint8_t>(cpuSchedulerModeByte));
                }
            }
        }
    }

    void Plugin_DERQ::GraphicsSettingsPatches(HMODULE baseModule)
    {
        // NOTE: Because you can technically adjust these settings while in-game, maybe I should do the signature scans first, and then find a way to have those reference the PlayerSettings variables.

        // Toggle off post-processing (From what I see, this is only a thing in the CE Table, a debugging thing if you will)
        if (auto postProcessToggleFunc = Memory::PatternScan(baseModule, "83 F8 ?? 75 ?? 41 8B ?? 49 8D")) { // Application.exe+6D8639: cmp eax,01 (83 F8 01)
            spdlog::info("Post Processing: Found Post Processing Toggle Signature at: {}", reinterpret_cast<void*>(postProcessToggleFunc));
        }

        if (PlayerSettingsPDQ.RS.Tonemapping) {
            if (auto tonemappingToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 0F ?? ?? ?? ?? ?? ?? 49 69 CF ?? ?? ?? ?? 42 88 ?? ?? ?? 41 0F")) { // "Application.exe"+29C420: mov [rcx+r13+70],al (42 88 44 29 70)
                spdlog::info("Post Processing: Found Color Correction Signature at: {}", reinterpret_cast<void*>(tonemappingToggleFunc));
                static SafetyHookMid tonemappingToggleHook;
            }
        }

        if (PlayerSettingsPDQ.RS.ColorCorrection) {
            if (auto colorCorrectionToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 66 41")) { // "Application.exe"+29C858: mov [rcx+r13+04],al (42 88 44 29 04)
                spdlog::info("Post Processing: Found Color Correction Signature at: {}", reinterpret_cast<void*>(colorCorrectionToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.LensFlare) {
            if (auto lensFlareToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7 ?? ?? ?? ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7 ?? ?? ?? ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 0F")) { // "Application.exe"+29CE43: mov [rcx+r13+000000EC],al (42 88 84 29 EC 00 00 00)
                spdlog::info("Post Processing: Found Lens Flare Signature at: {}", reinterpret_cast<void*>(lensFlareToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.Fog) {
            if (auto fogToggleFunc = Memory::PatternScan(baseModule, "88 44 ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 6B C7 ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? ?? ?? F3 0F ?? ?? F3 0F ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 8D ?? ?? ?? ?? ?? 0F 28 ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 8B")) { // "Application.exe"+29D495: mov [rcx+rsi+04],al; lea rdx,[rsp+50] (88 44 31 04 48 8D 54 24 50; F3 0F 58 C8 F3 42 0F 11 8C 28 64 01 00 00)
                spdlog::info("Post Processing: Found Fog Signature at: {}", reinterpret_cast<void*>(fogToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.RLRLighting) {
            // Disable IBL Lighting:
            // E8 ?? ?? ?? ?? 49 8B ?? E8 ?? ?? ?? ?? 48 8D ?? ?? 4C 89 (Application.exe+2981AD - E8 8E AE FF FF - call Application.exe+293040) This needs to have a switch statement that checks things before running it.

            // In the post processing settings
            if (auto rlrLightingToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7 ?? ?? ?? ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7 ?? ?? ?? ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D")) { // "Application.exe"+29D3DF: mov [rcx+r13+000001C4],al (42 88 84 29 C4 01 00 00)
                spdlog::info("Post Processing: Found RLR Lighting Signature at: {}", reinterpret_cast<void*>(rlrLightingToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.CameraDistortion) {
            if (auto cameraDistortionToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7 ?? ?? ?? ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 0F")) { // "Application.exe"+29CEF5: mov [rcx+r13+00000160],al (42 88 84 29 60 01 00 00)
                spdlog::info("Post Processing: Found Camera Distortion Signature at: {}", reinterpret_cast<void*>(cameraDistortionToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.Bloom) {
            if (auto bloomToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 66 41 ?? ?? ?? ?? ?? ?? ?? 49 69 CF ?? ?? ?? ?? 0F 5B ?? F3 0F ?? ?? 66 42 ?? ?? ?? ?? ?? ?? ?? ?? 0F 5B ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? 42 89 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 8B")) { // "Application.exe"+29CB44: mov [rcx+r13+000000BC],al (42 88 84 29 BC 00 00 00)
                spdlog::info("Post Processing: Found Bloom Signature at: {}", reinterpret_cast<void*>(bloomToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.MotionBlur == true) {
            if (auto motionBlurToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 8B")) { // "Application.exe"+29CFF3: mov [rcx+r13+58],al (42 88 44 29 58)
                spdlog::info("Post Processing: Found Motion Blur Signature at: {}", reinterpret_cast<void*>(motionBlurToggleFunc));
            }
            //switch (PlayerSettingsPDQ.RS.MotionBlurPreset) {
                //case 0:  // Disabled, will probably keep this around for motion vectors
                //case 1:  // Short
                //case 2:  // Medium
                //case 3:  // Long
                //default: // Shutter Ratio (Application.exe+7735D8, default: 0.67542696) and Max Blur Length (Application.exe+25EE50, default: 0.1000000015)
            //}
        }

        if (PlayerSettingsPDQ.RS.DepthOfField) {
            if (auto dofToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7 ?? ?? ?? ?? 0F 28 ?? 0F 28 ?? F3 41 ?? ?? ?? ?? ?? ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? F3 0F ?? ?? F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 8B")) { // "Application.exe"+29C600: mov [rcx+r13+00000118],al (42 88 84 29 18 01 00 00)
                spdlog::info("Post Processing: Found Depth of Field Signature at: {}", reinterpret_cast<void*>(dofToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.SSAO) {
            if (auto ssaoToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 66 41 ?? ?? ?? ?? ?? ?? ?? 49 69 CF ?? ?? ?? ?? 0F 5B ?? F3 0F ?? ?? 66 42 ?? ?? ?? ?? ?? ?? ?? ?? 0F 5B ?? F3 0F ?? ?? F3 0F ?? ?? F3 0F ?? ?? 42 89 ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7")) { // "Application.exe"+29D0BE: mov [rcx+r13+00000168],al (42 88 84 29 68 01 00 00)
                spdlog::info("Post Processing: Found SSAO Signature at: {}", reinterpret_cast<void*>(ssaoToggleFunc));
            }
        }

        if (PlayerSettingsPDQ.RS.TAA) {
            if (auto taaToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 49 69 C7")) { // "Application.exe"+29CF8D: mov [rcx+r13+69],al (42 88 44 29 69)
                spdlog::info("Post Processing: Found TAA Signature at: {}", reinterpret_cast<void*>(taaToggleFunc));
            }
            if (auto aaToggleFunc = Memory::PatternScan(baseModule, "42 88 ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 0F ?? ?? ?? ?? ?? ?? 49 69 CF ?? ?? ?? ?? 42 88 ?? ?? ?? 48 8D")) { // "Application.exe"+29CF64: mov [rcx+r13+68],al (42 88 44 29 68)
                spdlog::info("Post Processing: Found Post AA Signature at: {}", reinterpret_cast<void*>(aaToggleFunc));
            }
        }

        if (!PlayerSettingsPDQ.RS.Vignette) { // This one works a little bit differently, as you need to move a new float variable to the xmm1 register BEFORE the opcode is done, that way we can override the float variable used for the vignette intensity.
            if (auto vignetteFunc = Memory::PatternScan(baseModule, "F3 42 ?? ?? ?? ?? ?? ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 0F ?? ?? ?? ?? ?? ?? 49 69 CF ?? ?? ?? ?? 42 88 ?? ?? ?? 48 8D ?? ?? ?? 48 8D ?? ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 74 ?? 41 0F")) { // "Application.exe"+29CF36: movss [rax+r13+00000164],xmm1 (F3 42 0F 11 8C 28 64 01 00 00)
                spdlog::info("Post Processing: Found Vignette Signature at: {}", reinterpret_cast<void*>(vignetteFunc));
                static SafetyHookMid vignetteFuncMidHook{};
                vignetteFuncMidHook = safetyhook::create_mid(vignetteFunc,
                    [](SafetyHookContext& ctx) {
                        ctx.xmm1.f32[0] = 0.0f; // The default is "0.200000003"
                    });
            }
        }
    }

    void Plugin_DERQ::CameraPatches(HMODULE baseModule)
    {

    }

    void Plugin_DERQ::PhotoModePatches(HMODULE baseModule)
    {

    }

    std::string ConvertUTF16toUTF8(const wchar_t* wstr) {
        std::string utf8_str;
        const wchar_t* ptr = wstr;

        while (*ptr) {
            // Assuming UTF-16 with most characters fitting into 3 bytes in UTF-8
            if (*ptr < 0x80) {
                utf8_str.push_back(static_cast<char>(*ptr));  // ASCII range
            } else if (*ptr < 0x800) {
                utf8_str.push_back(static_cast<char>(0xC0 | (*ptr >> 6)));
                utf8_str.push_back(static_cast<char>(0x80 | (*ptr & 0x3F)));
            } else {
                utf8_str.push_back(static_cast<char>(0xE0 | (*ptr >> 12)));
                utf8_str.push_back(static_cast<char>(0x80 | ((*ptr >> 6) & 0x3F)));
                utf8_str.push_back(static_cast<char>(0x80 | (*ptr & 0x3F)));
            }
            ++ptr;
        }
        return utf8_str;
    }

    void HookedLoggingFunction(int code, const char* format, ...) {
        if (!format) return;  // Avoid null format strings

        // Allocate a buffer for the formatted message
        char buffer[1024];

        // Process variable arguments correctly
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        // Check for Japanese characters (heuristic: if `format` contains wide chars)
        std::string message = buffer;
        if (strstr(format, "%ls")) {  // If the format string expects wide strings
            const wchar_t* wideStr = reinterpret_cast<const wchar_t*>(buffer);
            message = ConvertUTF16toUTF8(wideStr);
        }

        // Switches the log type based on the output.
        switch (code) {
            case 0:  spdlog::info("[Game Log] {}", message);  break;
            case 1:  spdlog::warn("[Game Log] {}", message);  break;
            case 2:  spdlog::error("[Game Log] {}", message); break;
            default: spdlog::info("[Game Log] [Unknown Code {}] {}", code, message); break;
        }
    }

    void Plugin_DERQ::LoggingPatches(HMODULE baseModule)
    {


        if (auto loggingFunc = Memory::PatternScan(baseModule, "4C 89 44 24 ?? 4C 89 4C 24 ?? C3 CC CC CC CC CC 48 8b 01")) {
            spdlog::info("Logging: Found Engine Logging Function Signature at: {}", reinterpret_cast<void*>(loggingFunc));
            using LoggingFunctionType = void(*)(int, const char*, ...);
            LoggingFunctionType OriginalLoggingFunction = nullptr;

            // Store original function pointer
            OriginalLoggingFunction = reinterpret_cast<LoggingFunctionType>(loggingFunc);
            static auto hook = safetyhook::create_inline(OriginalLoggingFunction, HookedLoggingFunction);
            // TODO: Print the logs to the console.
        }
    }

    // ALT+F4 Window Signatures:
    // FF 15 ?? ?? ?? ?? 83 F8 ?? 0F 85
    // 83 F8 ?? 0F 85 ?? ?? ?? ?? 45 89 ?? 33 C0
} // EnigmaFix