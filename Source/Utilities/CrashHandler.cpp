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
#include "CrashHandler.h"

// System Libraries
#include <windows.h>
#include <psapi.h>
#include <cstdio>

// Third Party Libraries
#include "spdlog/spdlog.h"

namespace EnigmaFix::CrashHandler {

    namespace {
        PVOID vehHandle = nullptr;
        LONG  alreadyReported = 0;

        // Only these are worth reporting. Everything else -- C++ throws, the breakpoints a debugger plants, the
        // first-chance noise Wine generates -- goes straight back to normal handling untouched.
        bool IsFatal(const DWORD code)
        {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION:
                case EXCEPTION_ILLEGAL_INSTRUCTION:
                case EXCEPTION_PRIV_INSTRUCTION:
                case EXCEPTION_STACK_OVERFLOW:
                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                case EXCEPTION_IN_PAGE_ERROR:
                case EXCEPTION_DATATYPE_MISALIGNMENT:
                    return true;
                default:
                    return false;
            }
        }

        const char* CodeName(const DWORD code)
        {
            switch (code) {
                case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
                case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
                case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
                case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
                case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
                default:                              return "UNKNOWN";
            }
        }

        // Turns an absolute address into "Module.exe+RVA", which is the form a disassembler wants. Falls back to the
        // raw address when it belongs to no module the loader knows about, which usually means allocated trampoline
        // memory -- worth knowing on its own, since that is what a bad hook looks like.
        std::string DescribeAddress(const void* address)
        {
            char buffer[MAX_PATH + 64] = {};
            HMODULE module = nullptr;
            // Wine's GetModuleHandleExA happily claims a module owns address zero, which then reports as a wildly
            // negative RVA. Anything below the module base is not in that module, so reject it.
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCSTR>(address), &module) && module != nullptr
                && reinterpret_cast<uintptr_t>(address) >= reinterpret_cast<uintptr_t>(module)) {
                char modulePath[MAX_PATH] = {};
                if (GetModuleFileNameA(module, modulePath, MAX_PATH) != 0) {
                    const char* moduleName = strrchr(modulePath, '\\');
                    moduleName = (moduleName != nullptr) ? moduleName + 1 : modulePath;
                    const auto rva = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module);
                    snprintf(buffer, sizeof(buffer), "%s+0x%llX (absolute 0x%llX)", moduleName,
                             static_cast<unsigned long long>(rva),
                             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
                    return buffer;
                }
            }
            snprintf(buffer, sizeof(buffer), "0x%llX (not inside any loaded module)",
                     static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address)));
            return buffer;
        }

        LONG CALLBACK OnException(EXCEPTION_POINTERS* info)
        {
            if (info == nullptr || info->ExceptionRecord == nullptr) { return EXCEPTION_CONTINUE_SEARCH; }
            const auto* record = info->ExceptionRecord;
            if (!IsFatal(record->ExceptionCode)) { return EXCEPTION_CONTINUE_SEARCH; }

            // Report the first fatal exception only. A crash tends to cascade, and the first one is the useful one.
            if (InterlockedExchange(&alreadyReported, 1) != 0) { return EXCEPTION_CONTINUE_SEARCH; }

            spdlog::critical("================ EnigmaFix crash report ================");
            spdlog::critical("Exception {} (0x{:08X}) at {}", CodeName(record->ExceptionCode),
                             record->ExceptionCode, DescribeAddress(record->ExceptionAddress));

            if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
                const char* verb = record->ExceptionInformation[0] == 0 ? "reading"
                                 : record->ExceptionInformation[0] == 1 ? "writing"
                                                                        : "executing";
                spdlog::critical("Faulted while {} address 0x{:X}", verb,
                                 static_cast<unsigned long long>(record->ExceptionInformation[1]));
            }

            if (const CONTEXT* ctx = info->ContextRecord) {
                spdlog::critical("RIP={} RSP=0x{:X} RBP=0x{:X}", DescribeAddress(reinterpret_cast<void*>(ctx->Rip)),
                                 static_cast<unsigned long long>(ctx->Rsp), static_cast<unsigned long long>(ctx->Rbp));
                spdlog::critical("RAX=0x{:X} RBX=0x{:X} RCX=0x{:X} RDX=0x{:X}",
                                 static_cast<unsigned long long>(ctx->Rax), static_cast<unsigned long long>(ctx->Rbx),
                                 static_cast<unsigned long long>(ctx->Rcx), static_cast<unsigned long long>(ctx->Rdx));
                spdlog::critical("RSI=0x{:X} RDI=0x{:X} R8=0x{:X} R9=0x{:X}",
                                 static_cast<unsigned long long>(ctx->Rsi), static_cast<unsigned long long>(ctx->Rdi),
                                 static_cast<unsigned long long>(ctx->R8),  static_cast<unsigned long long>(ctx->R9));

                // A proper unwind needs dbghelp, which is unreliable under Wine. Scanning the stack for values that
                // land inside a loaded module's code is cruder but works anywhere, and it is enough to see which of
                // our hooks or which engine function was on the way in.
                spdlog::critical("---- Stack scan (possible return addresses) ----");
                const auto* stack = reinterpret_cast<const uintptr_t*>(ctx->Rsp);
                int reported = 0;
                for (int i = 0; i < 256 && reported < 24; ++i) {
                    if (IsBadReadPtr(stack + i, sizeof(uintptr_t))) { break; }
                    const auto candidate = stack[i];
                    if (candidate < 0x10000) { continue; }
                    HMODULE owner = nullptr;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCSTR>(candidate), &owner) && owner != nullptr) {
                        spdlog::critical("  [rsp+0x{:X}] {}", i * sizeof(uintptr_t),
                                         DescribeAddress(reinterpret_cast<void*>(candidate)));
                        ++reported;
                    }
                }
            }
            spdlog::critical("========================================================");
            spdlog::default_logger()->flush();

            return EXCEPTION_CONTINUE_SEARCH;  // Let the game handle it as it normally would.
        }
    }

    void Init()
    {
        if (vehHandle != nullptr) { return; }
        // 1 puts this first in line, so the report is written before anything else gets a chance to swallow it.
        vehHandle = AddVectoredExceptionHandler(1, OnException);
        if (vehHandle != nullptr) { spdlog::info("Crash handler installed."); }
        else { spdlog::error("Failed to install the crash handler."); }
    }

    void Shutdown()
    {
        if (vehHandle != nullptr) {
            RemoveVectoredExceptionHandler(vehHandle);
            vehHandle = nullptr;
        }
    }
}
