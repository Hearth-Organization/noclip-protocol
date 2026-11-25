#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

namespace NoclipProtocol
{
    //! Hand-held flashlight with a battery. F toggles it.
    //! The light itself is a child entity (a Spot light / Light component),
    //! which this component switches on and off.
    //! When the battery runs low the beam flickers; when it's empty it dies.
    class FlashlightComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
        , public AzFramework::InputChannelEventListener
    {
    public:
        AZ_COMPONENT(FlashlightComponent, "{BAD45DA2-209C-41B9-B3DF-CA71A55A53C6}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required);

        //! 0..1, for the HUD later.
        float GetBatteryFraction() const;
        //! Batteries found in the level call this.
        void AddBattery(float seconds);

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        // AzFramework::InputChannelEventListener
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;

    private:
        void SetLightVisible(bool visible);

        // Settings
        AZ::EntityId m_lightEntity;
        float m_batteryCapacitySeconds = 180.0f; // 3 minutes of light on a full battery
        float m_lowBatteryFraction = 0.15f;      // starts flickering below this
        bool m_startOn = false;

        // Runtime state
        float m_batterySeconds = 0.0f;
        float m_flickerTimer = 0.0f;
        bool m_switchedOn = false;   // what the player wants
        bool m_lightVisible = false; // what's actually lit right now
    };
} // namespace NoclipProtocol
