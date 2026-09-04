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

#ifndef PATCH_SITE_H
#define PATCH_SITE_H

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace EnigmaFix {

    // What to do once a patch site has been located.
    enum class PatchAction {
        Document,      // Nothing is applied. The site is recorded so the data needed to derive a signature isn't lost.
        NopBytes,      // Remove the instruction outright. Uses PatchSite::ByteCount.
        ForceALZero,   // Zero AL just before a "mov [reg+reg+disp],al", so the engine's own store writes 0.
        ForceALOne,    // As above, but stores 1.
        ForceEAXZero,  // Zero EAX just before a "mov [reg+reg+disp],eax".
    };

    // One patch lifted from a Cheat Engine table.
    //
    // The Death end re;Quest table recorded an IDA signature next to almost every address, so those patches ported
    // straight across to Memory::PatternScan. The DERQ2, Dragon Star Varnir and Neptunia Virtual Stars tables only
    // recorded absolute offsets. The instruction bytes on their own are nowhere near unique across an executable --
    // most of these sites are a plain "mov [reg+reg+disp],al" that recurs throughout the post processing code -- so
    // scanning for them would silently patch the wrong address and crash in ways that look like working code.
    //
    // Each site therefore carries everything needed to derive a signature from the binary (the CE offset to seek to,
    // the original bytes to confirm you're in the right place, and the decoded instruction) with Signature left empty
    // until that work is done. A site with no signature is logged and skipped; it never guesses at an address.
    //
    // To activate a site: derive a pattern that matches exactly once, drop it into Signature, and set Action to
    // whatever the Cheat Engine script did. CeOffset is documentation only and is never used to patch.
    struct PatchSite {
        const char* Name;
        std::uintptr_t CeOffset;    // "Game.exe"+X in the build the table was made against.
        const char* OriginalBytes;  // Bytes at the site, for deriving a signature and sanity checking it.
        const char* Instruction;    // What those bytes decode to.
        const char* Signature;      // Empty until derived from the binary and verified to match exactly once.
        PatchAction Action;
        std::size_t ByteCount;      // Bytes covered by the instruction. Used by NopBytes.
    };

    // Applies one site, returning false when it was skipped. Skipping is the expected path for a site that hasn't had
    // its signature derived yet, so that case is logged at warn rather than error.
    bool ApplyPatchSite(HMODULE baseModule, const PatchSite& site, const char* groupName);

    // Applies a whole table of sites and logs a summary of how many actually landed.
    void ApplyPatchSites(HMODULE baseModule, const PatchSite* sites, std::size_t count, const char* groupName);

    template <std::size_t N>
    void ApplyPatchSites(HMODULE baseModule, const PatchSite (&sites)[N], const char* groupName) {
        ApplyPatchSites(baseModule, sites, N, groupName);
    }

    // Scans for a string literal and NOPs it out. Used to skip opening movies: the engine moves on when a path won't
    // resolve. This is content addressed rather than offset addressed, so it needs no signature work to be safe.
    bool NOPStringLiteral(HMODULE baseModule, const std::string& text, const char* groupName);

} // EnigmaFix

#endif // PATCH_SITE_H
