// The player's weapons (approved PlayerWeapons mockup): what each weapon is, what you carry, and what's happening in
// your hands right now. Owned weapons and ammunition live in the expedition's gear (Base::State::m_gear, saved with
// the base): a weapon by its requisition name ("Service pistol"), its loaded rounds as "mag:<name>", reserve ammunition
// as "ammo.pistol" / "ammo.shells" / "ammo.carbine" / "ammo.flare", road flares as "Flares". The rest is for this
// moment only (reloading, the pump, the wheel). Player/WeaponsSystem.h runs it; the HUD draws it.
#pragma once

#include "../Base/BaseBuilding.h"
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace NoclipProtocol::Weapons
{
    enum Id : int
    {
        Pistol,
        Shotgun,
        Carbine,
        FlareGun,
        Knife,
        Shield,
        Count
    };

    struct Def
    {
        const char* m_gear;       // its name in the gear (and the Foundation's requisitions)
        const char* m_hud;        // the block's name: SERVICE PISTOL
        const char* m_wheel;      // the wheel's label: PISTOL
        const char* m_mode;       // SEMI, PUMP, AUTO, SINGLE, MELEE, BLOCK
        const char* m_icon;       // HudIcons name
        const char* m_roundsIcon; // HudIcons name of its ammunition
        const char* m_ammo;       // reserve's gear name (nullptr: no ammunition)
        int m_cap;                // rounds in a full magazine / tube
        float m_interval;         // seconds between shots
        float m_reload;           // seconds for a reload (per shell when m_perShell)
        bool m_perShell;          // loads one shell at a time (the shotgun)
        bool m_auto;              // keeps firing while the trigger is held
        bool m_pump;              // must be pumped after every shot
        int m_pellets;            // per shot
        float m_spreadDegrees;    // cone the pellets spread over
        float m_damage;           // per pellet / per hit
        float m_range;            // metres
        bool m_weaponsI;          // needs Weapons I research
        const char* m_view;       // what you hold: its spawnable
        const char* m_note;       // the block's extra line (when nothing else is said)
    };

    inline const Def& Info(int id)
    {
        static const Def defs[Count] = {
            { "Service pistol", "SERVICE PISTOL", "PISTOL", "SEMI", "pistol", "rounds", "ammo.pistol", 12, 0.16f, 1.5f, false, false, false, 1, 0.6f,
              34.0f, 60.0f, false, "prefabs/weapons/view_pistol.spawnable", "" },
            { "Pump shotgun", "PUMP SHOTGUN", "SHOTGUN", "PUMP", "shotgun", "shells", "ammo.shells", 6, 0.25f, 0.55f, true, false, true, 8, 7.0f,
              14.0f, 30.0f, false, "prefabs/weapons/view_shotgun.spawnable", "" },
            { "Carbine", "CARBINE", "CARBINE", "AUTO", "carbine", "rounds", "ammo.carbine", 30, 0.095f, 2.2f, false, true, false, 1, 1.2f,
              28.0f, 90.0f, true, "prefabs/weapons/view_carbine.spawnable", "" },
            { "Flare gun", "FLARE GUN", "FLARE GUN", "SINGLE", "flaregun", "flare", "ammo.flare", 1, 0.6f, 1.3f, false, false, false, 1, 0.5f,
              10.0f, 40.0f, false, "prefabs/weapons/view_flaregun.spawnable", "Lights a room for 40 s \xc2\xb7 scares Smilers" },
            { "Combat knife", "COMBAT KNIFE", "KNIFE", "MELEE", "knife", "", nullptr, 0, 0.45f, 0.0f, false, false, false, 1, 0.0f,
              40.0f, 1.7f, false, "prefabs/weapons/view_knife.spawnable", "[LMB] slash \xc2\xb7 [RMB] stab \xc2\xb7 quiet" },
            { "Riot shield", "RIOT SHIELD", "SHIELD", "BLOCK", "shield", "", nullptr, 0, 0.7f, 0.0f, false, false, false, 1, 0.0f,
              18.0f, 1.6f, false, "prefabs/weapons/view_shield.spawnable", "[RMB] raise \xc2\xb7 [LMB] bash \xc2\xb7 blocks a charge" },
        };
        return defs[AZStd::clamp(id, 0, Count - 1)];
    }

    // The flashlight on what you hold (the owner, Nov 27: mounted on the side of the long guns, held beside the pistol,
    // flare gun and knife in the spare hand; Tools/Weapons/lights.py): where its lens is this frame and which way the
    // weapon points, in camera space (the beam hangs under the camera). WeaponsComponent::PlaceView writes it, FlashlightComponent puts the beam there.
    struct Lens
    {
        bool m_valid = false;
        AZ::Vector3 m_at = AZ::Vector3::CreateZero();
        AZ::Quaternion m_turn = AZ::Quaternion::CreateIdentity();
    };
    inline Lens& HeldLens()
    {
        static Lens lens;
        return lens;
    }

    // The wheel's six slices, clockwise from the top: as in the mockup.
    constexpr int WheelOrder[Count] = { Shotgun, Pistol, Knife, FlareGun, Shield, Carbine };

    // Seconds a road flare (thrown, [G]) and a flare-gun flare burn.
    constexpr float RoadFlareSeconds = 60.0f;
    constexpr float GunFlareSeconds = 40.0f;

    struct Impact
    {
        AZ::Vector3 m_at;
        AZ::Vector3 m_normal;
        float m_born;
        bool m_soft; // hit something that isn't a wall (no spark)
    };

    struct State
    {
        // ---- what you carry (in the expedition's gear) ----
        static bool Owned(int id) { return Base::Get().GearCount(Info(id).m_gear) > 0; }
        static bool Locked(int id) { return Info(id).m_weaponsI && !Base::Get().m_weaponsI; }
        static AZStd::string MagKey(int id) { return AZStd::string("mag:") + Info(id).m_gear; }
        static int Mag(int id) { return Base::Get().GearCount(MagKey(id).c_str()); }
        static int Reserve(int id) { return Info(id).m_ammo ? Base::Get().GearCount(Info(id).m_ammo) : 0; }
        static void SetMag(int id, int rounds) { Base::Get().AddGear(MagKey(id).c_str(), rounds - Mag(id)); }
        static void TakeReserve(int id, int rounds)
        {
            if (Info(id).m_ammo)
            {
                Base::Get().AddGear(Info(id).m_ammo, -rounds);
            }
        }
        static bool UsesAmmo(int id) { return Info(id).m_ammo != nullptr; }

        int m_equipped = -1;    // what's in your hands (-1: nothing)
        int m_lastLong = -1;    // the last long gun you had out ([2] goes back to it)

        // ---- right now ----
        float m_cooldown = 0.0f;    // until the next shot
        float m_reloadLeft = 0.0f;  // > 0 while reloading
        float m_reloadTotal = 0.0f;
        bool m_pumpNeeded = false;  // the shotgun after a shot
        float m_pumpLeft = 0.0f;    // > 0 while pumping
        float m_meleeLeft = 0.0f;   // > 0 during a slash / stab / bash
        bool m_stab = false;
        bool m_shieldUp = false;
        bool m_trigger = false;     // [LMB] held
        bool m_aim = false;         // [RMB] held
        bool m_wheelOpen = false;   // [C] held
        int m_wheelPick = -1;       // the slice the mouse points at (an Id)
        float m_wheelX = 0.0f, m_wheelY = 0.0f; // the mouse's pull while the wheel is open
        float m_lastShot = -10.0f;  // when (clock) the last shot went off: the muzzle flash
        float m_kick = 0.0f;        // the view model's recoil, springs back
        float m_clock = 0.0f;
        float m_drawLeft = 0.0f;    // > 0 while bringing a weapon up
        AZStd::vector<Impact> m_impacts;
        AZStd::string m_notice;     // a moment's line under the block (NO AMMUNITION ...)
        float m_noticeUntil = 0.0f;

        bool Busy() const { return m_reloadLeft > 0.0f || m_pumpLeft > 0.0f || m_meleeLeft > 0.0f || m_drawLeft > 0.0f; }
        float ReloadProgress() const { return m_reloadTotal > 0.0f ? 1.0f - m_reloadLeft / m_reloadTotal : 0.0f; }
    };

    inline State& Get()
    {
        static State state;
        return state;
    }
} // namespace NoclipProtocol::Weapons
