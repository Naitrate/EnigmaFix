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
#include "Plugin_DERQ2.h"
#include "PatchSite.h"
#include "../Settings/PlayerSettings.h"
#include "../Utilities/MemoryHelper.h"

// Third Party Libraries
#include <safetyhook.hpp>
#include "spdlog/spdlog.h"

// Ported from DeathEndReQuest2.CT. Unlike the Death end re;Quest table, that one recorded almost no IDA signatures, so
// most sites below are documented rather than applied. See PatchSite.h for how to activate one.

auto& PlayerSettingsPDQ2 = EnigmaFix::PlayerSettings::Get();

EnigmaFix::Plugin_DERQ2 EnigmaFix::Plugin_DERQ2::derq2_Instance;

namespace EnigmaFix
{
    void Plugin_DERQ2::ResolutionPatches(HMODULE baseModule)
    {
        // "DeathEndReQuest2.exe"+12483F: mov [rbx+0C],eax
        // The resolution list is capped to whatever the desktop supports. Forcing the index to 11 unlocks the whole
        // list. This is one of the three sites in the table that came with a signature, so it applies today.
        if (auto maxResCheckFunc = Memory::PatternScan(baseModule, "89 43 ?? 48 83 C4 ?? 5B C3 CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC CC 48 83 EC")) {
            spdlog::info("Resolution: Found Max Resolution Check Signature at: {}", reinterpret_cast<void*>(maxResCheckFunc));
            static SafetyHookMid maxResCheckMidHook{};
            maxResCheckMidHook = safetyhook::create_mid(maxResCheckFunc,
                [](SafetyHookContext& ctx) {
                    ctx.rax = (ctx.rax & ~static_cast<uintptr_t>(0xFFFFFFFF)) | 11;  // Highest entry in the resolution list.
                });
            spdlog::info("Resolution: Unlocked the full resolution list.");
        }
        else { spdlog::error("Resolution: Max Resolution Check Signature was not found."); }

        // The internal resolution and window size values are plain data rather than code, so they have no signature to
        // scan for at all. They were read out of the table as:
        //   +124EA7  Fullscreen internal horizontal resolution (80 07 = 1920)
        //   +124F26  Fullscreen internal resolution, 4K Native mode (00 0F = 3840, 70 08 = 2160)
        //   +173DE6  Windowed internal resolution, 4K Native mode
        //   +13E0AD8 Windowed mode size, 1920x1080
        //   +13E0AE8 Windowed mode size, 4K Native
        //   +13E0878 Screen resolution array      +13E0874 Screen mode (0 windowed, 1 fullscreen, 2 borderless)
        //   +13E087C Max available resolution     +147CE20 Current window resolution
        // TODO: Reach these the way the DERQ plugin does, by hooking the resolution change function and walking the
        // config struct, rather than by absolute address.
    }

    void Plugin_DERQ2::AspectRatioPatches(HMODULE baseModule)
    {
        // Stops the engine overwriting the aspect ratio, so a custom one survives. Equivalent to the DERQ plugin's
        // "Disable Aspect Ratio Values from being overwritten", and the opcodes are byte for byte identical between the
        // two games -- only the surrounding context differs, which is exactly what still needs deriving.
        static constexpr PatchSite sites[] = {
            { "Aspect Ratio Write 1", 0x3D860E, "F3 0F 11 4F 50", "movss [rdi+50],xmm1", "", PatchAction::NopBytes, 5 },
            { "Aspect Ratio Write 2", 0x3D85D7, "F3 0F 11 4F 50", "movss [rdi+50],xmm1", "", PatchAction::NopBytes, 5 },
            { "Aspect Ratio Write 3", 0x3D2147, "89 41 50",       "mov [rcx+50],eax",    "", PatchAction::NopBytes, 3 },
        };
        ApplyPatchSites(baseModule, sites, "Aspect Ratio");

        // Aspect ratio value for save crystals, other objects and culling, at +0AE77228 (2.370370388 for 21:9).
    }

    void Plugin_DERQ2::FOVPatches(HMODULE baseModule)
    {
        if (!PlayerSettingsPDQ2.FOV.UseCustomFOV) { return; }

        // The same four writes the DERQ plugin deals with, and the same opcodes: the main gameplay write and the battle
        // write are both "movss [rcx+000004C4],xmm0", and the unpause restore is still a hardcoded 45 degrees.
        // Once signatures exist, prefer the DERQ approach of hooking these and overriding the register, rather than
        // NOPing, so the FOV can be retuned at runtime. The actions below mirror what the Cheat Engine table did.
        static constexpr PatchSite sites[] = {
            { "FOV Write (Gameplay)",  0x3D780F, "F3 0F 11 81 C4 04 00 00", "movss [rcx+000004C4],xmm0", "", PatchAction::NopBytes, 8 },
            { "FOV Write (Battle)",    0x3D718B, "F3 0F 11 81 C4 04 00 00", "movss [rcx+000004C4],xmm0", "", PatchAction::NopBytes, 8 },
            { "FOV Unpause Restore 1", 0x3D2135, "89 41 44",                "mov [rcx+44],eax",          "", PatchAction::NopBytes, 3 },
            { "FOV Unpause Restore 2", 0x3D861F, "C7 47 44 00 00 34 42", "mov [rdi+44],(float)45.0", "", PatchAction::NopBytes, 7 },  // Signature derived and unique, but deliberately left empty: the table applied all four FOV sites together, and NOPing this one alone would drop the unpause restore while the FOV writes still run.
        };
        ApplyPatchSites(baseModule, sites, "FOV");

        // Live FOV value at +014D77C8, backup pointer at +0AE72018. Defaults are 45 degrees Hor+ in the overworld and
        // 43 degrees in battle.
    }

