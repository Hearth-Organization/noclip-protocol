#include "Photoreal.h"

#include <Atom/Feature/PostProcess/AmbientOcclusion/AoSettingsInterface.h>
#include <Atom/Feature/PostProcess/ExposureControl/ExposureControlSettingsInterface.h>
#include <Atom/Feature/PostProcess/MotionBlur/MotionBlurSettingsInterface.h>
#include <Atom/Feature/PostProcess/PostProcessFeatureProcessorInterface.h>
#include <Atom/Feature/PostProcess/PostProcessSettingsInterface.h>
#include <Atom/Feature/PostProcess/Vignette/VignetteSettingsInterface.h>
#include <Atom/Feature/SpecularReflections/SpecularReflectionsFeatureProcessorInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/Settings/SettingsRegistry.h>

namespace NoclipProtocol::Photoreal
{
    namespace
    {
        bool s_reapply = false;

        //! /NoclipProtocol/Graphics/<name>, default when unset.
        bool Flag(const char* name, bool fallback)
        {
            bool value = fallback;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(value, AZStd::string::format("/NoclipProtocol/Graphics/%s", name));
            }
            return value;
        }

        // gfx_set <RtReflections|Ssr|Gtao|EyeAdaptation|MotionBlur|Vignette> <0|1>: one effect on or off, at once.
        void GfxSet(const AZ::ConsoleCommandContainer& arguments)
        {
            if (arguments.size() < 2)
            {
                AZ_Printf("NoclipProtocol", "gfx_set <RtReflections|Ssr|Gtao|EyeAdaptation|MotionBlur|Vignette> <0|1>");
                return;
            }
            const AZStd::string name(arguments[0]);
            const bool on = AZStd::string(arguments[1]) != "0";
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set(AZStd::string::format("/NoclipProtocol/Graphics/%s", name.c_str()), on);
            }
            s_reapply = true;
            AZ_Printf("NoclipProtocol", "gfx_set: %s %s", name.c_str(), on ? "on" : "off");
        }
        AZ_CONSOLEFREEFUNC("gfx_set", GfxSet, AZ::ConsoleFunctorFlags::Null, "gfx_set <effect> <0|1>: one photoreal effect on or off");
    }

    bool TakeReapply()
    {
        const bool again = s_reapply;
        s_reapply = false;
        return again;
    }

    bool Apply(AZ::EntityId environment, bool rayTraced, bool outdoors)
    {
        if (!environment.IsValid())
        {
            return false;
        }
        AZ::RPI::Scene* scene = AZ::RPI::Scene::GetSceneForEntityId(environment);
        if (!scene)
        {
            return false;
        }
        auto* reflections = scene->GetFeatureProcessor<AZ::Render::SpecularReflectionsFeatureProcessorInterface>();
        auto* postFx = scene->GetFeatureProcessor<AZ::Render::PostProcessFeatureProcessorInterface>();
        if (!reflections || !postFx)
        {
            return false;
        }
        bool look = true; // /NoclipProtocol/Graphics/Photoreal false: the old look (for slow machines and comparisons)
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(look, "/NoclipProtocol/Graphics/Photoreal");
        }

        // Reflections: the wet patches on the carpet, the hotel's polished wood, the pipes, puddles, the flashlight's
        // glint. Hybrid: screen space where it can see, rays where it can't (off-screen lights, behind the camera).
        AZ::Render::SSROptions ssr = reflections->GetSSROptions();
        // The ray-traced half stays off until it is proven on the Nvidia card (it lost the device on Nov 28).
        const bool rtReflections = rayTraced && Flag("RtReflections", false);
        ssr.m_enable = look && Flag("Ssr", true);
        ssr.m_reflectionMethod = rtReflections ? AZ::Render::SSROptions::ReflectionMethod::HybridWithFallback
                                               : AZ::Render::SSROptions::ReflectionMethod::ScreenSpace;
        ssr.m_rayTraceFallbackSpecular = rtReflections;
        ssr.m_maxRoughness = 0.55f;   // satin and damp surfaces too, not only mirrors
        ssr.m_maxRayDistance = 40.0f;
        ssr.m_halfResolution = true;  // full resolution costs too much for what it adds at 1080p
        ssr.m_temporalFiltering = true;
        ssr.m_luminanceClamp = true;
        ssr.m_maxLuminance = 2.0f;    // no fireflies off the light panels
        reflections->SetSSROptions(ssr);

        AZ::Render::PostProcessSettingsInterface* settings = postFx->GetOrCreateSettingsInterface(environment);
        if (!settings)
        {
            return false;
        }

        // Contact shadows where things meet: wall and floor, under a desk, the back of a corridor.
        if (auto* ao = settings->GetOrCreateAoSettingsInterface())
        {
            // Not outdoors: under the open sky GTAO hung the GPU two seconds into every outdoor level (Nov 28).
            ao->SetEnabled(look && !outdoors && Flag("Gtao", true));
            ao->SetAoMethod(AZ::Render::Ao::AoMethodType::GTAO);
            ao->SetGtaoQuality(AZ::Render::Ao::GtaoQualityLevel::High);
            ao->SetGtaoStrength(1.1f);
            ao->SetGtaoPower(1.4f);
            ao->SetGtaoWorldRadius(1.2f);
            ao->OnConfigChanged();
        }

        // An eye, not a fixed camera: it stops down in a bright room and never opens up past the level's own exposure,
        // so the dark stays dark and lit rooms don't blow out (owner, Nov 28).
        if (auto* exposure = settings->GetOrCreateExposureControlSettingsInterface())
        {
            exposure->SetExposureControlType(look && Flag("EyeAdaptation", true) ? AZ::Render::ExposureControl::ExposureControlType::EyeAdaptation
                                                  : AZ::Render::ExposureControl::ExposureControlType::ManualOnly);
            // Under the open sky it may only stop down: opening up turned Level 9's midnight into dusk (Nov 28).
            exposure->SetEyeAdaptationExposureMin(outdoors ? 0.0f : -2.0f);
            // Half a stop darker than each level was set up for (the owner, Nov 28: "not all blown out").
            // The level's own value is kept once, so applying again never darkens twice.
            static AZStd::unordered_map<AZ::EntityId, float> original;
            auto it = original.find(environment);
            if (it == original.end())
            {
                it = original.emplace(environment, exposure->GetManualCompensation()).first;
            }
            exposure->SetManualCompensation(it->second - (look ? 0.5f : 0.0f));
            exposure->SetEyeAdaptationExposureMax(0.0f);
            exposure->SetEyeAdaptationSpeedUp(2.0f);
            exposure->SetEyeAdaptationSpeedDown(0.8f);
            exposure->OnConfigChanged();
        }

        // The bodycam: blur on quick turns, darker edges (its grain follows your sanity: PlayerVitalsComponent).
        if (auto* blur = settings->GetOrCreateMotionBlurSettingsInterface())
        {
            blur->SetEnabled(look && Flag("MotionBlur", true));
            blur->SetStrength(0.45f);
            blur->OnConfigChanged();
        }
        if (auto* vignette = settings->GetOrCreateVignetteSettingsInterface())
        {
            vignette->SetEnabled(look && Flag("Vignette", true));
            vignette->SetIntensity(0.25f);
            vignette->OnConfigChanged();
        }
        settings->OnConfigChanged();
        postFx->OnPostProcessSettingsChanged();
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Graphics/PhotorealApplied", look);
        }
        return true;
    }
} // namespace NoclipProtocol::Photoreal
