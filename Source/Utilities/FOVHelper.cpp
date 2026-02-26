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
#define _USE_MATH_DEFINES
#include "FOVHelper.h"

namespace FOV {
    float AdjustFOVHorPlusToVertPlus(float aspectRatio, float horizontalFOV, float aspectRatioThreshold) {
        spdlog::info("FOV Helper: Window Resized! Changing FOV Scaling...");

        if (aspectRatio < aspectRatioThreshold) {
            // For aspect ratios narrower than 16:9, keep the horizontal FOV, expand vertical FOV
            auto horizontalFovRad = horizontalFOV * static_cast<float>(M_PI) / 180.0f;
            auto verticalFovRad = 2.0f * std::atan(std::tan(horizontalFovRad / 2.0f) * (aspectRatioThreshold / aspectRatio));
            auto verticalFovDeg = verticalFovRad * 180.0f / static_cast<float>(M_PI);
            return verticalFovDeg;
        }
        // For aspect ratios wider than 16:9, keep the horizontal FOV
        return horizontalFOV;
    }

} // FOV