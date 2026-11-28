#include "FlashlightComponent.h"
#include "Weapons.h"

#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Random.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AtomLyIntegration/CommonFeatures/CoreLights/AreaLightBus.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzCore/Console/IConsole.h>

namespace NoclipProtocol
{
    namespace
    {
        // flashlight_toggle: like pressing F (testing through the game bridge).
        void FlashlightToggle([[maybe_unused]] const AZ::ConsoleCommandContainer& arguments)
        {
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Player/FlashlightToggle", true);
            }
        }
        AZ_CONSOLEFREEFUNC("flashlight_toggle", FlashlightToggle, AZ::ConsoleFunctorFlags::Null, "Flashlight on/off (testing)");
    }

    namespace
    {
        // One shared random source for flicker timing.
        AZ::SimpleLcgRandom& FlickerRandom()
        {
            static AZ::SimpleLcgRandom random(1337);
            return random;
        }
    }

    void FlashlightComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<FlashlightComponent, AZ::Component>()
                ->Version(1)
                ->Field("LightEntity", &FlashlightComponent::m_lightEntity)
                ->Field("BatteryCapacitySeconds", &FlashlightComponent::m_batteryCapacitySeconds)
                ->Field("LowBatteryFraction", &FlashlightComponent::m_lowBatteryFraction)
                ->Field("StartOn", &FlashlightComponent::m_startOn);

            if (auto edit = serialize->GetEditContext())
            {
                edit->Class<FlashlightComponent>("Flashlight", "F toggles a battery-powered light that flickers when low")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlashlightComponent::m_lightEntity,
                        "Light entity", "Child entity with a spot Light component")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlashlightComponent::m_batteryCapacitySeconds,
                        "Battery (seconds)", "How long a full battery lasts")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &FlashlightComponent::m_lowBatteryFraction,
                        "Low battery", "Starts flickering below this fraction")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlashlightComponent::m_startOn,
                        "Start on", "Flashlight is on when the level starts");
            }
        }
    }

    void FlashlightComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
    {
        required.push_back(AZ_CRC_CE("TransformService"));
    }

    float FlashlightComponent::GetBatteryFraction() const
    {
        return m_batteryCapacitySeconds > 0.0f ? m_batterySeconds / m_batteryCapacitySeconds : 0.0f;
    }

    void FlashlightComponent::AddBattery(float seconds)
    {
        m_batterySeconds = AZ::GetMin(m_batterySeconds + seconds, m_batteryCapacitySeconds);
    }

    void FlashlightComponent::Activate()
    {
        m_batterySeconds = m_batteryCapacitySeconds;
        m_switchedOn = m_startOn;
        SetLightVisible(m_switchedOn);

        AzFramework::InputChannelEventListener::Connect();
        AZ::TickBus::Handler::BusConnect();
    }

    void FlashlightComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        AzFramework::InputChannelEventListener::Disconnect();
    }

    void FlashlightComponent::SetLightVisible(bool visible)
    {
        // Enforced every call (cheap), so the light also ends up right if it activated after us.
        m_lightVisible = visible;
        if (!m_lightEntity.IsValid())
        {
            return;
        }

        // Switch the light's entity on or off directly. (In game mode the level's entities are
        // spawned copies that aren't owned by the game entity context, so ask the application for it.)
        AZ::Entity* lightEntity = nullptr;
        AZ::ComponentApplicationBus::BroadcastResult(lightEntity, &AZ::ComponentApplicationRequests::FindEntity, m_lightEntity);
        if (!lightEntity)
        {
            return;
        }
        if (visible && lightEntity->GetState() == AZ::Entity::State::Init)
        {
            lightEntity->Activate();
            // Without a weapon in hand the beam comes from just ahead of the eyes, like a chest light; with one it comes
            // from the weapon's own light (PlaceBeam).
            if (!m_baseKept)
            {
                AZ::TransformBus::EventResult(m_baseRotation, m_lightEntity, &AZ::TransformBus::Events::GetLocalRotationQuaternion);
                m_baseKept = true;
            }
            AZ::TransformBus::Event(m_lightEntity, &AZ::TransformBus::Events::SetLocalTranslation, AZ::Vector3(0.16f, 0.95f, -0.10f));
            AZ::Render::AreaLightRequestBus::Event(m_lightEntity, [](AZ::Render::AreaLightRequests* light)
            {
                light->SetIntensity(450.0f);
                light->SetAttenuationRadius(30.0f);
                light->SetInnerShutterAngle(13.0f);
                light->SetOuterShutterAngle(30.0f);
            });
        }
        else if (!visible && lightEntity->GetState() == AZ::Entity::State::Active)
        {
            lightEntity->Deactivate();
        }
        if (visible)
        {
            PlaceBeam();
        }
    }

    void FlashlightComponent::PlaceBeam()
    {
        // The owner, Nov 27: the light is on the weapon (mounted on the side of the long guns, held beside the pistol,
        // flare gun and knife), so the beam starts at that light's lens and turns with the weapon, kick and sway included.
        const Weapons::Lens& lens = Weapons::HeldLens();
        if (lens.m_valid)
        {
            AZ::TransformBus::Event(m_lightEntity, &AZ::TransformBus::Events::SetLocalTM,
                AZ::Transform::CreateFromQuaternionAndTranslation(lens.m_turn * m_baseRotation, lens.m_at));
            m_onWeapon = true;
        }
        else if (m_onWeapon)
        {
            AZ::TransformBus::Event(m_lightEntity, &AZ::TransformBus::Events::SetLocalTM,
                AZ::Transform::CreateFromQuaternionAndTranslation(m_baseRotation, AZ::Vector3(0.16f, 0.95f, -0.10f)));
            m_onWeapon = false;
        }
    }

    bool FlashlightComponent::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
    {
        if (inputChannel.GetInputChannelId() == AzFramework::InputDeviceKeyboard::Key::AlphanumericF
            && inputChannel.IsStateBegan())
        {
            // A dead flashlight just clicks.
            m_switchedOn = !m_switchedOn;
        }
        return false;
    }

    void FlashlightComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        // Testing (the game bridge's registry op): /NoclipProtocol/Player/FlashlightToggle true flips it, like F.
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            bool toggle = false;
            if (registry->Get(toggle, "/NoclipProtocol/Player/FlashlightToggle") && toggle)
            {
                registry->Set("/NoclipProtocol/Player/FlashlightToggle", false);
                m_switchedOn = !m_switchedOn;
            }
        }
        // The HUD's flashlight bar (Hearth HUD, bind "player.flashlight").
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            // A fresh battery from the workbench or the Foundation ([B] in the HUD).
            bool refill = false;
            if (registry->Get(refill, "/NoclipProtocol/Player/FlashlightRefill") && refill)
            {
                registry->Set("/NoclipProtocol/Player/FlashlightRefill", false);
                m_batterySeconds = m_batteryCapacitySeconds;
            }
            registry->Set("/NoclipProtocol/Hud/player.flashlight", static_cast<double>(GetBatteryFraction()));
            registry->Set("/NoclipProtocol/Hud/player.flashlight.seconds", static_cast<double>(m_batterySeconds));
            registry->Set("/NoclipProtocol/Player/FlashlightOn", m_switchedOn && m_batterySeconds > 0.0f);
        }
        if (!m_switchedOn || m_batterySeconds <= 0.0f)
        {
            SetLightVisible(false);
            return;
        }

        m_batterySeconds = AZ::GetMax(0.0f, m_batterySeconds - deltaTime);

        if (GetBatteryFraction() >= m_lowBatteryFraction)
        {
            SetLightVisible(true);
            return;
        }

        // Low battery: flicker. The emptier it gets, the more it's off.
        m_flickerTimer -= deltaTime;
        if (m_flickerTimer <= 0.0f)
        {
            const float emptiness = 1.0f - GetBatteryFraction() / AZ::GetMax(m_lowBatteryFraction, 0.001f);
            const bool on = FlickerRandom().GetRandomFloat() > emptiness * 0.6f;
            SetLightVisible(on);
            m_flickerTimer = on ? 0.05f + FlickerRandom().GetRandomFloat() * 0.6f
                                : 0.03f + FlickerRandom().GetRandomFloat() * 0.15f;
        }
    }
} // namespace NoclipProtocol
