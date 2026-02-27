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

#ifndef ENIGMAFIX_FRAMERATEMANAGER_H
#define ENIGMAFIX_FRAMERATEMANAGER_H

// System Libraries
#include <windows.h>

namespace EnigmaFix {
    class FramerateManager {
    public:
        static FramerateManager& Get() {
            return frm_Instance;
        }
        void Update();
        int Limit();
        void Init();
        double deltaTime = 0;
        double msTime = 0;
        float fps = 0;
        float timeOffset = 0;

        bool loop = true;
        LARGE_INTEGER currentTime;
        LONG lastTime;
        LARGE_INTEGER frequency; // Frequency of the performance counter

        double targetFrameTime;
    private:
        FramerateManager() {}
        static FramerateManager frm_Instance;

        double originalFrTarget = 1.0f / 60.0f;
    };
}

#endif //ENIGMAFIX_FRAMERATEMANAGER_H
