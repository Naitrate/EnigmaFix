/**
Display Utilities code that is from Lyall's C++ mods (https://github.com/Lyall/). Simply borrowing them here.
At least on the MGSHDFix repo, it's licensed under MIT. So I figured I'd mention that here.

There's some things that I modified or added, such as getting a list of available resolutions.
**/

#include "DisplayHelper.h"
#include <winuser.h>

namespace Util
{
    std::vector<DesktopResolution> GetAvailableDisplayResolutions(int displayIndex) {
        std::vector<DesktopResolution> resolutions;

        DISPLAY_DEVICE displayDevice;
        displayDevice.cb = sizeof(DISPLAY_DEVICE);
        if (!EnumDisplayDevices(nullptr, displayIndex - 1, &displayDevice, 0)) {
            return resolutions;
        }

        std::string primaryMonitorName = displayDevice.DeviceName;

        DEVMODE devMode{};
        devMode.dmSize = sizeof(DEVMODE);
        int i = 0;

        while (EnumDisplaySettings(primaryMonitorName.c_str(), i++, &devMode)) {
            DesktopResolution res = {
                static_cast<int>(devMode.dmPelsWidth),
                static_cast<int>(devMode.dmPelsHeight)
            };

            if (std::find(resolutions.begin(), resolutions.end(), res) == resolutions.end()) {
                resolutions.push_back(res);
            }
        }
        return resolutions;
    }

    DesktopResolution GetCurrentDisplayResolution(int displayIndex) {
        DISPLAY_DEVICE displayDevice = { .cb = sizeof(DISPLAY_DEVICE) };

        if (EnumDisplayDevices(nullptr, displayIndex - 1, &displayDevice, 0)) {
            DEVMODE devMode = { .dmSize = sizeof(DEVMODE) };

            if (EnumDisplaySettings(displayDevice.DeviceName, ENUM_CURRENT_SETTINGS, &devMode))
                return { static_cast<int>(devMode.dmPelsWidth), static_cast<int>(devMode.dmPelsHeight) };
        }
        //if (DEVMODE devMode{ .dmSize = sizeof(DEVMODE) }; EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode))
            //return { static_cast<int>(devMode.dmPelsWidth), static_cast<int>(devMode.dmPelsHeight) };
        return {}; // Returns {0, 0} by default
    }

    float GetDPIScaleForWindow(HWND hwnd) {
        if (!hwnd)
            return 1.0f; // Default DPI scale

        const auto dpi = static_cast<float>(GetDpiForWindow(hwnd));
        return dpi / 96.0f; // 96 DPI is the default scale (1.0)
    }
}