    void Plugin_DERQ2::UIPatches(HMODULE baseModule)
    {
        // UI and minimap scaling. The table hardcodes 2580 and 1080 here, which are the numbers that make the UI sit
        // correctly at 21:9 -- they are not general purpose. Deriving the right values from the active aspect ratio is
        // a design decision rather than a port, so these stay documented even though the first one has a signature.
        //
        // That signature, "8B 00 89 84 ? ? ? ? ? 48 8B ? ? ? ? ? ? 8B 40 ? 89 84 ? ? ? ? ? EB", is the same pattern the
        // DERQ table used for its "UI Adjustment 2", so the two games share this code path.
        static constexpr PatchSite sites[] = {
            { "UI Scale (Horizontal)",     0x71ECE0, "8B 00 89 84 24 F0 00 00 00",    "mov eax,[rax] / mov [rsp+000000F0],eax",  "", PatchAction::Document, 9 },
            { "UI Scale (Vertical)",       0x71ECF1, "8B 40 04 89 84 24 F4 00 00 00", "mov eax,[rax+04] / mov [rsp+000000F4],eax","", PatchAction::Document, 10 },
            { "UI Scale (Pause Menu 1)",   0x5E16F9, "F3 0F 2A 48 40",                "cvtsi2ss xmm1,[rax+40]",                  "", PatchAction::Document, 5 },
            { "UI Scale (Pause Menu 2)",   0x5E174C, "F3 0F 2A 48 40",                "cvtsi2ss xmm1,[rax+40]",                  "", PatchAction::Document, 5 },
            { "UI Scale (Affects FMVs)",   0x588E30, "8B 09 89 08 48 8B 44 24 28",    "mov ecx,[rcx] / mov [rax],ecx",           "", PatchAction::Document, 9 },
            { "Minimap Scale", 0x382045, "F3 0F 10 35 D7 E3 B6 00", "movss xmm6,[DeathEndReQuest2.exe+EF0424]", "F3 0F 10 35 D7 E3 B6 00", PatchAction::Document, 8 },
        };
        ApplyPatchSites(baseModule, sites, "UI");
    }

    void Plugin_DERQ2::VideoPatches(HMODULE baseModule)
    {
        if (!PlayerSettingsPDQ2.MS.SkipOpeningVideos) { return; }

        // Content addressed rather than offset addressed, so this one needs no signature work and applies today. The
        // SYSTEM/WARN notice screens share the same block of memory and are deliberately left intact.
        for (const auto& openingVideo : {
            "../../resource/finalizedWin64/MOVIE/game_op.usm",
            "../../resource/finalizedWin64/MOVIE/EN/game_op.usm",
            "../../resource/finalizedWin64/MOVIE/TCH/game_op.usm",
            "../../resource/finalizedWin64/MOVIE/SCH/game_op.usm",
            "../../resource/finalizedWin64/MOVIE/game_op_001.usm",
            "../../resource/finalizedWin64/MOVIE/EN/game_op_001.usm",
            "../../resource/finalizedWin64/MOVIE/TCH/game_op_001.usm",
            "../../resource/finalizedWin64/MOVIE/SCH/game_op_001.usm",
            "../../resource/finalizedWin64/MOVIE/logo_if.usm",
            "../../resource/finalizedWin64/MOVIE/logo_ch.usm",
            "../../resource/finalizedWin64/MOVIE/logo_silicon.usm",
            "../../resource/finalizedWin64/MOVIE/logo_silicon_en.usm",
        }) {
            NOPStringLiteral(baseModule, openingVideo, "Video Playback");
        }
    }

