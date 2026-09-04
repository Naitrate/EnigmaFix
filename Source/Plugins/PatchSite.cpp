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
#include "PatchSite.h"
#include "../Utilities/MemoryHelper.h"

// Third Party Libraries
#include <deque>
#include <safetyhook.hpp>
#include "spdlog/spdlog.h"

namespace EnigmaFix {

    namespace {
        // Mid hooks have to outlive this function, and safetyhook takes a plain function pointer rather than a
        // closure, so the handlers below are shared and the hook objects are parked here. A deque is used because it
        // never relocates the elements it already holds when it grows.
        std::deque<SafetyHookMid>& MidHookStorage() {
            static std::deque<SafetyHookMid> hooks;
            return hooks;
        }

        // The engine's graphics toggles are all a "mov [reg+reg+disp],al" fed from a settings struct. The Cheat Engine
        // scripts swap the store for an immediate, but that encoding is a byte longer than the original, which is why
        // the tables trampoline out to allocated memory. Forcing the register just before the store is equivalent and
        // stays in place.
        void ForceALZero(SafetyHookContext& ctx)  { ctx.rax &= ~static_cast<uintptr_t>(0xFF); }
        void ForceALOne(SafetyHookContext& ctx)   { ctx.rax = (ctx.rax & ~static_cast<uintptr_t>(0xFF)) | 1; }
        void ForceEAXZero(SafetyHookContext& ctx) { ctx.rax &= ~static_cast<uintptr_t>(0xFFFFFFFF); }
    }

    bool ApplyPatchSite(HMODULE baseModule, const PatchSite& site, const char* groupName)
    {
        if (site.Action == PatchAction::Document) {
            spdlog::debug("{}: '{}' is recorded for reference only. (CE offset: +{:X}, bytes: {}, instruction: {})",
                          groupName, site.Name, site.CeOffset, site.OriginalBytes, site.Instruction);
            return false;
        }

        if (site.Signature == nullptr || *site.Signature == '\0') {
            spdlog::warn("{}: '{}' has no verified signature yet, so it was skipped. Derive one at CE offset +{:X} "
                         "(bytes: {}, instruction: {}).",
                         groupName, site.Name, site.CeOffset, site.OriginalBytes, site.Instruction);
            return false;
        }

        const auto siteAddr = Memory::PatternScan(baseModule, site.Signature);
        if (siteAddr == nullptr) {
            spdlog::error("{}: '{}' signature was not found.", groupName, site.Name);
            return false;
        }
        spdlog::info("{}: Found '{}' at: {}", groupName, site.Name, reinterpret_cast<void*>(siteAddr));

        switch (site.Action) {
            case PatchAction::NopBytes: {
                for (std::size_t i = 0; i < site.ByteCount; ++i) {
                    Memory::Write(reinterpret_cast<uintptr_t>(siteAddr + i), static_cast<uint8_t>(0x90));
                }
                spdlog::info("{}: Patched {} bytes of '{}' with NOPs.", groupName, site.ByteCount, site.Name);
                break;
            }
            case PatchAction::ForceALZero:
            case PatchAction::ForceALOne:
            case PatchAction::ForceEAXZero: {
                const auto handler = site.Action == PatchAction::ForceALZero  ? &ForceALZero
                                   : site.Action == PatchAction::ForceALOne   ? &ForceALOne
                                                                             : &ForceEAXZero;
                MidHookStorage().push_back(safetyhook::create_mid(siteAddr, handler));
                spdlog::info("{}: Hooked '{}'.", groupName, site.Name);
                break;
            }
            default: break;
        }
        return true;
    }

    void ApplyPatchSites(HMODULE baseModule, const PatchSite* sites, const std::size_t count, const char* groupName)
    {
        std::size_t applied = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (ApplyPatchSite(baseModule, sites[i], groupName)) { ++applied; }
        }
        spdlog::info("{}: Applied {} of {} patch sites.", groupName, applied, count);
    }

    bool NOPStringLiteral(HMODULE baseModule, const std::string& text, const char* groupName)
    {
        constexpr char hexDigits[] = "0123456789ABCDEF";
        std::string pattern;
        pattern.reserve(text.size() * 3);
        for (const unsigned char character : text) {
            if (!pattern.empty()) { pattern.push_back(' '); }
            pattern.push_back(hexDigits[character >> 4]);
            pattern.push_back(hexDigits[character & 0x0F]);
        }

        const auto stringAddr = Memory::PatternScan(baseModule, pattern.c_str());
        if (stringAddr == nullptr) {
            spdlog::error("{}: String '{}' was not found.", groupName, text);
            return false;
        }
        for (std::size_t i = 0; i < text.size(); ++i) {
            Memory::Write(reinterpret_cast<uintptr_t>(stringAddr + i), static_cast<uint8_t>(0x90));
        }
        spdlog::info("{}: Blanked '{}' at: {}", groupName, text, reinterpret_cast<void*>(stringAddr));
        return true;
    }

} // EnigmaFix
