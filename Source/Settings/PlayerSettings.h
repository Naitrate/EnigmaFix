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

#ifndef ENIGMAFIX_PLAYERSETTINGS_H
#define ENIGMAFIX_PLAYERSETTINGS_H

// Internal Functionality
#include "../Utilities/DisplayHelper.h"

namespace EnigmaFix {
    class PlayerSettings {
    public:
        static PlayerSettings& Get() {
            return ps_Instance;
        }

        enum E_GameMode {
            DERQ,
            DERQ2,
            Varnir,
            VIIR,
            NVS,
            MS2,
            MSF
        };

        E_GameMode GameMode = (E_GameMode)0; // Sets the Game Mode to DERQ by default.

        bool ShowUI         = false;
        bool ShowEFUI       = false;
        bool ShowDevConsole = false;

        struct ResolutionSettings {
            // Resolution Related Settings
            bool UseCustomRes         = true;
            bool UseCustomResScale    = true;
            int  CustomResScale       = 100;  // Eventually will be used for upscaling/supersampling support.
            Util::DesktopResolution Resolution = { 1920, 1080 };
            //int  HorizontalRes        = 1920; // Used for Render Target/Viewport/Scissor Region Resizing
            //int  VerticalRes          = 1080; // Used for Render Target/Viewport/Scissor Region Resizing
            float InternalAspectRatio = 16.0f / 9.0f; // Used for RenderManager to dynamically adjust the rendering aspect ratio.
        };

        struct FOVSettings {
            bool UseCustomFOV    = false;
            bool AdaptiveFOVScaling = true; // Enables Vert+ scaling below 16:9 aspect ratios.
            int  FieldOfView     = 44;   // This will probably need to be tuned on a per-game basis. At least for DERQ, the FOV should be fine at 44-45 degrees.
        };

        struct SyncSettings {
            bool VSync        = false;
            int  MaxFPS       = 0;
            int  SyncInterval = 1;
            double TargetFrameTime = 1.0 / MaxFPS;
        };

        struct RenderingSettings {
            bool Bloom                     = true;
            bool CameraDistortion          = true;
            bool ColorCorrection           = true;
            bool DepthOfField              = true;
            bool EdgeRendering             = true; // Toggle for character outline rendering (It's also called Edge Rendering by the engine)
            bool Fog                       = true;
            bool FoliageRendering          = true; // An eventual toggle for when I can find a way to toggle foliage rendering on/off.
            bool IBL                       = true;
            bool LensFlare                 = true;
            bool MotionBlur                = false;
            bool RLRLighting               = true;
            bool Shadows                   = true;
            bool SSAO                      = true;
            bool SSR                       = true;
            bool TAA                       = true; // Eventually need to find a way to either do a velocity buffer, and/or redo the entire TAA pipeline from scratch (so something like DLSS, FSR, or TAAU can be implemented)
            bool Tonemapping               = true;
            bool Vignette                  = true;
            int  MotionBlurPreset          = 1;    // Ideally, I want a few different presets for exposure settings (short, medium, long) that can be chosen based on preference and the framerate target.
            int  ScreenSpaceEffectsDivider = 2;    // Gets divided by the SSAO and SSR logic to determine if SSAO and SSR should be full-res or not.
            int  ShadowRes                 = 2048; // Used for Render Target Resizing
            int  SSAOScaleDivider          = 2;
            int  SSRScaleDivider           = 2;
        };

        struct InputSettings {
            bool DisableSteamInput = false;
            bool KBMPrompts        = false;
            int  InputDeviceType   = 0;     // Auto, Xbox, PlayStation, Switch (Eventually, I want to find a way to hack in SteamInput support or KB/M prompts, but not for now.
        };

        struct MiscSettings {
            bool CameraTweaks      = true; // Enables functionality that improves the vertical clamping range of camera movement, alongside other things.
            bool SkipOpeningVideos = true; // Self Explanatory
            bool EnableConsoleLog  = true; // Shows a debug window with logs when enabled.
        };

        struct LauncherSettings {
            bool IgnoreUpdates = false;
        };

        struct InternalSettings {
            Util::DesktopResolution InternalResolution = { 1920, 1080 };
            float dpiScale = 100.0f;
            float dpiScaleMultiplier = 1.0f; // This is just for debugging DPI scaling.
        };

        struct ResolutionSettings RES {};
        struct FOVSettings FOV {};
        struct SyncSettings SYNC {};
        struct RenderingSettings RS {};
        struct InputSettings IS {};
        struct MiscSettings MS {};
        struct LauncherSettings LS {};
        struct InternalSettings INS {};

    private:
        PlayerSettings() {}
        static PlayerSettings ps_Instance;
    };
}

#endif //ENIGMAFIX_PLAYERSETTINGS_H
