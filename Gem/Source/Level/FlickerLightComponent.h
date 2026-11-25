#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Random.h>

namespace NoclipProtocol
{
    //! A failing fluorescent light: mostly on, then stutters in bursts, and now and then dies for a few seconds.
    //! Switches its light entity and its glowing panel entity on and off together, and crackles through a
    //! MiniAudio Playback component on its own entity (if it has one) when it sputters back on.
    //!
    //! With "Dies when near" set it looks like a normal working light until the player walks under it,
    //! then it stutters hard and goes dark for good.
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
        void Crackle();
        float Range(float low, float high);
        bool PlayerIsNear() const;
        void TickFlicker();
        void TickDying();

        // Settings
        AZ::EntityId m_lightEntity;
        AZ::EntityId m_panelEntity;
        float m_minSteadySeconds = 1.5f;
        float m_maxSteadySeconds = 9.0f;
        float m_blackoutChance = 0.15f; // chance a stutter ends in a long blackout
        float m_crackleChance = 0.6f;   // chance it crackles each time it sputters back on
        bool m_diesWhenNear = false;
        float m_triggerRadius = 2.2f;

        // Runtime
        AZ::SimpleLcgRandom m_random;
        float m_timer = 0.0f;
        int m_burstToggles = 0;
        bool m_lit = true;
        bool m_dying = false;
        bool m_dead = false;
    };
} // namespace NoclipProtocol
