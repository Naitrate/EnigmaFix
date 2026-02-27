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
#include "Localization.h"
#include "../Settings/PlayerSettings.h"
#include "tchar.h"
#include "imgui.h"
#include "../Utilities/JsonHelper.h"
// System Libraries
#include <fstream>
#include <iostream>
#include <windows.h>
// Variables
auto& PlayerSettingsLoc = EnigmaFix::PlayerSettings::Get();
// Singleton Instance
EnigmaFix::Localization EnigmaFix::Localization::loc_Instance;

using json = nlohmann::json;

namespace EnigmaFix
{
    void Localization::InitFont(float scale_factor) {
        language = 0; // Dummy Variable for testing language support.
        ImGuiIO& io = ImGui::GetIO();
        // Makes an ImFontConfig for our main fonts.
        ImFontConfig font_Config;
        // Loads some Windows English, Japanese, and Traditional Chinese fonts. Note that these locales need to be installed in order to use these.
        io.Fonts->AddFontFromFileTTF("Resources\\Fonts\\NotoSans-Regular.ttf", fontSize * scale_factor, &font_Config, io.Fonts->GetGlyphRangesDefault()); // Loads the font for English strings first.
        // Uses a switch statement instead of loading all of these at once, to prevent even worse massive hitching from happening. It's already bad enough with just JP and CN fonts.
        switch (language) {
            case 1: { // Japanese
                io.Fonts->AddFontFromFileTTF("Resources\\Fonts\\NotoSansJP-Regular.otf", fontSize * scale_factor, &font_Config, io.Fonts->GetGlyphRangesJapanese());
                break;
            }
            case 2: { // Traditional Chinese
                io.Fonts->AddFontFromFileTTF("Resources\\Fonts\\NotoSansTC-Regular.otf", fontSize * scale_factor, &font_Config, io.Fonts->GetGlyphRangesChineseFull());
                break;
            }
            default: break;
        }
        // Adds FontAwesome font support to ImGui, so icons can be used in the User Interface.
        ImFontConfig icons_config;
        icons_config.MergeMode = true;
        icons_config.PixelSnapH = true;
        icons_config.GlyphMinAdvanceX = fontSize;
        static const ImWchar icon_ranges[] = { 0xe005, 0xf8ff, 0 };
        // Finally adds the FontAwesome fonts to the available fonts for ImGui to pick from.
        io.Fonts->AddFontFromFileTTF("Resources\\Fonts\\fa-solid-900.ttf", fontSize * scale_factor, &icons_config, icon_ranges);
        io.Fonts->Build(); // Build the font atlas, which seemingly fixes our hitching issue from earlier when using Japanese or Chinese fonts.
    }

