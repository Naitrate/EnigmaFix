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

#include <windows.h>

#ifndef PLUGIN_DERQ_H
#define PLUGIN_DERQ_H

namespace EnigmaFix {
class Plugin_DERQ {
    public:
        static Plugin_DERQ& Get() {
            return pq_Instance;
        }
        static void ResolutionPatches(HMODULE baseModule);
        static void AspectRatioPatches(HMODULE baseModule);
        static void FOVPatches(HMODULE baseModule);
        static void UIPatches(HMODULE baseModule);
        static void FrameratePatches(HMODULE baseModule);
        static void GraphicsSettingsPatches(HMODULE baseModule);
        static void CameraPatches(HMODULE baseModule);
        static void PhotoModePatches(HMODULE baseModule);
        static void LoggingPatches(HMODULE baseModule);
        static void SchedulerPatches(HMODULE baseModule);
    private:
        Plugin_DERQ() {}
        static Plugin_DERQ pq_Instance;
};
}; // EnigmaFix

#endif //PLUGIN_DERQ_H
