#pragma once

namespace NoclipProtocol::RayTracing
{
    //! True when ray-traced bounce light runs: the graphics card can ray trace (the Nvidia PC, not the laptop)
    //! and it hasn't been turned off with the /NoclipProtocol/Graphics/RayTracing setting.
    //! With it on, light bounces off the floor and walls, so Level 0 has fewer working lights to stay dark.
    bool IsOn();
} // namespace NoclipProtocol::RayTracing