    void Plugin_DERQ2::GraphicsSettingsPatches(HMODULE baseModule)
    {
        // Every toggle here is a "mov [r14+r15+disp],al" fed from the graphics settings struct, so forcing AL to 0 just
        // before the store is what disables the effect. Offsets into that struct differ from DERQ's, but the shape of
        // the code is identical.
        static constexpr PatchSite colorCorrection    = { "Color Correction", 0x77BFE1, "43 88 44 3E 0C", "mov [r14+r15+0C],al", "43 88 44 3E 0C", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite depthOfField       = { "Depth of Field", 0x77BB58, "43 88 84 3E 4C 01 00 00", "mov [r14+r15+0000014C],al", "43 88 84 3E 4C 01 00 00", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite glare              = { "Glare", 0x77C4DB, "43 88 84 3E F0 00 00 00", "mov [r14+r15+000000F0],al", "43 88 84 3E F0 00 00 00", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite lensDistortion     = { "Lens Distortion", 0x77CABD, "43 88 84 3E 98 01 00 00", "mov [r14+r15+00000198],al", "43 88 84 3E 98 01 00 00", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite antiAliasing       = { "Anti-Aliasing", 0x77CDB1, "43 88 44 3E 70", "mov [r14+r15+70],al", "43 88 44 3E 70", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite temporalAA         = { "Temporal AA", 0x77CDFD, "43 88 44 3E 71", "mov [r14+r15+71],al", "43 88 44 3E 71", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite motionBlur         = { "Motion Blur", 0x77D31D, "43 88 44 3E 60", "mov [r14+r15+60],al", "43 88 44 3E 60", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite ssao               = { "SSAO", 0x77D47D, "43 88 84 3E A0 01 00 00", "mov [r14+r15+000001A0],al", "43 88 84 3E A0 01 00 00", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite rlrLighting        = { "RLR Lighting", 0x77D992, "43 88 84 3E FC 01 00 00", "mov [r14+r15+000001FC],al", "43 88 84 3E FC 01 00 00", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite fog                = { "Fog", 0x77DA2F, "43 88 84 3E 88 09 00 00", "mov [r14+r15+00000988],al", "43 88 84 3E 88 09 00 00", PatchAction::ForceALZero, 8 };

        const struct { const PatchSite& Site; bool Enabled; } toggles[] = {
            { colorCorrection, PlayerSettingsPDQ2.RS.ColorCorrection },
            { depthOfField,    PlayerSettingsPDQ2.RS.DepthOfField    },
            { glare,           PlayerSettingsPDQ2.RS.Bloom           },
            { lensDistortion,  PlayerSettingsPDQ2.RS.CameraDistortion},
            { antiAliasing,    PlayerSettingsPDQ2.RS.TAA             },
            { temporalAA,      PlayerSettingsPDQ2.RS.TAA             },
            { motionBlur,      PlayerSettingsPDQ2.RS.MotionBlur      },
            { ssao,            PlayerSettingsPDQ2.RS.SSAO            },
            { rlrLighting,     PlayerSettingsPDQ2.RS.RLRLighting     },
            { fog,             PlayerSettingsPDQ2.RS.Fog             },
        };
        for (const auto& toggle : toggles) {
            if (!toggle.Enabled) { ApplyPatchSite(baseModule, toggle.Site, "Post Processing"); }
        }

        // These have no PlayerSettings flag yet, so they are recorded rather than wired up.
        // The two unnamed sites were left unidentified in the Cheat Engine table as well.
        static constexpr PatchSite unsettable[] = {
            { "Chromatic Aberration", 0x77CCFD, "43 88 84 3E 84 01 00 00", "mov [r14+r15+00000184],al", "43 88 84 3E 84 01 00 00", PatchAction::Document, 8 },
            { "Unidentified Toggle 1", 0x77B841, "43 88 84 3E A4 00 00 00", "mov [r14+r15+000000A4],al", "43 88 84 3E A4 00 00 00", PatchAction::Document, 8 },
            { "Unidentified Toggle 2", 0x77B82C, "43 88 84 3E 81 09 00 00", "mov [r14+r15+00000981],al", "43 88 84 3E 81 09 00 00", PatchAction::Document, 8 },
        };
        ApplyPatchSites(baseModule, unsettable, "Post Processing");

        // Forcing SMAA on. Note this fights the anti-aliasing toggles above: the table's SMAA script sets +70 and +78
        // to 1, so only enable this when TAA is left on.
        static constexpr PatchSite smaa[] = {
            { "SMAA Enable", 0x77CDB1, "43 88 44 3E 70", "mov [r14+r15+70],al", "43 88 44 3E 70", PatchAction::Document, 5 },
            { "SMAA Method", 0x77CEAC, "43 89 44 3E 78", "mov [r14+r15+78],eax", "43 89 44 3E 78", PatchAction::Document, 5 },
            { "SMAA Threshold", 0x77CF0F, "F3 43 0F 11 4C 3E 7C", "movss [r14+r15+7C],xmm1", "F3 43 0F 11 4C 3E 7C", PatchAction::Document, 7 },
        };
        ApplyPatchSites(baseModule, smaa, "Anti-Aliasing");

        // Motion blur shutter ratio and max blur length, the DERQ2 equivalent of the pair the DERQ plugin overrides via
        // xmm1. Once signatures exist these should be mid-hooks feeding RS.MotionBlurPreset, not NOPs.
        static constexpr PatchSite motionBlurTweaks[] = {
            { "Motion Blur Shutter Ratio", 0x77D3D2, "F3 43 0F 11 4C 3E 68", "movss [r14+r15+68],xmm1", "F3 43 0F 11 4C 3E 68", PatchAction::Document, 7 },
            { "Motion Blur Max Blur Length", 0x77D430, "F3 43 0F 11 4C 3E 6C", "movss [r14+r15+6C],xmm1", "F3 43 0F 11 4C 3E 6C", PatchAction::Document, 7 },
        };
        ApplyPatchSites(baseModule, motionBlurTweaks, "Motion Blur");
    }
} // EnigmaFix
