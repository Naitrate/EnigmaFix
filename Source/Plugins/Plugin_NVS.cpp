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
#include "Plugin_NVS.h"
#include "PatchSite.h"
#include "../Settings/PlayerSettings.h"

// Third Party Libraries
#include "spdlog/spdlog.h"

// Ported from NeptuniaVirtualStars.CT. That table recorded no IDA signatures at all, only absolute offsets, so every
// site below is documented rather than applied until a signature can be derived. See PatchSite.h.

auto& PlayerSettingsNVS = EnigmaFix::PlayerSettings::Get();

EnigmaFix::Plugin_NVS EnigmaFix::Plugin_NVS::nvs_Instance;

namespace EnigmaFix
{
    void Plugin_NVS::ResolutionPatches(HMODULE baseModule)
    {
        // The framerate caps are plain floats in the executable rather than code, at +16A3B10 (60 FPS) and +16A3B18
        // (30 FPS). They have no signature to scan for, so reaching them means finding an opcode that reads them.
        // TODO: Locate the reads and hook those instead, the way the DERQ plugin handles its framerate limiter.
        spdlog::debug("Resolution: Neptunia Virtual Stars has no resolution patches ported yet.");
    }

    void Plugin_NVS::UIPatches(HMODULE baseModule)
    {
        // Minimap and UI scaling, the same code path and opcode bytes as DERQ2's pair. The table hardcoded 2580 and
        // 1080, which are the values that place the UI correctly at 21:9 rather than anything general purpose.
        static constexpr PatchSite sites[] = {
            { "UI Scale (Horizontal)", 0x91A610, "8B 00 89 84 24 F0 00 00 00",    "mov eax,[rax] / mov [rsp+000000F0],eax",   "", PatchAction::Document, 9 },
            { "UI Scale (Vertical)",   0x91A621, "8B 40 04 89 84 24 F4 00 00 00", "mov eax,[rax+04] / mov [rsp+000000F4],eax","", PatchAction::Document, 10 },
        };
        ApplyPatchSites(baseModule, sites, "UI");
    }

    void Plugin_NVS::GraphicsSettingsPatches(HMODULE baseModule)
    {
        // All thirteen toggles are a "mov [r14+r15+disp],al" fed from the graphics settings struct, so forcing AL to 0
        // just before the store disables the effect. Depth of field is the one site the table wrote as [r15+r14],
        // which encodes differently, hence the different leading bytes.
        static constexpr PatchSite depthOfField    = { "Depth of Field",       0x976128, "43 88 84 37 54 01 00 00", "mov [r15+r14+00000154],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite glare           = { "Glare",                0x976AAB, "43 88 84 3E F8 00 00 00", "mov [r14+r15+000000F8],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite lensFlare       = { "Anamorphic Lens Flare",0x976F71, "43 88 84 3E 28 01 00 00", "mov [r14+r15+00000128],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite lensDistortion  = { "Lens Distortion",      0x97708D, "43 88 84 3E A0 01 00 00", "mov [r14+r15+000001A0],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite antiAliasing    = { "Anti-Aliasing",        0x977381, "43 88 44 3E 78",          "mov [r14+r15+78],al",       "", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite temporalAA      = { "Temporal AA",          0x9773CD, "43 88 44 3E 79",          "mov [r14+r15+79],al",       "", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite motionBlur      = { "Motion Blur",          0x9778ED, "43 88 44 3E 60",          "mov [r14+r15+60],al",       "", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite ssao            = { "SSAO",                 0x977B5D, "43 88 84 3E A8 01 00 00", "mov [r14+r15+000001A8],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite rlrLighting     = { "RLR Lighting",         0x977FEF, "43 88 84 3E 04 02 00 00", "mov [r14+r15+00000204],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite fog             = { "Fog",                  0x97808C, "43 88 84 3E 90 09 00 00", "mov [r14+r15+00000990],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite eFog            = { "eFog",                 0x978419, "43 88 84 3E E4 09 00 00", "mov [r14+r15+000009E4],al", "", PatchAction::ForceALZero, 8 };

        const struct { const PatchSite& Site; bool Enabled; } toggles[] = {
            { depthOfField,   PlayerSettingsNVS.RS.DepthOfField    },
            { glare,          PlayerSettingsNVS.RS.Bloom           },
            { lensFlare,      PlayerSettingsNVS.RS.LensFlare       },
            { lensDistortion, PlayerSettingsNVS.RS.CameraDistortion},
            { antiAliasing,   PlayerSettingsNVS.RS.TAA             },
            { temporalAA,     PlayerSettingsNVS.RS.TAA             },
            { motionBlur,     PlayerSettingsNVS.RS.MotionBlur      },
            { ssao,           PlayerSettingsNVS.RS.SSAO            },
            { rlrLighting,    PlayerSettingsNVS.RS.RLRLighting     },
            { fog,            PlayerSettingsNVS.RS.Fog             },
            { eFog,           PlayerSettingsNVS.RS.Fog             },
        };
        for (const auto& toggle : toggles) {
            if (!toggle.Enabled) { ApplyPatchSite(baseModule, toggle.Site, "Post Processing"); }
        }

        // No PlayerSettings flag for these yet.
        static constexpr PatchSite unsettable[] = {
            { "Chromatic Aberration", 0x9772CD, "43 88 84 3E 8C 01 00 00", "mov [r14+r15+0000018C],al", "", PatchAction::Document, 8 },
            { "Texture Overlay",      0x978A7E, "43 88 84 3E 9C 0A 00 00", "mov [r14+r15+00000A9C],al", "", PatchAction::Document, 8 },
        };
        ApplyPatchSites(baseModule, unsettable, "Post Processing");
    }
} // EnigmaFix
