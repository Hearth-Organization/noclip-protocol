// The engine's film grain request bus (AtomLyIntegration CommonFeatures, PostProcess/FilmGrain/FilmGrainBus.h),
// declared here exactly as the engine's macros expand it: that gem's include folder isn't on this gem's include
// path without a CMake change (a reconfigure). Same name, same RTTI id, same functions in the same order, so it is
// the same bus. If the engine's FilmGrainParams.inl changes, change this too.
#pragma once

#include <AzCore/Component/ComponentBus.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/std/string/string.h>

namespace AZ
{
    namespace Render
    {
        class FilmGrainRequests : public ComponentBus
        {
        public:
            AZ_RTTI(AZ::Render::FilmGrainRequests, "{8072BEF6-22A2-45F0-A887-0CB840B6EE45}");

            static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Single;
            virtual ~FilmGrainRequests()
            {
            }

            virtual bool GetEnabled() const = 0;
            virtual void SetEnabled(bool val) = 0;
            virtual bool GetEnabledOverride() const = 0;
            virtual void SetEnabledOverride(bool val) = 0;

            virtual float GetIntensity() const = 0;
            virtual void SetIntensity(float val) = 0;
            virtual float GetIntensityOverride() const = 0;
            virtual void SetIntensityOverride(float val) = 0;

            virtual float GetLuminanceDampening() const = 0;
            virtual void SetLuminanceDampening(float val) = 0;
            virtual float GetLuminanceDampeningOverride() const = 0;
            virtual void SetLuminanceDampeningOverride(float val) = 0;

            virtual float GetTilingScale() const = 0;
            virtual void SetTilingScale(float val) = 0;
            virtual float GetTilingScaleOverride() const = 0;
            virtual void SetTilingScaleOverride(float val) = 0;

            virtual AZStd::string GetGrainPath() const = 0;
            virtual void SetGrainPath(AZStd::string val) = 0;
        };

        typedef AZ::EBus<FilmGrainRequests> FilmGrainRequestBus;
    } // namespace Render
} // namespace AZ
