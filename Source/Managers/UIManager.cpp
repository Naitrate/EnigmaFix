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

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-security"
#endif

#define NOMINMAX

// Internal Functionality
#include "UIManager.h"
#include "PluginManager.h"
#include "PatchManager.h"
#include "ConfigManager.h"
#include "../Settings/PlayerSettings.h"
#include "../Localization/Localization.h"
#include "../Utilities/UITextureLoader.h"
#include "../Utilities/DisplayHelper.h"
// System Libraries
#include <d3d11.h>
// Third Party Libraries
#include <imgui_internal.h>

#include "imgui.h"
#include "../Utilities/ColorHelper.h"
#include "../Utilities/JsonHelper.h"


using namespace ImGui;
using namespace EnigmaFix;

bool initLoc = false;
bool startupNotice = false;
bool exitPrompt = false;
bool aboutPage = false;

// ImGui Style
ImGuiStyle* style;

// Test Variables
bool PressedSave = false;
bool TestFullResPP = false;
static const char* ShadowOptions[]{ "Very Low (512)", "Low (1024)", "Medium (2048)", "High (4096)", "Ultra (8192)", "Extreme (16384)" };
static int ShadowOptionResolution[]{ 512, 1024, 2048, 4096, 8192, 16384 };
int ShadowResolution;

static const char* SSAOQualityOptions[]{ "Quarter Res", "Half Res", "Full" };
static int SSAOQualityDivider[]{ 4, 2, 1 };
int SelectedSSAOOption = 2;

static const char* SSRQualityOptions[]{ "Quarter Res", "Half Res", "Full" };
static int SSRQualityDivider[]{ 4, 2, 1 };
int SelectedSSROption = 2;

int SelectedShadowOption = 2;
static const char* InputOptions[]{ "Auto", "Xbox", "PlayStation", "Switch" };

int LogoWidth  = 0;
int LogoHeight = 0;
ID3D11ShaderResourceView* Logo = nullptr;


// Singleton References
auto& LocUI = EnigmaFix::Localization::Get();
auto& ConManUI = EnigmaFix::ConfigManager::Get();
auto& SettingsUI = EnigmaFix::PlayerSettings::Get();
// Singleton Instance
EnigmaFix::UIManager EnigmaFix::UIManager::um_Instance;

namespace EnigmaFix {

    void UIManager::ShowStartupOverlay(bool* p_open)
    {
        const float PAD = 10.0f / (SettingsUI.INS.dpiScale * SettingsUI.INS.dpiScaleMultiplier);
        static int corner = 0;
        if (corner != -1) {
            const ImGuiViewport* viewport = GetMainViewport();
            ImVec2 work_pos = viewport->WorkPos; // Use work area to avoid menu-bar/task-bar, if any!
            ImVec2 work_size = viewport->WorkSize;
            ImVec2 window_pos, window_pos_pivot;
            window_pos.x = (corner & 1) ? (work_pos.x + work_size.x - PAD) : (work_pos.x + PAD);
            window_pos.y = (corner & 2) ? (work_pos.y + work_size.y - PAD) : (work_pos.y + PAD);
            window_pos_pivot.x = (corner & 1) ? 1.0f : 0.0f;
            window_pos_pivot.y = (corner & 2) ? 1.0f : 0.0f;
            SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        }
        if (::Begin("Startup Overlay", p_open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove))
        {
            Text(LocUI.Strings.startupWindowWelcome.c_str());
            Text(LocUI.Strings.startupWindowInputPrompt.c_str());
            Separator();
            Text(LocUI.Strings.startupWindowCrackWarning.c_str());
        }
        End();
    }

