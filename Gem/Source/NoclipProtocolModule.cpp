
#include <AzCore/Memory/SystemAllocator.h>
#include <AzCore/Module/Module.h>

#include "NoclipProtocolSystemComponent.h"
#include "Player/FirstPersonControllerComponent.h"
#include "Player/FlashlightComponent.h"
#include "Player/FootstepsComponent.h"
#include "Level/EndlessLevelComponent.h"
#include "Level/FlickerLightComponent.h"
#include "Hud/NoclipHudComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "Debug/GameBridgeComponent.h"
#include "Scripting/HearthBlocksComponent.h"
#include "Hud/HearthHudComponent.h"
#include "Debug/HearthDebugOverlayComponent.h"
#include "Monsters/HearthMonsterComponent.h"
namespace NoclipProtocol
{
    AZ::ComponentDescriptor* CreateWeaponsDescriptor(); // Player/FirstPersonControllerComponent.cpp (the weapons need Atom's headers)
    AZ::ComponentDescriptor* CreateMonsterSpawnerDescriptor(); // Player/FirstPersonControllerComponent.cpp too
}

#include <NoclipProtocol/NoclipProtocolTypeIds.h>

namespace NoclipProtocol
{
    class NoclipProtocolModule
        : public AZ::Module
    {
    public:
        AZ_RTTI(NoclipProtocolModule, NoclipProtocolModuleTypeId, AZ::Module);
        AZ_CLASS_ALLOCATOR(NoclipProtocolModule, AZ::SystemAllocator);

        NoclipProtocolModule()
            : AZ::Module()
        {
            // Push results of [MyComponent]::CreateDescriptor() into m_descriptors here.
            m_descriptors.insert(m_descriptors.end(), {
                NoclipProtocolSystemComponent::CreateDescriptor(),
                FirstPersonControllerComponent::CreateDescriptor(),
                FlashlightComponent::CreateDescriptor(),
                FootstepsComponent::CreateDescriptor(),
                EndlessLevelComponent::CreateDescriptor(),
                FlickerLightComponent::CreateDescriptor(),
                HearthBlocksComponent::CreateDescriptor(),
                HearthHudComponent::CreateDescriptor(),
                NoclipHudComponent::CreateDescriptor(),
                PlayerVitalsComponent::CreateDescriptor(),
                GameBridgeComponent::CreateDescriptor(),
                HearthDebugOverlayComponent::CreateDescriptor(),
                HearthMonsterComponent::CreateDescriptor(),
                CreateWeaponsDescriptor(),
                CreateMonsterSpawnerDescriptor(),
            });
        }

        /**
         * Add required SystemComponents to the SystemEntity.
         */
        AZ::ComponentTypeList GetRequiredSystemComponents() const override
        {
            return AZ::ComponentTypeList{
                azrtti_typeid<NoclipProtocolSystemComponent>(),
            };
        }
    };
}// namespace NoclipProtocol

#if defined(O3DE_GEM_NAME)
AZ_DECLARE_MODULE_CLASS(AZ_JOIN(Gem_, O3DE_GEM_NAME), NoclipProtocol::NoclipProtocolModule)
#else
AZ_DECLARE_MODULE_CLASS(Gem_NoclipProtocol, NoclipProtocol::NoclipProtocolModule)
#endif
