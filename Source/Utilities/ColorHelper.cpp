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

#include "ColorHelper.h"

namespace Utils {
    std::vector<float> HexToRGBA(const std::string& hex) // TODO: Figure out why colors look so saturated.
        {
        std::string hexValue = hex;
        if (hexValue[0] == '#') {
            hexValue = hexValue.substr(1);
        }

        // Validate hex value
        if (hexValue.length() != 8) {
            std::cout << "Error: Invalid hex value. Hex value must be in the format RRGGBBAA." << std::endl;
            return {};
        }

        float r, g, b, a;

        try {
            r = static_cast<float>(std::stoi(hexValue.substr(0, 2), nullptr, 16)) / 255.0f;
            g = static_cast<float>(std::stoi(hexValue.substr(2, 2), nullptr, 16)) / 255.0f;
            b = static_cast<float>(std::stoi(hexValue.substr(4, 2), nullptr, 16)) / 255.0f;
            a = static_cast<float>(std::stoi(hexValue.substr(6, 2), nullptr, 16)) / 255.0f;
        }
        catch (const std::exception& e) {
            std::cout << "Error: Invalid hex value. " << e.what() << std::endl;
            return {};
        }

        return { r, g, b, a };
        }
} // ColorHelper