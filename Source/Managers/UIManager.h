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

#ifndef ENIGMAFIX_CLION_UIMANAGER_H
#define ENIGMAFIX_CLION_UIMANAGER_H

// System Libraries
#include <d3d11.h>
#include <vector>
#include <string>
#include <iostream>

namespace EnigmaFix {
    class UIManager {
    public:
        static UIManager &Get() {
            return um_Instance;
        };
        static void Start(ID3D11Device* pDevice);
    private:
        static UIManager um_Instance;
        static void BeginRender();
        static void ShowMainMenu(bool* p_open);
        static void ShowStartupOverlay(bool* p_open);
        static void ShowAboutWindow(bool* p_open);
        static void HelpMarker(const char* desc);
        static void InitLocalization();
        static void ActivateTheme();
        static void MainMenuOptions();
        static void LoopChecks();
        static void ShowExitPrompt();
        static void WindowButtons();
        static void LoadLogoTexture(ID3D11Device* pDevice);
        static void ResolutionOptions();
        static void AdjustDPIScaling(float scale_factor);
    };
}

#endif //ENIGMAFIX_CLION_UIMANAGER_H
