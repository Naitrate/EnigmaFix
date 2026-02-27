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

#ifndef ENIGMAFIX_LOCALIZATION_H
#define ENIGMAFIX_LOCALIZATION_H
#include <nlohmann/json.hpp>

namespace EnigmaFix {
    class Localization {
    public:
        static Localization& Get() {
            return loc_Instance;
        }
        // Localization Functionality
        void InitLoc();
        void InitFont(float scale_factor = 1.0f);
        void LoadStringsFromJson(nlohmann::json loadedJSON);
        // Variables
        float fontSize = 18.0;
        int language;
        // Strings
        struct S_Strings {
            // Common Strings
            std::string enigmaFixName;
            std::string gameName;
            // About Window Strings
            std::string aboutEnigmaFix;
            std::string versionNumber;
            std::string enigmaFixLicense;
            std::string donations;
            std::string specialThanksTo;
            std::string specialThanksLine1;
            std::string specialThanksLine2;
            std::string specialThanksLine3;
            std::string alsoSpecialThanksToLine;
            std::string specialThanksLine4;
            std::string thirdPartySoftware;
            std::string imguiLicense;
            std::string kieroHookLicense;
            std::string stbLicense;
            std::string jsonLicense;
            std::string ghFearLicense;
            std::string inippLicense;
            std::string thirteenAGAsiLicense;
            std::string safetyHookLicense;
            std::string zydisLicense;
            std::string fontsLicense;
            std::string fontAwesomeLicense;
            std::string iconFontCppHeadersLicense;
            std::string button_Close;
            // User Interface Strings
            std::string collapsingHeader_Resolution;
            std::string checkbox_UseCustomRes;
            std::string combobox_CustomResolution;
            std::string helpmarker_CustomResolution;
            std::string checkbox_UseCustomResScale;
            std::string dragInt_CustomResScale;
            std::string helpmarker_CustomResScale;
            std::string collapsingHeader_Fov;
            std::string checkbox_customFOV;
            std::string sliderInt_customFOV;
            std::string helpmarker_customFOV;
            std::string checkbox_VSync;
            std::string helpmarker_VSync;
            std::string sliderInt_syncInterval;
            std::string helpmarker_syncInterval;
            std::string collapsingHeader_Framerate;
            std::string sliderInt_FramerateCap;
            std::string helpmarker_FramerateCap;
            std::string text_Frametime;
            std::string text_Framerate;
            std::string collapsingHeader_Rendering;
            std::string checkbox_ShadowRendering;
            std::string combobox_ShadowQuality;
            std::string helpmarker_ShadowQuality;
            std::string checkbox_SSAO;
            std::string helpmarker_SSAO;
            std::string combobox_ssaoQuality;
            std::string helpmarker_ssaoQuality;
            std::string checkbox_SSR;
            std::string helpmarker_SSR;
            std::string helpmarker_ssrQuality;
            std::string combobox_ssrQuality;
            std::string checkbox_CharacterOutlines;
            std::string helpmarker_CharacterOutlines;
            std::string checkbox_GI;
            std::string helpmarker_GI;
            std::string checkbox_DoF;
            std::string helpmarker_DoF;
            std::string checkbox_TAA;
            std::string helpmarker_TAA;
            std::string checkbox_MotionBlur;
            std::string helpmarker_MotionBlur;
            std::string checkbox_Foliage;
            std::string helpmarker_Foliage;
            std::string collapsingHeader_Input;
            std::string combobox_ControlType;
            std::string helpmarker_ControlType;
            std::string collapsingHeader_Misc;
            std::string checkbox_SkipOP;
            std::string checkbox_CameraTweaks;
            std::string helpmarker_CameraTweaks;
            std::string collapsingHeader_Launcher;
            std::string checkbox_IgnoreUpdates;
            std::string button_Save;
            std::string button_About;
            // Developer Console
            std::string consoleName;
            // Startup Overlay Strings
            std::string startupWindowWelcome;
            std::string startupWindowInputPrompt;
            std::string startupWindowCrackWarning;
            // Game Exit Strings
            std::string gameExit;
            std::string exitTextPrompt;
            std::string exitOK;
            std::string exitCancel;
        };
        S_Strings Strings;
    private:
        static Localization loc_Instance;
    };
}



#endif //ENIGMAFIX_LOCALIZATION_H
