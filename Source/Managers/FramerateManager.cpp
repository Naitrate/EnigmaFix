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
#include "FramerateManager.h"
#include "../Settings/PlayerSettings.h"

auto& PlayerSettingsFrm = EnigmaFix::PlayerSettings::Get();

// Singleton Instance
EnigmaFix::FramerateManager EnigmaFix::FramerateManager::frm_Instance;

namespace EnigmaFix
{
    void FramerateManager::Update()
    {
        QueryPerformanceFrequency(&frequency); // Get the frequency of the performance counter
        while (loop) {
            QueryPerformanceCounter(&currentTime); // Grabs the current time.
            double deltaTimeSeconds = static_cast<double>(currentTime.QuadPart - lastTime) / frequency.QuadPart;
            msTime = deltaTimeSeconds * 1000.0;

            // Avoid division by zero by checking if deltaTimeSeconds is not too small.
            if (deltaTimeSeconds > 0.000001) {
                fps = 1.0 / deltaTimeSeconds; // Corrected framerate calculation
                timeOffset = originalFrTarget / float(deltaTimeSeconds);
            }
            lastTime = currentTime.QuadPart;
        }
    }

    int FramerateManager::Limit()
    {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);

        double elapsedTime = static_cast<double>(currentTime.QuadPart - lastTime) / frequency.QuadPart;
        double remainingTime = targetFrameTime - elapsedTime;

        int totalSleepTime = 0; // Store total sleep time in milliseconds

        if (remainingTime > 0)
        {
            int sleepTime = static_cast<int>(remainingTime * 1000.0); // Convert to milliseconds
            if (sleepTime > 0)
            {
                Sleep(sleepTime);
                totalSleepTime = sleepTime;
            }

            // Fine-tune timing with a spin-wait
            LARGE_INTEGER spinTime;
            do {
                QueryPerformanceCounter(&spinTime);
            } while (static_cast<double>(spinTime.QuadPart - lastTime) / frequency.QuadPart < targetFrameTime);
        }

        // Update lastTime for the next frame
        lastTime = currentTime.QuadPart;

        return totalSleepTime;
    }

    void FramerateManager::Init()
    {
        PlayerSettingsFrm.SYNC.TargetFrameTime = 1.0 / PlayerSettingsFrm.SYNC.MaxFPS;
    }
}