    void Localization::LoadStringsFromJson(json loadedJSON)
    {
        switch (PlayerSettingsLoc.GameMode) {
            case PlayerSettings::DERQ: { // Death end re;Quest
                Strings.gameName = loadedJSON["gameNames"]["DERQ"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["DERQ"].get<std::string>();
                break;
            }
            case PlayerSettings::DERQ2: { // Death end re;Quest 2
                Strings.gameName = loadedJSON["gameNames"]["DERQ2"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["DERQ2"].get<std::string>();
                break;
            }
            case PlayerSettings::Varnir: { // Dragon Star Varnir
                Strings.gameName = loadedJSON["gameNames"]["Varnir"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["Varnir"].get<std::string>();
                break;
            }
            case PlayerSettings::VIIR: { // Neptunia VIIR
                Strings.gameName = loadedJSON["gameNames"]["VIIR"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["VIIR"].get<std::string>();
                break;
            }
            case PlayerSettings::NVS: { // Neptunia Virtual Stars
                Strings.gameName = loadedJSON["gameNames"]["NVS"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["NVS"].get<std::string>();
                break;
            }
            case PlayerSettings::MS2: { // Mary Skelter 2
                Strings.gameName = loadedJSON["gameNames"]["MS2"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["MS2"].get<std::string>();
                break;
            }
            case PlayerSettings::MSF: { // Mary Skelter Finale
                Strings.gameName = loadedJSON["gameNames"]["MSF"].get<std::string>();
                Strings.startupWindowWelcome = loadedJSON["startupWindowWelcome"]["MSF"].get<std::string>();
                break;
            }
        }
        Strings.enigmaFixName = loadedJSON["commonStrings"]["enigmaFixName"].get<std::string>();
        Strings.aboutEnigmaFix = loadedJSON["commonStrings"]["aboutEnigmaFix"].get<std::string>();
        Strings.versionNumber = loadedJSON["commonStrings"]["versionNumber"].get<std::string>();
        Strings.enigmaFixLicense = loadedJSON["commonStrings"]["enigmaFixLicense"].get<std::string>();
        Strings.donations = loadedJSON["commonStrings"]["donations"].get<std::string>();
        Strings.specialThanksTo = loadedJSON["commonStrings"]["specialThanksTo"].get<std::string>();
        Strings.specialThanksLine1 = loadedJSON["commonStrings"]["specialThanksLine1"].get<std::string>();
        Strings.specialThanksLine2 = loadedJSON["commonStrings"]["specialThanksLine2"].get<std::string>();
        Strings.specialThanksLine3 = loadedJSON["commonStrings"]["specialThanksLine3"].get<std::string>();
        Strings.alsoSpecialThanksToLine = loadedJSON["commonStrings"]["alsoSpecialThanksToLine"].get<std::string>();
        Strings.specialThanksLine4 = loadedJSON["commonStrings"]["specialThanksLine4"].get<std::string>();
        Strings.thirdPartySoftware = loadedJSON["commonStrings"]["thirdPartySoftware"].get<std::string>();
        Strings.imguiLicense = loadedJSON["commonStrings"]["imguiLicense"].get<std::string>();
        Strings.kieroHookLicense = loadedJSON["commonStrings"]["kieroHookLicense"].get<std::string>();
        Strings.ghFearLicense = loadedJSON["commonStrings"]["ghFearLicense"].get<std::string>();
        Strings.stbLicense = loadedJSON["commonStrings"]["stbLicense"].get<std::string>();
        Strings.jsonLicense = loadedJSON["commonStrings"]["jsonLicense"].get<std::string>();
        Strings.inippLicense = loadedJSON["commonStrings"]["inippLicense"].get<std::string>();
        Strings.thirteenAGAsiLicense = loadedJSON["commonStrings"]["thirteenAGAsiLicense"].get<std::string>();
        Strings.safetyHookLicense = loadedJSON["commonStrings"]["safetyHookLicense"].get<std::string>();
        Strings.zydisLicense = loadedJSON["commonStrings"]["zydisLicense"].get<std::string>();
        Strings.fontsLicense = loadedJSON["commonStrings"]["fontsLicense"].get<std::string>();
        Strings.fontAwesomeLicense = loadedJSON["commonStrings"]["fontAwesomeLicense"].get<std::string>();
        Strings.button_Close = loadedJSON["commonStrings"]["button_Close"].get<std::string>();
        Strings.collapsingHeader_Resolution = loadedJSON["uiStrings"]["collapsingHeader_Resolution"].get<std::string>();
        Strings.checkbox_UseCustomRes = loadedJSON["uiStrings"]["checkbox_UseCustomRes"].get<std::string>();
        Strings.combobox_CustomResolution = loadedJSON["uiStrings"]["combobox_CustomResolution"].get<std::string>();
        Strings.helpmarker_CustomResolution = loadedJSON["uiStrings"]["helpmarker_CustomResolution"].get<std::string>();
        Strings.checkbox_UseCustomResScale = loadedJSON["uiStrings"]["checkbox_UseCustomResScale"].get<std::string>();
        Strings.dragInt_CustomResScale = loadedJSON["uiStrings"]["dragInt_CustomResScale"].get<std::string>();
        Strings.helpmarker_CustomResScale = loadedJSON["uiStrings"]["helpmarker_CustomResScale"].get<std::string>();
        Strings.collapsingHeader_Fov = loadedJSON["uiStrings"]["collapsingHeader_Fov"].get<std::string>();
        Strings.checkbox_customFOV = loadedJSON["uiStrings"]["checkbox_customFOV"].get<std::string>();
        Strings.sliderInt_customFOV = loadedJSON["uiStrings"]["sliderInt_customFOV"].get<std::string>();
        Strings.helpmarker_customFOV = loadedJSON["uiStrings"]["helpmarker_customFOV"].get<std::string>();
        Strings.collapsingHeader_Framerate = loadedJSON["uiStrings"]["collapsingHeader_Framerate"].get<std::string>();
        Strings.checkbox_VSync = loadedJSON["uiStrings"]["checkbox_VSync"].get<std::string>();
        Strings.helpmarker_VSync = loadedJSON["uiStrings"]["helpmarker_VSync"].get<std::string>();
        Strings.sliderInt_syncInterval = loadedJSON["uiStrings"]["sliderInt_syncInterval"].get<std::string>();
        Strings.helpmarker_syncInterval = loadedJSON["uiStrings"]["helpmarker_syncInterval"].get<std::string>();
        Strings.sliderInt_FramerateCap = loadedJSON["uiStrings"]["sliderInt_FramerateCap"].get<std::string>();
        Strings.helpmarker_FramerateCap = loadedJSON["uiStrings"]["helpmarker_FramerateCap"].get<std::string>();
        Strings.text_Frametime = loadedJSON["uiStrings"]["text_Frametime"].get<std::string>();
        Strings.text_Framerate = loadedJSON["uiStrings"]["text_Framerate"].get<std::string>();
        Strings.collapsingHeader_Rendering = loadedJSON["uiStrings"]["collapsingHeader_Rendering"].get<std::string>();
        Strings.checkbox_ShadowRendering = loadedJSON["uiStrings"]["checkbox_ShadowRendering"].get<std::string>();
        Strings.combobox_ShadowQuality = loadedJSON["uiStrings"]["combobox_ShadowQuality"].get<std::string>();
        Strings.helpmarker_ShadowQuality = loadedJSON["uiStrings"]["helpmarker_ShadowQuality"].get<std::string>();
        Strings.checkbox_SSAO = loadedJSON["uiStrings"]["checkbox_SSAO"].get<std::string>();
        Strings.helpmarker_SSAO = loadedJSON["uiStrings"]["helpmarker_SSAO"].get<std::string>();
        Strings.combobox_ssaoQuality = loadedJSON["uiStrings"]["combobox_ssaoQuality"].get<std::string>();
        Strings.checkbox_SSR = loadedJSON["uiStrings"]["checkbox_SSR"].get<std::string>();
        Strings.helpmarker_SSR = loadedJSON["uiStrings"]["helpmarker_SSR"].get<std::string>();
        Strings.combobox_ssrQuality = loadedJSON["uiStrings"]["combobox_ssrQuality"].get<std::string>();
        Strings.checkbox_CharacterOutlines = loadedJSON["uiStrings"]["checkbox_CharacterOutlines"].get<std::string>();
        Strings.helpmarker_CharacterOutlines = loadedJSON["uiStrings"]["helpmarker_CharacterOutlines"].get<std::string>();
        Strings.checkbox_GI = loadedJSON["uiStrings"]["checkbox_GI"].get<std::string>();
        Strings.helpmarker_GI = loadedJSON["uiStrings"]["helpmarker_GI"].get<std::string>();
        Strings.checkbox_DoF = loadedJSON["uiStrings"]["checkbox_DoF"].get<std::string>();
        Strings.helpmarker_DoF = loadedJSON["uiStrings"]["helpmarker_DoF"].get<std::string>();
        Strings.checkbox_TAA = loadedJSON["uiStrings"]["checkbox_TAA"].get<std::string>();
        Strings.helpmarker_TAA = loadedJSON["uiStrings"]["helpmarker_TAA"].get<std::string>();
        Strings.checkbox_Foliage = loadedJSON["uiStrings"]["checkbox_Foliage"].get<std::string>();
        Strings.helpmarker_Foliage = loadedJSON["uiStrings"]["helpmarker_Foliage"].get<std::string>();
        Strings.checkbox_MotionBlur = loadedJSON["uiStrings"]["checkbox_MotionBlur"].get<std::string>();
        Strings.helpmarker_MotionBlur = loadedJSON["uiStrings"]["helpmarker_MotionBlur"].get<std::string>();
        Strings.collapsingHeader_Input = loadedJSON["uiStrings"]["collapsingHeader_Input"].get<std::string>();
        Strings.combobox_ControlType = loadedJSON["uiStrings"]["combobox_ControlType"].get<std::string>();
        Strings.helpmarker_ControlType = loadedJSON["uiStrings"]["helpmarker_ControlType"].get<std::string>();
        Strings.collapsingHeader_Misc = loadedJSON["uiStrings"]["collapsingHeader_Misc"].get<std::string>();
        Strings.checkbox_SkipOP = loadedJSON["uiStrings"]["checkbox_SkipOP"].get<std::string>();
        Strings.checkbox_CameraTweaks = loadedJSON["uiStrings"]["checkbox_CameraTweaks"].get<std::string>();
        Strings.helpmarker_CameraTweaks = loadedJSON["uiStrings"]["helpmarker_CameraTweaks"].get<std::string>();
        Strings.collapsingHeader_Launcher = loadedJSON["uiStrings"]["collapsingHeader_Launcher"].get<std::string>();
        Strings.checkbox_IgnoreUpdates = loadedJSON["uiStrings"]["checkbox_IgnoreUpdates"].get<std::string>();
        Strings.button_Save = loadedJSON["uiStrings"]["button_Save"].get<std::string>();
        Strings.button_About = loadedJSON["uiStrings"]["button_About"].get<std::string>();
        Strings.consoleName = loadedJSON["consoleStrings"]["consoleName"].get<std::string>();
        Strings.startupWindowInputPrompt = loadedJSON["startupWindowStrings"]["startupWindowInputPrompt"].get<std::string>();
        Strings.startupWindowCrackWarning = loadedJSON["startupWindowStrings"]["startupWindowCrackWarning"].get<std::string>();
        Strings.gameExit = loadedJSON["exitWindowStrings"]["gameExit"].get<std::string>();
        Strings.exitTextPrompt = loadedJSON["exitWindowStrings"]["exitTextPrompt"].get<std::string>();
        Strings.exitOK = loadedJSON["exitWindowStrings"]["exitOK"].get<std::string>();
        Strings.exitCancel = loadedJSON["exitWindowStrings"]["exitCancel"].get<std::string>();
    }

    void Localization::InitLoc() {
        switch (language) { //checks what Steam's API says, if the game is running in english, japanese, or tchinese (traditional chinese), and then switches strings based on that.
            case 0: { // English
                if (auto enLoc = JsonUtils::LoadJson("Resources/Localization/en.json"); enLoc != nullptr) {
                    LoadStringsFromJson(enLoc);
                }
                else { throw std::runtime_error("Failed to load English localization JSON."); }
                break;
            }
            case 1: { // Japanese
                if (auto jaLoc = JsonUtils::LoadJson("Resources/Localization/ja.json"); jaLoc != nullptr) {
                    LoadStringsFromJson(jaLoc);
                }
                else { throw std::runtime_error("Failed to load Japanese localization JSON."); }
                break;
            }
            case 2: { // Traditional Chinese
                if (auto zhLoc = JsonUtils::LoadJson("Resources/Localization/zh.json"); zhLoc != nullptr) {
                    LoadStringsFromJson(zhLoc);
                }
                else { throw std::runtime_error("Failed to load Traditional Chinese localization JSON."); }
                break;
            }
            default: break;
        }
    }
}