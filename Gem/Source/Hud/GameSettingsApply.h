// What each setting does (GameSettings.h has the lines). ApplyAll() runs at start and when the menu's Apply is pressed.
//
// Through the engine: render scale and upscaling (r_renderScale), anti-aliasing (r_antiAliasing, r_multiSampleCount),
// shadow filtering, texture detail (r_streamingImageMipBias), clutter (r_maxVisibleDecals), draw distance (how many
// lights), depth of field and fog, fullscreen and the window size, V-Sync, HDR output, the sound buses (Hearth audio).
// The game reads the rest from the registry itself: ray tracing, film grain, field of view, frame limit, mouse and
// controller, voices, weapons' volume, HUD pieces, subtitles, chatter, head bob, and so on.
#pragma once

#include "GameSettings.h"

#include <MiniAudio/MiniAudioBus.h>

#include <AzCore/Console/IConsole.h>
#include <AzCore/Interface/Interface.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/string/string.h>

#include <cmath>

namespace NoclipProtocol::GameSettings
{
    //! Runs an engine command, only when it differs from the last time (switching anti-aliasing or the window rebuilds
    //! things, so nothing is redone for no reason).
    inline void Command(const AZStd::string& command)
    {
        static AZStd::unordered_map<AZStd::string, AZStd::string> last;
        const AZStd::string name = command.substr(0, command.find(' '));
        auto it = last.find(name);
        if (it != last.end() && it->second == command)
        {
            return;
        }
        last[name] = command;
        if (auto* console = AZ::Interface<AZ::IConsole>::Get())
        {
            console->PerformCommand(command.c_str());
        }
    }

    //! 0..100 volume as decibels for the Hearth audio buses (0 = silent).
    inline double Decibels(double volume)
    {
        return volume <= 0.5 ? -60.0 : 20.0 * std::log10(volume / 100.0);
    }

    //! startup: the light settings only (anti-aliasing, the window and HDR rebuild the renderer, so they change only
    //! when the menu's Apply changes them).
    inline void ApplyAll(bool startup = false)
    {
        auto* r = AZ::SettingsRegistry::Get();
        if (!r)
        {
            return;
        }
        // ---- graphics ----
        r->Set("/NoclipProtocol/Graphics/RayTracing", On("raytracing"));
        static const double upscale[4] = { 1.0, 0.67, 0.58, 0.5 };
        const double scale = Number("renderscale") / 100.0 * upscale[AZStd::clamp(Index("upscaling"), 0, 3)];
        Command(AZStd::string::format("r_renderScale %.3f", scale));
        static const char* const aa[4] = { "", "SMAA", "TAA", "MSAA" };
        const int aaIndex = AZStd::clamp(Index("aa"), 0, 3);
        static int appliedAa = 2; // the pipeline starts with TAA
        if (!startup && aaIndex != appliedAa)
        {
            appliedAa = aaIndex;
            Command(AZStd::string::format("r_multiSampleCount %d", aaIndex == 3 ? 4 : 1));
            if (aaIndex > 0)
            {
                Command(AZStd::string::format("r_antiAliasing %s", aa[aaIndex]));
            }
        }
        static const int shadowFilter[4] = { 0, 1, 1, 3 };
        Command(AZStd::string::format("r_directionalShadowFilteringMethod %d", shadowFilter[AZStd::clamp(Index("shadows"), 0, 3)]));
        Command(AZStd::string::format("r_streamingImageMipBias %d", 3 - AZStd::clamp(Index("textures"), 0, 3)));
        static const int decals[4] = { 64, 256, 512, 1024 };
        Command(AZStd::string::format("r_maxVisibleDecals %d", decals[AZStd::clamp(Index("clutter"), 0, 3)]));
        static const int lights[3] = { 128, 256, 512 };
        Command(AZStd::string::format("r_maxVisiblePointLights %d", lights[AZStd::clamp(Index("distance"), 0, 2)]));
        Command(AZStd::string::format("r_maxVisibleSpotLights %d", lights[AZStd::clamp(Index("distance"), 0, 2)]));
        Command(AZStd::string::format("r_enableDOF %d", On("dof") ? 1 : 0));
        Command("r_enableFog 1");
        r->Set("/NoclipProtocol/Graphics/FilmGrain", Number("filmgrain") / 100.0);

        // ---- display ----
        static const int sizes[6][2] = { { 1280, 720 }, { 1600, 900 }, { 1920, 1080 }, { 2560, 1440 }, { 3440, 1440 }, { 3840, 2160 } };
        const int res = AZStd::clamp(Index("resolution"), 0, 5);
        const int mode = Index("displaymode");
        if (!startup)
        {
            Command(AZStd::string::format("r_fullscreen %d", mode == 2 ? 0 : 1));
            if (mode == 2)
            {
                Command(AZStd::string::format("r_width %d", sizes[res][0]));
                Command(AZStd::string::format("r_height %d", sizes[res][1]));
            }
            Command(AZStd::string::format("r_hdrOutput %d", On("hdr") ? 1 : 0)); // takes effect at the next start
        }
        Command(AZStd::string::format("vsync_interval %d", On("vsync") ? 1 : 0));
        static const double limits[7] = { 30, 60, 90, 120, 144, 165, 0 };
        r->Set("/NoclipProtocol/Display/FrameLimit", limits[AZStd::clamp(Index("framelimit"), 0, 6)]);
        r->Set("/NoclipProtocol/Display/Fov", Number("fov"));
        r->Set("/NoclipProtocol/Display/Brightness", Number("brightness"));

        // ---- audio: the whole game at full volume (each level's listener used to hold it at 1%), then the Hearth buses ----
        if (auto* audio = MiniAudio::MiniAudioInterface::Get())
        {
            audio->SetGlobalVolume(1.0f);
        }
        // ---- audio: the Hearth buses (the engine's own sounds) ----
        r->Set("/Hearth/Audio/Buses/Master", Decibels(Number("vol_master")));
        r->Set("/Hearth/Audio/Buses/Music", Decibels(Number("vol_music")));
        r->Set("/Hearth/Audio/Buses/Player", Decibels(Number("vol_effects")));
        r->Set("/Hearth/Audio/Buses/Entities", Decibels(Number("vol_monsters")));
        r->Set("/Hearth/Audio/Buses/Ambience", Decibels(Number("vol_ambience")));
        // The fluorescent hum and buzz: the owner, Nov 27: "turn the light hum way down" (24 dB under the Ambience slider).
        r->Set("/Hearth/Audio/Buses/Lights", Decibels(Number("vol_ambience")) - 24.0);
        Command("hearth_audioRefresh");

        // ---- controls ----
        r->Set("/NoclipProtocol/Player/MouseSensitivity", Number("mouse_sens") / 4.5);
        r->Set("/NoclipProtocol/Player/InvertLook", On("mouse_invert"));
        r->Set("/NoclipProtocol/Player/AimSensitivity", Number("mouse_aim"));
    }
} // namespace NoclipProtocol::GameSettings
