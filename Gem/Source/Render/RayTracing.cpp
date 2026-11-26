#include "RayTracing.h"

#include <Atom/RHI/RHISystemInterface.h>
#include <AzCore/Settings/SettingsRegistry.h>

namespace NoclipProtocol::RayTracing
{
    bool IsOn()
    {
        // The same check the engine's probe grid makes before it ray traces.
        auto* rhi = AZ::RHI::RHISystemInterface::Get();
        const bool supported = rhi && rhi->GetRayTracingSupport() != AZ::RHI::MultiDevice::NoDevices;
        bool wanted = true;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(wanted, "/NoclipProtocol/Graphics/RayTracing");
            registry->Set("/NoclipProtocol/Graphics/RayTracingSupported", supported); // for the settings menu
        }
        return supported && wanted;
    }
} // namespace NoclipProtocol::RayTracing
