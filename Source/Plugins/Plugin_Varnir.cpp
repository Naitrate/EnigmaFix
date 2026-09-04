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
#include "Plugin_Varnir.h"
#include "PatchSite.h"
#include "../Settings/PlayerSettings.h"

// Third Party Libraries
#include "spdlog/spdlog.h"

// Ported from DragonStarVarnir.CT. That table recorded no IDA signatures at all, only absolute offsets, so every site
// below is documented rather than applied until a signature can be derived. See PatchSite.h.

auto& PlayerSettingsVarnir = EnigmaFix::PlayerSettings::Get();

EnigmaFix::Plugin_Varnir EnigmaFix::Plugin_Varnir::varnir_Instance;

namespace EnigmaFix
{
    void Plugin_Varnir::AspectRatioPatches(HMODULE baseModule)
    {
        // The object aspect ratio, used for save crystals and similar world objects. Identical opcode to the one the
        // DERQ plugin documents as its "object aspect ratio ptr". The table forced 2.388888888888889, which is 21:9.
        static constexpr PatchSite sites[] = {
            { "Object Aspect Ratio", 0x32674B, "8B 87 D0 04 00 00", "mov eax,[rdi+000004D0]", "", PatchAction::Document, 6 },
        };
        ApplyPatchSites(baseModule, sites, "Aspect Ratio");
    }

    void Plugin_Varnir::FOVPatches(HMODULE baseModule)
    {
        if (!PlayerSettingsVarnir.FOV.UseCustomFOV) { return; }

        // Same opcode as DERQ and DERQ2's main FOV write, right down to the struct offset. Once a signature exists,
        // prefer hooking this and overriding xmm0 rather than NOPing, so the FOV stays adjustable at runtime.
        static constexpr PatchSite sites[] = {
            { "FOV Write", 0x32807F, "F3 0F 11 81 C4 04 00 00", "movss [rcx+000004C4],xmm0", "", PatchAction::NopBytes, 8 },
        };
        ApplyPatchSites(baseModule, sites, "FOV");
    }

    void Plugin_Varnir::UIPatches(HMODULE baseModule)
    {
        // The same UI scale code path DERQ and DERQ2 have, and the same opcode bytes.
        static constexpr PatchSite sites[] = {
            { "UI Scale", 0x5B3570, "8B 00 89 84 24 F0 00 00 00", "mov eax,[rax] / mov [rsp+000000F0],eax", "", PatchAction::Document, 9 },
        };
        ApplyPatchSites(baseModule, sites, "UI");
    }

    void Plugin_Varnir::FrameratePatches(HMODULE baseModule)
    {
        // These three redirect delta time reads at a fixed step value, which is how the table kept animation and
        // movement speed sane once the framerate was unlocked. Each one reads a float out of the executable, so
        // porting them properly means hooking the site and supplying our own delta rather than repointing the read.
        static constexpr PatchSite sites[] = {
            { "Delta Time Read 1", 0x3A49B5, "F3 0F 58 25 FB 73 CE 08",    "addss xmm4,[DragonStarVarnir.exe+908BDB8]", "", PatchAction::Document, 8 },
            { "Delta Time Read 2", 0x3B4DA3, "F3 44 0F 10 15 B4 D9 81 00", "movss xmm10,[DragonStarVarnir.exe+BD2760]", "", PatchAction::Document, 9 },
            { "Delta Time Read 3", 0x4760D5, "F3 0F 10 05 0F D7 77 00",    "movss xmm0,[DragonStarVarnir.exe+BF37EC]",  "", PatchAction::Document, 8 },
        };
        ApplyPatchSites(baseModule, sites, "Framerate");
    }