    void UIManager::ShowAboutWindow(bool* p_open)
    {
        if (::Begin(LocUI.Strings.aboutEnigmaFix.c_str(), p_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
            float dpiScaleLogo = SettingsUI.INS.dpiScale / 100.0f * SettingsUI.INS.dpiScaleMultiplier;
            int logoSizeDivider = 8;

            float logoWidthNew = (static_cast<float>(LogoWidth) / static_cast<float>(logoSizeDivider)) * dpiScaleLogo;

            // Save the current cursor position
            float originalCursorPosX = GetCursorPosX();

            // Calculate the horizontal center position and set the cursor position
            float windowWidth = GetWindowWidth();
            float imageXPos = (windowWidth - logoWidthNew) * 0.5f;  // Center the image horizontally
            SetCursorPosX(imageXPos);

            Image(reinterpret_cast<ImTextureID>(Logo), ImVec2((static_cast<float>(LogoWidth) / static_cast<float>(logoSizeDivider)) * dpiScaleLogo, (static_cast<float>(LogoHeight) / static_cast<float>(logoSizeDivider)) * dpiScaleLogo));

            // Restore the cursor position after the image
            SetCursorPosX(originalCursorPosX);

            // Calculate the text width (this will give you the length of the text in pixels)
            float textWidth = CalcTextSize(LocUI.Strings.enigmaFixName.c_str()).x + CalcTextSize(LocUI.Strings.versionNumber.c_str()).x + 10.0f; // 10.0f for a small space between the two strings

            // Set the cursor to the center of the window minus half the text width
            SetCursorPosX((windowWidth - textWidth) * 0.5f);

            // Render the centered text
            Text("%s %s", LocUI.Strings.enigmaFixName, LocUI.Strings.versionNumber.c_str());
            Separator();
            Text("(c) 2026 Bryce Q.");
            Text(LocUI.Strings.enigmaFixLicense.c_str());
            Text(LocUI.Strings.donations.c_str());
            Separator();
            Text(LocUI.Strings.specialThanksTo.c_str());
            Text(LocUI.Strings.specialThanksLine1.c_str());
            Text(LocUI.Strings.specialThanksLine2.c_str());
            Text(LocUI.Strings.specialThanksLine3.c_str());
            PushTextWrapPos(GetWindowWidth() * 0.8f);  // Wrap text at 80% of the window width
            Text(LocUI.Strings.alsoSpecialThanksToLine.c_str());
            Text(LocUI.Strings.specialThanksLine4.c_str());
            Separator();
            Text(LocUI.Strings.thirdPartySoftware.c_str());
            Text(LocUI.Strings.imguiLicense.c_str());
            Text(LocUI.Strings.kieroHookLicense.c_str());
            Text(LocUI.Strings.ghFearLicense.c_str());
            Text(LocUI.Strings.stbLicense.c_str());
            Text(LocUI.Strings.jsonLicense.c_str());
            Text(LocUI.Strings.inippLicense.c_str());
            Text(LocUI.Strings.thirteenAGAsiLicense.c_str());
            Text(LocUI.Strings.safetyHookLicense.c_str());
            Text(LocUI.Strings.zydisLicense.c_str());
            Text(LocUI.Strings.fontsLicense.c_str());
            Text(LocUI.Strings.fontAwesomeLicense.c_str());
            PopTextWrapPos();  // Reset word wrap
            Separator();
            if (Button(LocUI.Strings.button_Close.c_str())) {
                aboutPage = false;
            }
        }
        End();
    }

    void UIManager::HelpMarker(const char* desc)
    {
        TextDisabled("(?)");
        if (IsItemHovered()) {
            BeginTooltip();
            PushTextWrapPos(GetFontSize() * 35.0f);
            TextUnformatted(desc);
            PopTextWrapPos();
            EndTooltip();
        }
    }

    void UIManager::InitLocalization()
    {
        if (!initLoc) {
            // Initialize localization strings.
            LocUI.InitLoc();
            initLoc = true; // Confirms that these values have been initialized.
        }
    }

    ImVec4 VectorToVec4(std::vector<float> Vec){
        Vec.resize(4);
        return {Vec[0], Vec[1], Vec[2], Vec[3]};
    }

    void UIManager::ActivateTheme()
    {
        // Window and Border theming
        ImVec4* colors           = style->Colors;
        // TODO: Move this style (not the color stuff) to the DPI Scaling settings. That way we can ensure consistency.
        style->FrameRounding     = 2.0f;
        style->FrameBorderSize   = 1.0f;
        style->PopupBorderSize   = 2.0f;
        style->PopupBorderSize   = 1.0f;
        style->WindowRounding    = 6.0f;
        style->PopupRounding     = 6.0f;
        style->ScrollbarRounding = 6.0f;
        //UIProperties Color;
        json theme;

        // Color Scheme
        switch (SettingsUI.GameMode) { // I should find out how to put this in a struct instead and just have it access a pointer that switches the reference based on the game mode.
            case PlayerSettings::DERQ: { // Death end re;Quest
                if (theme = JsonUtils::LoadJson("Resources/Themes/DERQ.json"); theme == nullptr) {
                    throw std::runtime_error("Failed to load DERQ Theme JSON.");
                }
                break;
            }
            case PlayerSettings::DERQ2: { // Death end re;Quest 2
                if (theme = JsonUtils::LoadJson("Resources/Themes/DERQ2.json"); theme == nullptr) {
                    throw std::runtime_error("Failed to load DERQ2 Theme JSON.");
                }
                break;
            }
            default: { // Misc
                if (theme = JsonUtils::LoadJson("Resources/Themes/MISC.json"); theme == nullptr) {
                    throw std::runtime_error("Failed to load MISC Theme JSON.");
                }
                break;
            }
        }

        // Finally grab our theme color values from our parsed JSON file.
        colors[ImGuiCol_Text] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_Text"]));
        colors[ImGuiCol_TextDisabled] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TextDisabled"]));
        colors[ImGuiCol_WindowBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_WindowBg"]));
        colors[ImGuiCol_ChildBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ChildBg"]));
        colors[ImGuiCol_PopupBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_PopupBg"]));
        colors[ImGuiCol_Border] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_Border"]));
        colors[ImGuiCol_BorderShadow] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_BorderShadow"]));
        colors[ImGuiCol_FrameBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_FrameBg"]));
        colors[ImGuiCol_FrameBgHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_FrameBgHovered"]));
        colors[ImGuiCol_FrameBgActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_FrameBgActive"]));
        colors[ImGuiCol_TitleBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TitleBg"]));
        colors[ImGuiCol_TitleBgActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TitleBgActive"]));
        colors[ImGuiCol_TitleBgCollapsed] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TitleBgCollapsed"]));
        colors[ImGuiCol_MenuBarBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_MenuBarBg"]));
        colors[ImGuiCol_ScrollbarBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ScrollbarBg"]));
        colors[ImGuiCol_ScrollbarGrab] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ScrollbarGrab"]));
        colors[ImGuiCol_ScrollbarGrabHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ScrollbarGrabHovered"]));
        colors[ImGuiCol_ScrollbarGrabActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ScrollbarGrabActive"]));
        colors[ImGuiCol_CheckMark] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_CheckMark"]));
        colors[ImGuiCol_SliderGrab] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_SliderGrab"]));
        colors[ImGuiCol_SliderGrabActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_SliderGrabActive"]));
        colors[ImGuiCol_Button] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_Button"]));
        colors[ImGuiCol_ButtonHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ButtonHovered"]));
        colors[ImGuiCol_ButtonActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ButtonActive"]));
        colors[ImGuiCol_Header] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_Header"]));
        colors[ImGuiCol_HeaderHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_HeaderHovered"]));
        colors[ImGuiCol_HeaderActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_HeaderActive"]));
        colors[ImGuiCol_Separator] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_Separator"]));
        colors[ImGuiCol_SeparatorHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_SeparatorHovered"]));
        colors[ImGuiCol_SeparatorActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_SeparatorActive"]));
        colors[ImGuiCol_ResizeGrip] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ResizeGrip"]));
        colors[ImGuiCol_ResizeGripHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ResizeGripHovered"]));
        colors[ImGuiCol_ResizeGripActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ResizeGripActive"]));
        colors[ImGuiCol_Tab] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_Tab"]));
        colors[ImGuiCol_TabHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TabHovered"]));
        colors[ImGuiCol_TabActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TabActive"]));
        colors[ImGuiCol_TabUnfocused] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TabUnfocused"]));
        colors[ImGuiCol_TabUnfocusedActive] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TabUnfocusedActive"]));
        colors[ImGuiCol_PlotLines] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_PlotLines"]));
        colors[ImGuiCol_PlotLinesHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_PlotLinesHovered"]));
        colors[ImGuiCol_PlotHistogram] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_PlotHistogram"]));
        colors[ImGuiCol_PlotHistogramHovered] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_PlotHistogramHovered"]));
        colors[ImGuiCol_TableHeaderBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TableHeaderBg"]));
        colors[ImGuiCol_TableBorderStrong] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TableBorderStrong"]));
        colors[ImGuiCol_TableBorderLight] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TableBorderLight"]));
        colors[ImGuiCol_TableRowBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TableRowBg"]));
        colors[ImGuiCol_TableRowBgAlt] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TableRowBgAlt"]));
        colors[ImGuiCol_TextSelectedBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_TextSelectedBg"]));
        colors[ImGuiCol_DragDropTarget] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_DragDropTarget"]));
        colors[ImGuiCol_NavHighlight] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_NavHighlight"]));
        colors[ImGuiCol_NavWindowingHighlight] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_NavWindowingHighlight"]));
        colors[ImGuiCol_NavWindowingDimBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_NavWindowingDimBg"]));
        colors[ImGuiCol_ModalWindowDimBg] = VectorToVec4(Utils::HexToRGBA(theme["ImGuiCol_ModalWindowDimBg"]));
    }

    void UIManager::ResolutionOptions()
    {
        // Grab the available display resolutions, and then the current resolution used by the PlayerSettings.
        std::vector<Util::DesktopResolution> resolutions = Util::GetAvailableDisplayResolutions();
        auto currentResolution = SettingsUI.RES.Resolution;
        // Check if currentResolution is in the list
        if (std::ranges::find(resolutions, currentResolution) == resolutions.end()) {
            resolutions.push_back(currentResolution); // Add it if it's not in the list
        }
        // Sort the resolutions from smallest to largest based on area (width * height)
        std::ranges::sort(resolutions, [](const Util::DesktopResolution& a, const Util::DesktopResolution& b) {
            return (a.x * a.y) < (b.x * b.y);  // Compare by resolution area
        });
        // Convert resolution list to ImGui-friendly format
        std::vector<std::string> resolutionStrings;
        for (const auto& [x, y] : resolutions) {
            resolutionStrings.push_back(std::to_string(x) + "x" + std::to_string(y));
        }
        // Convert to a format Combo can use
        std::vector<const char*> resolutionCStrs;
        for (const auto& str : resolutionStrings) {
            resolutionCStrs.push_back(str.c_str());
        }
        // Set the selected resolution index based on current resolution
        int selectedResolutionOption = -1;  // Default to an invalid index
        for (int i = 0; i < resolutionStrings.size(); ++i) {
            if (resolutionStrings[i] == std::to_string(currentResolution.x) + "x" + std::to_string(currentResolution.y)) {
                selectedResolutionOption = i;  // Set the selected resolution index
                break;
            }
        }
        Combo(LocUI.Strings.combobox_CustomResolution.c_str(), &selectedResolutionOption, resolutionCStrs.data(), static_cast<int>(resolutionCStrs.size()));
    }

    void UIManager::MainMenuOptions()
    {
        std::string res = std::string("\xef\x89\xac ") + LocUI.Strings.collapsingHeader_Resolution;
        if (CollapsingHeader(res.c_str()), ImGuiTreeNodeFlags_Leaf) {
            ResolutionOptions();
            SameLine(); HelpMarker(LocUI.Strings.helpmarker_CustomResolution.c_str());
            // NOTE: Planning on phasing out the custom resolution checkbox. It's only here currently for debugging issues with crashing on startup.
            //Checkbox(LocUI.Strings.checkbox_UseCustomRes, &SettingsUI.RES.UseCustomRes);
            //if (SettingsUI.RES.UseCustomRes)
            //{
            //}
            Checkbox(LocUI.Strings.checkbox_UseCustomResScale.c_str(), &SettingsUI.RES.UseCustomResScale);
            if (SettingsUI.RES.UseCustomResScale) {
                // TODO: Figure out why this doesn't work.
                DragInt(LocUI.Strings.dragInt_CustomResScale.c_str(), &SettingsUI.RES.CustomResScale, 25, 100);
                SameLine(); HelpMarker(LocUI.Strings.helpmarker_CustomResScale.c_str());
            }
            Text("Internal Resolution: %d x %d", static_cast<int>(SettingsUI.INS.InternalResolution.x), static_cast<int>(SettingsUI.INS.InternalResolution.y));
        }
        std::string fov = std::string("\xef\x81\xae ") + LocUI.Strings.collapsingHeader_Fov;
        if (CollapsingHeader(fov.c_str()), ImGuiTreeNodeFlags_Leaf) {
            Checkbox(LocUI.Strings.checkbox_customFOV.c_str(), &SettingsUI.FOV.UseCustomFOV);
            if (SettingsUI.FOV.UseCustomFOV) {
                SliderInt(LocUI.Strings.sliderInt_customFOV.c_str(), &SettingsUI.FOV.FieldOfView, 44, 90);
                SameLine(); HelpMarker(LocUI.Strings.helpmarker_customFOV.c_str());
                //fovPatch(SettingsUI.FOV.FieldOfView);
            }
            else {

            }
        }
        std::string framerate = std::string("\xef\x8f\xbd ") + LocUI.Strings.collapsingHeader_Framerate;
        if (CollapsingHeader(framerate.c_str()), ImGuiTreeNodeFlags_Leaf)
        {
            constexpr auto framerateBad = ImVec4(1.0f, 0.7f, 0.7f, 1.0f);
            constexpr auto framerateGood = ImVec4(0.7f, 1.0f, 0.7f, 1.0f);
            Checkbox(LocUI.Strings.checkbox_VSync.c_str(), &SettingsUI.SYNC.VSync);
            SameLine(); HelpMarker(LocUI.Strings.helpmarker_VSync.c_str());
            if (SettingsUI.SYNC.VSync) {
                SliderInt(LocUI.Strings.sliderInt_syncInterval.c_str(), &SettingsUI.SYNC.SyncInterval, 1, 4);
                SameLine(0.0, -1.0); HelpMarker(LocUI.Strings.helpmarker_syncInterval.c_str());
            }
            else { SettingsUI.SYNC.SyncInterval = 0; }
            InputInt(LocUI.Strings.sliderInt_FramerateCap.c_str(), &SettingsUI.SYNC.MaxFPS, 1, 100, 0);
            SettingsUI.SYNC.MaxFPS = ImClamp(SettingsUI.SYNC.MaxFPS, 0, std::numeric_limits<int>::max()); // Clamp to prevent anything below 0 from being set.
            SameLine(0.0, -1.0); HelpMarker(LocUI.Strings.helpmarker_FramerateCap.c_str());
            // Adds the Framerate and Frametime counter and changes the color of the text based on if the framerate is < 60FPS (red), or >= 60FPS (green).
            // Since I can't put this in a Switch/Case statement apparently, the YandereDev-tier "IF/ELSE" statement shenanigans will have to do for now.
            if (GetIO().Framerate >= 60.0f) {
                Text(LocUI.Strings.text_Frametime.c_str()); SameLine(); TextColored(framerateGood, "%.3f ms", 1000.0f / GetIO().Framerate);
                Text(LocUI.Strings.text_Framerate.c_str()); SameLine(); TextColored(framerateGood, "%.1f FPS", GetIO().Framerate);
            }
            else {
                Text(LocUI.Strings.text_Frametime.c_str()); SameLine(); TextColored(framerateBad, "%.3f ms", 1000.0f / GetIO().Framerate);
                Text(LocUI.Strings.text_Framerate.c_str()); SameLine(); TextColored(framerateBad, "%.1f FPS", GetIO().Framerate);
            }
        }
        std::string rendering = std::string("\xef\x87\xbc ") + LocUI.Strings.collapsingHeader_Rendering;
        if (CollapsingHeader(rendering.c_str()), ImGuiTreeNodeFlags_Leaf)
        {
            // Shadow quality below is real -- it drives the shadow render target resize in RenderManager. Turning
            // shadows off entirely is not, since no plugin patches RS.Shadows, so only the checkbox is greyed out.
            BeginDisabled();
            Checkbox(LocUI.Strings.checkbox_ShadowRendering.c_str(), &SettingsUI.RS.Shadows);
            EndDisabled();
            if (SettingsUI.RS.Shadows) {
                Combo(LocUI.Strings.combobox_ShadowQuality.c_str(), &SelectedShadowOption, ShadowOptions, IM_ARRAYSIZE(ShadowOptions));
                SameLine();
                HelpMarker(LocUI.Strings.helpmarker_ShadowQuality.c_str());
            }
            Checkbox(LocUI.Strings.checkbox_SSAO.c_str(), &SettingsUI.RS.SSAO);
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_SSAO.c_str());
            if (SettingsUI.RS.SSAO) {
                // Not localised yet, and restart-gated for the same reason as the TAA resolve: shaders are built once.
                static const char* aoModes[] = { "Engine", "GTAO (restart)" };
                Combo("Ambient Occlusion Mode", &SettingsUI.RS.SSAOMode, aoModes, IM_ARRAYSIZE(aoModes));
                SameLine();
                HelpMarker("Replaces the engine's screen space AO with Ground Truth Ambient Occlusion, a horizon "
                           "search over the depth buffer using the GBuffer's octahedral normals. Needs a restart.");
                if (SettingsUI.RS.SSAOMode == 1) {
                    SliderInt("AO Radius", &SettingsUI.RS.SSAORadius, 10, 300);
                    SameLine();
                    HelpMarker("Sampling radius in world units. The scene's near plane is 10 and mid-scene depths "
                               "run into the thousands, so this is not a 0-1 value. Also needs a restart.");
                    SliderInt("AO Intensity", &SettingsUI.RS.SSAOIntensity, 25, 400);
                    SameLine();
                    HelpMarker("Power applied to the visibility term. 100 is unmodified ground truth; higher "
                               "darkens. Needs a restart.");
                }
            }
            // Greyed out because no plugin implements it yet. There is no Edge Rendering signature in Plugin_DERQ, so
            // the checkbox moved a value nothing reads. Drop the BeginDisabled pair once a patch exists.
            BeginDisabled();
            Checkbox(LocUI.Strings.checkbox_CharacterOutlines.c_str(), &SettingsUI.RS.EdgeRendering);
            EndDisabled();
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_CharacterOutlines.c_str());
            Checkbox(LocUI.Strings.checkbox_GI.c_str(), &SettingsUI.RS.IBL);
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_GI.c_str());
            Checkbox(LocUI.Strings.checkbox_DoF.c_str(), &SettingsUI.RS.DepthOfField);
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_DoF.c_str());
            Checkbox(LocUI.Strings.checkbox_TAA.c_str(), &SettingsUI.RS.TAA);
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_TAA.c_str());
            // Not localised yet, and deliberately plain text while this is being evaluated. Unlike the toggles above
            // this one is genuinely live: the jitter is rewritten in the constant buffer every frame, so it can be
            // switched on and off while looking at the same scene.
            if (SettingsUI.RS.TAA) {
                Checkbox("Temporal AA Jitter (experimental)", &SettingsUI.RS.TAAJitter);
                SameLine();
                HelpMarker("The engine runs its temporal AA without any subpixel jitter, so it blurs without ever "
                           "resolving extra detail. This drives the jitter offset the shaders already read with an "
                           "8 phase Halton sequence, which turns it into real temporal supersampling.");
                Checkbox("Replace Temporal AA Resolve (restart)", &SettingsUI.RS.TAAReplaceResolve);
                SameLine();
                HelpMarker("Swaps the engine's temporal AA resolve for one with a Catmull-Rom history fetch and no "
                           "centre-tap bypass, which is what makes the jitter and sharpness settings do anything at "
                           "all. Shaders are only built once, so this needs a restart to take effect.");
                if (SettingsUI.RS.TAAJitter) {
                    SliderInt("Temporal AA Sharpness", &SettingsUI.RS.TAASharpness, 0, 100);
                    SameLine();
                    HelpMarker("Narrows the resolve's reconstruction filter. The engine's is a Gaussian of 0.534 "
                               "pixels, which is wide once the jitter phases are accumulating. 0 keeps the engine's "
                               "width; 100 narrows it to 0.6x. Push it too far and fine detail will start to sparkle.");
                }
            }
            // Same as Character Outlines: no signature exists for foliage yet, so this one is greyed out too.
            BeginDisabled();
            Checkbox(LocUI.Strings.checkbox_Foliage.c_str(), &SettingsUI.RS.FoliageRendering);
            EndDisabled();
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_Foliage.c_str());
        }
        std::string texturing = std::string("\xef\x87\xbc ") + std::string("Texturing");
        if (CollapsingHeader(texturing.c_str()), ImGuiTreeNodeFlags_Leaf)
        {
            // Not localised yet. Sampler states are built once at startup, so both of these need a restart.
            static const char* afLevels[] = { "Engine default", "2x", "4x", "8x", "16x" };
            static const int   afValues[] = { 0, 2, 4, 8, 16 };
            int afIndex = 0;
            for (int i = 0; i < IM_ARRAYSIZE(afValues); ++i) {
                if (afValues[i] == SettingsUI.RS.AnisotropicFiltering) { afIndex = i; }
            }
            if (Combo("Anisotropic Filtering (restart)", &afIndex, afLevels, IM_ARRAYSIZE(afLevels))) {
                SettingsUI.RS.AnisotropicFiltering = afValues[afIndex];
            }
            SameLine();
            HelpMarker("Forces anisotropic filtering on the game's texture samplers. Shadow comparison samplers and "
                       "the point samplers the post processing chain uses for exact texel fetches are left alone.");

            SliderInt("Texture LOD Bias (restart)", &SettingsUI.RS.TextureLODBias, -20, 0);
            SameLine();
            HelpMarker("In tenths of a mip level; negative sharpens distant textures. Only worth using with temporal "
                       "AA enabled, since without it a negative bias trades blur for shimmer. -5 to -10 is typical.");

            Checkbox("Pillarbox UI to 16:9", &SettingsUI.RS.PillarboxUI);
            SameLine();
            HelpMarker("Confines the 2D UI to a centred 16:9 box rather than letting it stretch across the whole "
                       "screen. Mouse driven menus are not remapped yet, so the cursor will not line up with what "
                       "it is pointing at while this is on.");
        }
        std::string input = std::string("\xef\x84\x9b ") + LocUI.Strings.collapsingHeader_Input;
        if (CollapsingHeader(input.c_str()), ImGuiTreeNodeFlags_Leaf) {
            Combo(LocUI.Strings.combobox_ControlType.c_str(), &SettingsUI.IS.InputDeviceType, InputOptions, IM_ARRAYSIZE(InputOptions));
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_ControlType.c_str());
            // Add something here for KB/M prompts if that ever becomes a thing, due to the complexities of remapping, and other circumstances.
        }
        std::string misc = std::string("\xef\x81\x99 ") + LocUI.Strings.collapsingHeader_Misc;
        if (CollapsingHeader(misc.c_str()), ImGuiTreeNodeFlags_Leaf) {
            Checkbox(LocUI.Strings.checkbox_SkipOP.c_str(), &SettingsUI.MS.SkipOpeningVideos);
            Checkbox(LocUI.Strings.checkbox_CameraTweaks.c_str(), &SettingsUI.MS.CameraTweaks);
            SameLine();
            HelpMarker(LocUI.Strings.helpmarker_CameraTweaks.c_str());
        }
        std::string launcher = std::string("\xef\x8b\x90 ") + LocUI.Strings.collapsingHeader_Misc;
        if (CollapsingHeader(launcher.c_str()), ImGuiTreeNodeFlags_Leaf) {
            Checkbox(LocUI.Strings.checkbox_IgnoreUpdates.c_str(), &SettingsUI.LS.IgnoreUpdates);
        }
    }

    void UIManager::WindowButtons()
    {
        if (Button(LocUI.Strings.button_Save.c_str())) {
            // Saving used to raise the startup notice as a stand-in while SaveConfig() was a stub that wrote nothing,
            // so pressing Save appeared to do nothing except show the welcome prompt. SaveConfig() writes now, and the
            // notice belongs to startup rather than to this button.
            ConManUI.SaveConfig();
        }
        SameLine();
        if (Button(LocUI.Strings.button_About.c_str())) { aboutPage = true; }
        SameLine();
        if (Button(LocUI.Strings.button_Close.c_str())) {
            //showUI = false;
            // Always center this window when appearing
            const auto center = GetMainViewport()->GetCenter();
            SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            OpenPopup(LocUI.Strings.gameExit.c_str());
        }
    }

    void UIManager::ShowExitPrompt()
    {
        Text(LocUI.Strings.exitTextPrompt.c_str());
        Separator();
        PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        PopStyleVar();
        if (Button(LocUI.Strings.exitOK.c_str(), ImVec2(120, 0))) {
            exit(0);
        }
        SetItemDefaultFocus();
        SameLine();
        if (Button(LocUI.Strings.exitCancel.c_str(), ImVec2(120, 0))) {
            CloseCurrentPopup();
        }
        EndPopup();
    }

    void UIManager::LoopChecks()
    {
        if (startupNotice) { ShowStartupOverlay(&startupNotice); }
        if (aboutPage) { ShowAboutWindow(&aboutPage); }
        if (BeginPopupModal(LocUI.Strings.gameExit.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
            ShowExitPrompt();
        }
        ShadowResolution = ShadowOptionResolution[SelectedShadowOption];
    }
    
    bool scaled = false;
    void UIManager::AdjustDPIScaling(float scale_factor = 1.0f)
    {
        if (!scaled) {
            style->WindowPadding.x                  = ImTrunc(style->WindowPadding.x * scale_factor);
            style->WindowPadding.y                  = ImTrunc(style->WindowPadding.y * scale_factor);
            style->WindowRounding                   = ImTrunc(style->WindowRounding * scale_factor);
            style->WindowMinSize.x                  = ImTrunc(style->WindowMinSize.x * scale_factor);
            style->WindowMinSize.y                  = ImTrunc(style->WindowMinSize.y * scale_factor);
            style->ChildRounding                    = ImTrunc(style->ChildRounding * scale_factor);
            style->PopupRounding                    = ImTrunc(style->PopupRounding * scale_factor);
            style->FramePadding.x                   = ImTrunc(style->FramePadding.x * scale_factor);
            style->FramePadding.y                   = ImTrunc(style->FramePadding.y * scale_factor);
            style->FrameRounding                    = ImTrunc(style->FrameRounding * scale_factor);
            style->ItemSpacing.x                    = ImTrunc(style->ItemSpacing.x * scale_factor);
            style->ItemSpacing.y                    = ImTrunc(style->ItemSpacing.x * scale_factor);
            style->ItemInnerSpacing.x               = ImTrunc(style->ItemInnerSpacing.x * scale_factor);
            style->ItemInnerSpacing.y               = ImTrunc(style->ItemInnerSpacing.y * scale_factor);
            style->CellPadding.x                    = ImTrunc(style->CellPadding.x * scale_factor);
            style->CellPadding.y                    = ImTrunc(style->CellPadding.y * scale_factor);
            style->TouchExtraPadding.x              = ImTrunc(style->TouchExtraPadding.x * scale_factor);
            style->TouchExtraPadding.y              = ImTrunc(style->TouchExtraPadding.y * scale_factor);
            style->IndentSpacing                    = ImTrunc(style->IndentSpacing * scale_factor);
            style->ColumnsMinSpacing                = ImTrunc(style->ColumnsMinSpacing * scale_factor);
            style->ScrollbarSize                    = ImTrunc(style->ScrollbarSize * scale_factor);
            style->ScrollbarRounding                = ImTrunc(style->ScrollbarRounding * scale_factor);
            style->GrabMinSize                      = ImTrunc(style->GrabMinSize * scale_factor);
            style->GrabRounding                     = ImTrunc(style->GrabRounding * scale_factor);
            style->LogSliderDeadzone                = ImTrunc(style->LogSliderDeadzone * scale_factor);
            style->TabRounding                      = ImTrunc(style->TabRounding * scale_factor);
            style->TabCloseButtonMinWidthSelected   = ImTrunc(style->TabCloseButtonMinWidthSelected * scale_factor);
            style->TabCloseButtonMinWidthUnselected = ImTrunc(style->TabCloseButtonMinWidthUnselected * scale_factor);
            style->SeparatorTextPadding.x           = ImTrunc(style->SeparatorTextPadding.x * scale_factor);
            style->SeparatorTextPadding.y           = ImTrunc(style->SeparatorTextPadding.y * scale_factor);
            style->DisplayWindowPadding.x           = ImTrunc(style->DisplayWindowPadding.x * scale_factor);
            style->DisplayWindowPadding.y           = ImTrunc(style->DisplayWindowPadding.y * scale_factor);
            style->DisplaySafeAreaPadding.x         = ImTrunc(style->DisplaySafeAreaPadding.x * scale_factor);
            style->DisplaySafeAreaPadding.y         = ImTrunc(style->DisplaySafeAreaPadding.y * scale_factor);
            style->MouseCursorScale                 = ImTrunc(style->MouseCursorScale * scale_factor);
            scaled = true;
        }
        
    }

    void UIManager::ShowMainMenu(bool* p_open)
    {
        if (Begin(LocUI.Strings.gameName.c_str(), p_open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            // Creates the main menu UI and enables our custom theming.
            ActivateTheme();
            // Spawns the main menu logic.
            MainMenuOptions();
            Separator();
            WindowButtons();
        }
    }

    void UIManager::LoadLogoTexture(ID3D11Device* pDevice) // TODO: Fix this not loading the texture properly.
    {
        if (!Logo) { // Prevent reloading each frame.
            // TODO: Figure out why the texture is missing.
            const auto ret = LoadTextureFromFile("Resources/EnigmaFix_Logo.png", &Logo, pDevice, &LogoWidth, &LogoHeight);
            IM_ASSERT(ret);
        }
    }

    void UIManager::BeginRender()
    {
        ShowMainMenu(&SettingsUI.ShowUI); // showMainMenu is fine on it's own, we just need a boolean that actually works
        // Runs some menu checks per-frame, since I seemingly can't get it so these checks don't happen per-frame.
        LoopChecks();
    }

    void UIManager::Start(ID3D11Device* pDevice)
    {
        // First, get our ImGui style before we do anything else.
        style = &GetStyle();
        // TODO: Figure out how to adjust the style settings only when necessary. Right now it keeps writing to them and causes problems.
        AdjustDPIScaling(SettingsUI.INS.dpiScale / 100.0f * SettingsUI.INS.dpiScaleMultiplier);
        // Checks if the localization strings have been initialized.
        InitLocalization();
        // Loads the About screen's logo texture.
        LoadLogoTexture(pDevice);
        // Begins drawing the UI.
        BeginRender();
    }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
