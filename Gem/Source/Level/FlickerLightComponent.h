#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Random.h>

namespace NoclipProtocol
{
    //! A failing fluorescent light: mostly on, then stutters in bursts, and now and then dies for a few seconds.
    //! Switches its light entity and its glowing panel entity on and off together.
    class FlickerLightComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(FlickerLightComponent, "{9E4B1F2A-7C35-4D8E-A6B0-3F1D2C5E8A47}");

        static void Reflect(AZ::ReflectContext* context);

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        void SetLit(bool lit);
        float Range(float low, float high);

        // Settings
        AZ::EntityId m_lightEntity;
        AZ::EntityId m_panelEntity;
        float m_minSteadySeconds = 1.5f;
        float m_maxSteadySeconds = 9.0f;
        float m_blackoutChance = 0.15f; // chance a stutter ends in a long blackout

        // Runtime
        AZ::SimpleLcgRandom m_random;
        float m_timer = 0.0f;
        int m_burstToggles = 0;
        bool m_lit = true;
    };
} // namespace NoclipProtocol