    void Plugin_Varnir::GraphicsSettingsPatches(HMODULE baseModule)
    {
        // Same shape as DERQ2: a block of "mov [reg+reg+disp],al" stores fed from the graphics settings struct, so
        // forcing AL to 0 just before the store disables the effect. The register pairing varies between sites here
        // (r15+rcx, r15+r10, r9+r15, rcx+r15), which is reflected in the original bytes.
        static constexpr PatchSite tonemapping    = { "Tonemapping",     0x60E555, "41 88 84 0F A4 00 00 00", "mov [r15+rcx+000000A4],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite depthOfField   = { "Depth of Field",  0x60E8B2, "43 88 84 39 4C 01 00 00", "mov [r9+r15+0000014C],al",  "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite colorCorrection= { "Color Correction",0x60ED84, "41 88 44 0F 0C",          "mov [r15+rcx+0C],al",       "", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite glare          = { "Glare",           0x60F2D2, "43 88 84 17 F0 00 00 00", "mov [r15+r10+000000F0],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite lensDistortion = { "Lens Distortion", 0x60F924, "41 88 84 0F 94 01 00 00", "mov [r15+rcx+00000194],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite antiAliasing   = { "Anti-Aliasing",   0x60F9E4, "41 88 44 0F 70",          "mov [r15+rcx+70],al",       "", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite temporalAA     = { "Temporal AA",     0x60FA35, "42 88 44 39 71",          "mov [rcx+r15+71],al",       "", PatchAction::ForceALZero, 5 };
        static constexpr PatchSite ssao           = { "SSAO",            0x61018E, "43 88 84 17 9C 01 00 00", "mov [r15+r10+0000019C],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite rlrLighting    = { "RLR Lighting",    0x6106A4, "41 88 84 0F F8 01 00 00", "mov [r15+rcx+000001F8],al", "", PatchAction::ForceALZero, 8 };
        static constexpr PatchSite fog            = { "Fog",             0x610808, "43 88 84 17 84 09 00 00", "mov [r15+r10+00000984],al", "", PatchAction::ForceALZero, 8 };

        const struct { const PatchSite& Site; bool Enabled; } toggles[] = {
            { tonemapping,     PlayerSettingsVarnir.RS.Tonemapping     },
            { depthOfField,    PlayerSettingsVarnir.RS.DepthOfField    },
            { colorCorrection, PlayerSettingsVarnir.RS.ColorCorrection },
            { glare,           PlayerSettingsVarnir.RS.Bloom           },
            { lensDistortion,  PlayerSettingsVarnir.RS.CameraDistortion},
            { antiAliasing,    PlayerSettingsVarnir.RS.TAA             },
            { temporalAA,      PlayerSettingsVarnir.RS.TAA             },
            { ssao,            PlayerSettingsVarnir.RS.SSAO            },
            { rlrLighting,     PlayerSettingsVarnir.RS.RLRLighting     },
            { fog,             PlayerSettingsVarnir.RS.Fog             },
        };
        for (const auto& toggle : toggles) {
            if (!toggle.Enabled) { ApplyPatchSite(baseModule, toggle.Site, "Post Processing"); }
        }

        // Motion blur is the odd one out: the table forces it on rather than off, since the in-game options don't
        // expose it. Left documented because turning it on by default would be a behaviour change, not a port.
        static constexpr PatchSite motionBlur[] = {
            { "Motion Blur Enable", 0x610004, "43 88 44 17 60", "mov [r15+r10+60],al", "", PatchAction::Document, 5 },
        };
        ApplyPatchSites(baseModule, motionBlur, "Motion Blur");

        // No PlayerSettings flag for these yet. Edge and contour rendering share a struct offset and differ only in
        // which call site writes it, so they need separate signatures despite the identical bytes.
        static constexpr PatchSite unsettable[] = {
            { "Anti-Aliasing Method", 0x60FAF3, "41 89 44 0F 78",          "mov [r15+rcx+78],eax",      "", PatchAction::Document, 5 },
            { "Auto Exposure",        0x60E5A9, "41 88 84 0F A6 00 00 00", "mov [r15+rcx+000000A6],al", "", PatchAction::Document, 8 },
            { "Texture Overlay",      0x611366, "41 88 84 37 80 0A 00 00", "mov [r15+rsi+00000A80],al", "", PatchAction::Document, 8 },
            { "Edge Rendering",       0xA7DB23, "41 88 86 43 03 00 00",    "mov [r14+00000343],al",     "", PatchAction::Document, 7 },
            { "Contour Rendering",    0xA7DAFB, "41 88 86 43 03 00 00",    "mov [r14+00000343],al",     "", PatchAction::Document, 7 },
        };
        ApplyPatchSites(baseModule, unsettable, "Post Processing");
    }
} // EnigmaFix
