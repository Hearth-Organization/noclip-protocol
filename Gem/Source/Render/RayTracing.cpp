#include "RayTracing.h"

#include <Atom/RHI/RHISystemInterface.h>
#include <AzCore/Settings/SettingsRegistry.h>

namespace NoclipProtocol::RayTracing
{
    bool IsOn()
    {
        bool wanted = true;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(wanted, "/NoclipProtocol/Graphics/RayTracing");
        }
        if (!wanted)
        {
            return false;
        }
        // The same check the engine's probe grid makes before it ray traces.
        auto* rhi = AZ::RHI::RHISystemInterface::Get();
        return rhi && rhi->GetRayTracingSupport() != AZ::RHI::MultiDevice::NoDevices;
    }
} // namespace NoclipProtocol::RayTracing
