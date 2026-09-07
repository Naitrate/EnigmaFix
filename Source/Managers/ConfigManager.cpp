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
#include "ConfigManager.h"
#include "../Settings/PlayerSettings.h"
#include "../Utilities/DisplayHelper.h"
// System Libraries
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cctype>
// Third Party Libraries
#include <inipp.h>
#include <spdlog/spdlog.h>
// Variables
auto& PlayerSettingsConf = EnigmaFix::PlayerSettings::Get();
// Namespaces
using namespace std;
// Singleton Instance
EnigmaFix::ConfigManager EnigmaFix::ConfigManager::cm_Instance; // Seemingly need this declared in PlayerSettings.cpp so a bunch of linker errors don't happen.

namespace EnigmaFix {
    inipp::Ini<char> config;
    std::ifstream is("Config.ini");

    void ConfigManager::Init() {
        ifstream configName("Config.ini");
        config.parse(configName);
        config.generate(cout);
        config.default_section(config.sections["Settings"]);
        config.interpolate();

        // inipp has no concept of a trailing comment, so "TextureLODBias = -5   # tenths" parses as the whole string
        // and extract() leaves the setting at its default without complaining. That is a silent wrong answer, and it
        // already cost one test run. Anything after an unquoted #, ; or // is stripped before the values are read.
        for (auto& section : config.sections) {
            for (auto& entry : section.second) {
                std::string& value = entry.second;
                size_t cut = std::string::npos;
                for (size_t i = 0; i < value.size(); ++i) {
                    if (value[i] == '#' || value[i] == ';') { cut = i; break; }
                    if (value[i] == '/' && i + 1 < value.size() && value[i + 1] == '/') { cut = i; break; }
                }
                if (cut == std::string::npos) { continue; }
                value.erase(cut);
                while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) { value.pop_back(); }
            }
        }
    }


    namespace {
        struct ConfigEntry {
            const char* Section;
            const char* Key;
            std::string Value;
        };

        std::string FromBool(const bool value) { return value ? "true" : "false"; }
        std::string FromInt(const int value)   { return std::to_string(value); }

        std::string Trimmed(const std::string& text) {
            const size_t first = text.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) { return {}; }
            const size_t last = text.find_last_not_of(" \t\r\n");
            return text.substr(first, last - first + 1);
        }

        bool EqualsNoCase(const std::string& a, const std::string& b) {
            if (a.size() != b.size()) { return false; }
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
                    return false;
                }
            }
            return true;
        }

        // inipp parses fine but its generate() drops every comment, and Config.ini is mostly comments explaining what
        // the options do. So saving rewrites the file in place instead: existing values are replaced on the lines they
        // already occupy, and any key the file does not have yet is appended under its section. Comments, ordering and
        // alignment all survive, and a hand-edited file stays hand-editable.
        bool WriteConfigPreservingLayout(const std::string& path, const std::vector<ConfigEntry>& entries) {
            std::vector<std::string> lines;
            {
                std::ifstream input(path);
                if (!input) { return false; }
                std::string line;
                while (std::getline(input, line)) {
                    if (!line.empty() && line.back() == '\r') { line.pop_back(); }
                    lines.push_back(line);
                }
            }

            std::vector<bool> written(entries.size(), false);
            std::string       section;

            // Pass one: replace values where the key already exists.
            for (auto& line : lines) {
                const std::string trimmed = Trimmed(line);
                if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
                    section = trimmed.substr(1, trimmed.size() - 2);
                    continue;
                }
                if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || trimmed.front() == ';' || trimmed.front() == '#') {
                    continue;
                }

                const size_t equals = line.find('=');
                if (equals == std::string::npos) { continue; }
                const std::string key = Trimmed(line.substr(0, equals));

                for (size_t i = 0; i < entries.size(); ++i) {
                    if (written[i]) { continue; }
                    if (!EqualsNoCase(section, entries[i].Section) || !EqualsNoCase(key, entries[i].Key)) { continue; }
                    // Keep everything left of the '=' exactly as the user had it, including any alignment padding.
                    line = line.substr(0, equals + 1) + " " + entries[i].Value;
                    written[i] = true;
                    break;
                }
            }

            // Pass two: append whatever the file did not already contain, after the last real line of its section so
            // it lands inside the section rather than after a trailing blank line.
            for (size_t i = 0; i < entries.size(); ++i) {
                if (written[i]) { continue; }

                int insertAt = -1;
                std::string current;
                for (size_t l = 0; l < lines.size(); ++l) {
                    const std::string trimmed = Trimmed(lines[l]);
                    if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
                        current = trimmed.substr(1, trimmed.size() - 2);
                        if (EqualsNoCase(current, entries[i].Section)) { insertAt = static_cast<int>(l); }
                        continue;
                    }
                    if (EqualsNoCase(current, entries[i].Section) && !trimmed.empty()) { insertAt = static_cast<int>(l); }
                }

                const std::string entryLine = std::string(entries[i].Key) + " = " + entries[i].Value;
                if (insertAt < 0) {
                    lines.emplace_back("");
                    lines.emplace_back("[" + std::string(entries[i].Section) + "]");
                    lines.push_back(entryLine);
                }
                else {
                    lines.insert(lines.begin() + insertAt + 1, entryLine);
                }
                written[i] = true;
            }

            std::ofstream output(path, std::ios::trunc);
            if (!output) { return false; }
            for (const auto& line : lines) { output << line << "\n"; }
            return true;
        }
    }

    void ConfigManager::SaveConfig() {
        if (AlreadyReadConfig) {
            spdlog::info("Saving Config...");

            const std::vector<ConfigEntry> entries = {
                { "Resolution",  "UseCustomResolution",       FromBool(PlayerSettingsConf.RES.UseCustomRes) },
                { "Resolution",  "HorizontalResolution",      FromInt(PlayerSettingsConf.RES.Resolution.x) },
                { "Resolution",  "VerticalResolution",        FromInt(PlayerSettingsConf.RES.Resolution.y) },
                { "Resolution",  "UseResolutionScale",        FromBool(PlayerSettingsConf.RES.UseCustomResScale) },
                { "Resolution",  "ResolutionScalePercentage", FromInt(PlayerSettingsConf.RES.CustomResScale) },

                { "FieldOfView", "UseCustomFOV",              FromBool(PlayerSettingsConf.FOV.UseCustomFOV) },
                { "FieldOfView", "FieldOfView",               FromInt(PlayerSettingsConf.FOV.FieldOfView) },
                { "FieldOfView", "UseAdaptiveFOVScaling",     FromBool(PlayerSettingsConf.FOV.AdaptiveFOVScaling) },

                { "Framerate",   "MaxFPS",                    FromInt(PlayerSettingsConf.SYNC.MaxFPS) },
                { "Framerate",   "VSync",                     FromBool(PlayerSettingsConf.SYNC.VSync) },
                { "Framerate",   "SyncInterval",              FromInt(PlayerSettingsConf.SYNC.SyncInterval) },

                { "Rendering",   "CameraDistortion",          FromBool(PlayerSettingsConf.RS.CameraDistortion) },
                { "Rendering",   "EdgeRendering",             FromBool(PlayerSettingsConf.RS.EdgeRendering) },
                { "Rendering",   "ColorCorrection",           FromBool(PlayerSettingsConf.RS.ColorCorrection) },
                { "Rendering",   "DepthOfField",              FromBool(PlayerSettingsConf.RS.DepthOfField) },
                { "Rendering",   "Fog",                       FromBool(PlayerSettingsConf.RS.Fog) },
                { "Rendering",   "Foliage",                   FromBool(PlayerSettingsConf.RS.FoliageRendering) },
                { "Rendering",   "Bloom",                     FromBool(PlayerSettingsConf.RS.Bloom) },
                { "Rendering",   "IBL",                       FromBool(PlayerSettingsConf.RS.IBL) },
                { "Rendering",   "LensFlare",                 FromBool(PlayerSettingsConf.RS.LensFlare) },
                { "Rendering",   "MotionBlur",                FromBool(PlayerSettingsConf.RS.MotionBlur) },
                { "Rendering",   "MotionBlurPreset",          FromInt(PlayerSettingsConf.RS.MotionBlurPreset) },
                { "Rendering",   "RLRLighting",               FromBool(PlayerSettingsConf.RS.RLRLighting) },
                { "Rendering",   "Shadows",                   FromBool(PlayerSettingsConf.RS.Shadows) },
                { "Rendering",   "ShadowResolution",          FromInt(PlayerSettingsConf.RS.ShadowRes) },
                { "Rendering",   "SSAO",                      FromBool(PlayerSettingsConf.RS.SSAO) },
                { "Rendering",   "SSAOMode",                  PlayerSettingsConf.RS.SSAOMode == 1 ? "GTAO" : "Engine" },
                { "Rendering",   "SSAORadius",                FromInt(PlayerSettingsConf.RS.SSAORadius) },
                { "Rendering",   "SSAOIntensity",             FromInt(PlayerSettingsConf.RS.SSAOIntensity) },
                { "Rendering",   "SSR",                       FromBool(PlayerSettingsConf.RS.SSR) },
                { "Rendering",   "TAA",                       FromBool(PlayerSettingsConf.RS.TAA) },
                { "Rendering",   "TAAJitter",                 FromBool(PlayerSettingsConf.RS.TAAJitter) },
                { "Rendering",   "TAASharpness",              FromInt(PlayerSettingsConf.RS.TAASharpness) },
                { "Rendering",   "TAAReplaceResolve",         FromBool(PlayerSettingsConf.RS.TAAReplaceResolve) },
                { "Rendering",   "AnisotropicFiltering",      FromInt(PlayerSettingsConf.RS.AnisotropicFiltering) },
                { "Rendering",   "TextureLODBias",            FromInt(PlayerSettingsConf.RS.TextureLODBias) },
                { "Rendering",   "FixMinimapScissor",         FromBool(PlayerSettingsConf.RS.FixMinimapScissor) },
                { "Rendering",   "Tonemapping",               FromBool(PlayerSettingsConf.RS.Tonemapping) },
                { "Rendering",   "Vignette",                  FromBool(PlayerSettingsConf.RS.Vignette) },

                { "Input",       "KBMPrompts",                FromBool(PlayerSettingsConf.IS.KBMPrompts) },
                { "Input",       "DisableSteamInput",         FromBool(PlayerSettingsConf.IS.DisableSteamInput) },

                { "Misc",        "SkipOpeningVideos",         FromBool(PlayerSettingsConf.MS.SkipOpeningVideos) },
                { "Misc",        "CameraTweaks",              FromBool(PlayerSettingsConf.MS.CameraTweaks) },
                { "Misc",        "EnableConsoleLog",          FromBool(PlayerSettingsConf.MS.EnableConsoleLog) },

                { "Launcher",    "IgnoreUpdates",             FromBool(PlayerSettingsConf.LS.IgnoreUpdates) },
            };

            if (WriteConfigPreservingLayout("Config.ini", entries)) {
                spdlog::info("Saved {} settings to Config.ini.", entries.size());
            }
            else {
                spdlog::error("Could not write Config.ini. Settings were not saved.");
            }
        }
        else {
            spdlog::warn("Refusing to save before the config has been read, which would write defaults over the file.");
        }
    }

    void ConfigManager::ReadConfig() {
        spdlog::info("Reading Config...");
        cout.flush();
        cout.clear();
        cin.clear();
        Init();

        // Resolution Settings
        // Capital U. inipp's section maps are case sensitive and the file has always written "UseCustomResolution", so
        // the old lowercase spelling here never matched anything -- the setting silently kept its compiled-in default.
        inipp::extract(config.sections["Resolution"]["UseCustomResolution"], PlayerSettingsConf.RES.UseCustomRes);
        inipp::extract(config.sections["Resolution"]["HorizontalResolution"], PlayerSettingsConf.RES.Resolution.x);
        inipp::extract(config.sections["Resolution"]["VerticalResolution"], PlayerSettingsConf.RES.Resolution.y);
        inipp::extract(config.sections["Resolution"]["UseResolutionScale"], PlayerSettingsConf.RES.UseCustomResScale);
        inipp::extract(config.sections["Resolution"]["ResolutionScalePercentage"], PlayerSettingsConf.RES.CustomResScale);
        // FOV Settings
        inipp::extract(config.sections["FieldOfView"]["UseCustomFOV"], PlayerSettingsConf.FOV.UseCustomFOV);
        inipp::extract(config.sections["FieldOfView"]["FieldOfView"], PlayerSettingsConf.FOV.FieldOfView);
        inipp::extract(config.sections["FieldOfView"]["UseAdaptiveFOVScaling"], PlayerSettingsConf.FOV.AdaptiveFOVScaling);
        // Sync and Framerate Settings
        inipp::extract(config.sections["Framerate"]["MaxFPS"], PlayerSettingsConf.SYNC.MaxFPS);
        inipp::extract(config.sections["Framerate"]["VSync"], PlayerSettingsConf.SYNC.VSync);
        inipp::extract(config.sections["Framerate"]["SyncInterval"], PlayerSettingsConf.SYNC.SyncInterval);
        // Rendering Settings
        inipp::extract(config.sections["Rendering"]["CameraDistortion"], PlayerSettingsConf.RS.CameraDistortion);
        inipp::extract(config.sections["Rendering"]["EdgeRendering"], PlayerSettingsConf.RS.EdgeRendering);
        inipp::extract(config.sections["Rendering"]["ColorCorrection"], PlayerSettingsConf.RS.ColorCorrection);
        inipp::extract(config.sections["Rendering"]["DepthOfField"], PlayerSettingsConf.RS.DepthOfField);
        inipp::extract(config.sections["Rendering"]["Fog"], PlayerSettingsConf.RS.Fog);
        inipp::extract(config.sections["Rendering"]["Foliage"], PlayerSettingsConf.RS.FoliageRendering);
        inipp::extract(config.sections["Rendering"]["Bloom"], PlayerSettingsConf.RS.Bloom);
        inipp::extract(config.sections["Rendering"]["IBL"], PlayerSettingsConf.RS.IBL);
        inipp::extract(config.sections["Rendering"]["LensFlare"], PlayerSettingsConf.RS.LensFlare);
        inipp::extract(config.sections["Rendering"]["MotionBlur"], PlayerSettingsConf.RS.MotionBlur);
        inipp::extract(config.sections["Rendering"]["MotionBlurPreset"], PlayerSettingsConf.RS.MotionBlurPreset);
        inipp::extract(config.sections["Rendering"]["RLRLighting"], PlayerSettingsConf.RS.RLRLighting);
        inipp::extract(config.sections["Rendering"]["Shadows"], PlayerSettingsConf.RS.Shadows);
        inipp::extract(config.sections["Rendering"]["ShadowResolution"], PlayerSettingsConf.RS.ShadowRes);
        inipp::extract(config.sections["Rendering"]["SSAO"], PlayerSettingsConf.RS.SSAO);
        string ssaoQuality;
        string ssrQuality;
        inipp::extract(config.sections["Rendering"]["SSAOQuality"], ssaoQuality);
        // "Engine" or "GTAO". Read at startup because the AO shader is created once, same as TAAReplaceResolve.
        string ssaoMode;
        inipp::extract(config.sections["Rendering"]["SSAOMode"], ssaoMode);
        if (ssaoMode == "GTAO" || ssaoMode == "gtao") { PlayerSettingsConf.RS.SSAOMode = 1; }
        else if (!ssaoMode.empty())                   { PlayerSettingsConf.RS.SSAOMode = 0; }
        inipp::extract(config.sections["Rendering"]["SSAORadius"], PlayerSettingsConf.RS.SSAORadius);
        inipp::extract(config.sections["Rendering"]["SSAOIntensity"], PlayerSettingsConf.RS.SSAOIntensity);
        inipp::extract(config.sections["Rendering"]["SSR"], PlayerSettingsConf.RS.SSR);
        inipp::extract(config.sections["Rendering"]["SSRQuality"], ssrQuality);
        inipp::extract(config.sections["Rendering"]["TAA"], PlayerSettingsConf.RS.TAA);
        inipp::extract(config.sections["Rendering"]["TAAJitter"], PlayerSettingsConf.RS.TAAJitter);
        inipp::extract(config.sections["Rendering"]["TAASharpness"], PlayerSettingsConf.RS.TAASharpness);
        // Read here rather than only set from the menu, because shaders are built once during startup: by the time the
        // checkbox is reachable the resolve has already been created and the toggle cannot take effect until a restart.
        inipp::extract(config.sections["Rendering"]["TAAReplaceResolve"], PlayerSettingsConf.RS.TAAReplaceResolve);
        inipp::extract(config.sections["Rendering"]["AnisotropicFiltering"], PlayerSettingsConf.RS.AnisotropicFiltering);
        inipp::extract(config.sections["Rendering"]["TextureLODBias"], PlayerSettingsConf.RS.TextureLODBias);
        inipp::extract(config.sections["Rendering"]["FixMinimapScissor"], PlayerSettingsConf.RS.FixMinimapScissor);
        inipp::extract(config.sections["Rendering"]["Tonemapping"], PlayerSettingsConf.RS.Tonemapping);
        inipp::extract(config.sections["Rendering"]["Vignette"], PlayerSettingsConf.RS.Vignette);
        // Input Settings
        inipp::extract(config.sections["Input"]["KBMPrompts"], PlayerSettingsConf.IS.KBMPrompts);
        inipp::extract(config.sections["Input"]["DisableSteamInput"], PlayerSettingsConf.IS.DisableSteamInput);
        string inputDeviceType;
        inipp::extract(config.sections["Input"]["InputType"], inputDeviceType);
        // Misc Settings
        inipp::extract(config.sections["Misc"]["SkipOpeningVideos"], PlayerSettingsConf.MS.SkipOpeningVideos);
        inipp::extract(config.sections["Misc"]["CameraTweaks"], PlayerSettingsConf.MS.CameraTweaks);
        inipp::extract(config.sections["Misc"]["EnableConsoleLog"], PlayerSettingsConf.MS.EnableConsoleLog);
        string cpuSchedulerMode;
        // The file spells this "CpuSchedulerMode"; the old "CPUSchedulerMode" spelling never matched. The value is
        // still parsed into a local and dropped, same as SSAOQuality, SSRQuality and InputType above -- none of them
        // have anywhere to go in PlayerSettings yet.
        inipp::extract(config.sections["Misc"]["CpuSchedulerMode"], cpuSchedulerMode);
        // Launcher Settings
        inipp::extract(config.sections["Launcher"]["IgnoreUpdates"], PlayerSettingsConf.LS.IgnoreUpdates);

        // Check if the Horizontal or Vertical Res is 0. If so, default the custom resolution to the current display resolution.
        if (PlayerSettingsConf.RES.Resolution.x == 0 || PlayerSettingsConf.RES.Resolution.y == 0) {
            auto CurrentResolution                = Util::GetCurrentDisplayResolution();
            PlayerSettingsConf.RES.Resolution.x   = CurrentResolution.x;
            PlayerSettingsConf.RES.Resolution.y   = CurrentResolution.y;
        }
        AlreadyReadConfig = true; // After the INI file has successfully been read for the first time, allow writing.
    }
}