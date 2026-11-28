#pragma once

#include <AzCore/Component/EntityId.h>

namespace NoclipProtocol::Photoreal
{
    //! The photoreal look, the same in every level (the owner, Nov 28: "make the game look photo realistic";
    //! "ray tracing needs to be on all the levels"). Set on the level's "Atom Default Environment" entity:
    //! - ray-traced reflections (screen space first, the ray tracer where the screen has nothing; screen space only
    //!   on a card that can't ray trace),
    //! - ground-truth ambient occlusion (GTAO) in corners, under furniture, where walls meet the floor,
    //! - eye adaptation: the camera's exposure follows the light like an eye (dark corridors open up slowly, a lit room
    //!   dazzles for a moment), around the level's own exposure,
    //! - motion blur on fast turns and a soft lens vignette, like the bodycam it is.
    //! Returns false until the scene and the environment are ready; call it each frame until it returns true.
    bool Apply(AZ::EntityId environment, bool rayTraced);
    //! True once after gfx_set changed an effect: apply again.
    bool TakeReapply();
} // namespace NoclipProtocol::Photoreal
