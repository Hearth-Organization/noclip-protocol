// The game's settings (GameSettings1-7.dc.html, approved Nov 27 2026: "the ingame setting need to be flushed out like a
// AAA games does"). Seven tabs; every line here is a line in the menu, in the mockup's order.
//
// Values live in the settings registry under /NoclipProtocol/Settings/<id> (choices as their index, sliders as their
// number, toggles as true/false, keys as the key's name) and are saved to the project's user/Registry/
// noclip_settings.setreg, so they never change by themselves. The menu edits a pending copy; Enter applies it.
#pragma once

#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

#include <cmath>

namespace NoclipProtocol::GameSettings
{
    enum class Kind { Head, Choice, Slider, Toggle, Key, Button };
    enum class Panel { Plain, RayTracing, Fov, Voices, Rebind, Gyro, Difficulty, Captions };

    struct Line
    {
        Kind m_kind;
        const char* m_id;      // registry name (also the key's action name)
        const char* m_label;
        const char* m_options; // choices: "A|B|C"; keys: "PRIMARY|SECONDARY"; sliders: unit format ("%.0f%%")
        double m_default;      // choice index, slider value, toggle 0/1
        double m_min, m_max, m_step;
        const char* m_note;    // the small grey note left of a choice ("Custom when you change one")
        const char* m_about;   // the side panel
        Panel m_panel;
    };

    inline constexpr const char* Tabs[7] = { "GRAPHICS", "DISPLAY", "AUDIO", "CONTROLS", "CONTROLLER", "GAMEPLAY", "ACCESSIBILITY" };

#define NP_HEAD(t) { Kind::Head, "", t, "", 0, 0, 0, 0, nullptr, nullptr, Panel::Plain }
#define NP_CHOICE(id, label, opts, def, note, about, panel) { Kind::Choice, id, label, opts, def, 0, 0, 1, note, about, panel }
#define NP_SLIDER(id, label, fmt, def, lo, hi, step, about, panel) { Kind::Slider, id, label, fmt, def, lo, hi, step, nullptr, about, panel }
#define NP_TOGGLE(id, label, def, about, panel) { Kind::Toggle, id, label, "", def, 0, 1, 1, nullptr, about, panel }
#define NP_KEY(id, label, keys) { Kind::Key, id, label, keys, 0, 0, 0, 0, nullptr, "Select an action and press the key or mouse button you want. Each action takes two. If the key is already used, you're asked whether to swap them.", Panel::Rebind }
#define NP_BUTTON(id, label, text, about, panel) { Kind::Button, id, label, text, 0, 0, 0, 0, nullptr, about, panel }

    inline const AZStd::vector<Line>& TabLines(int tab)
    {
        static const AZStd::vector<Line> tabs[7] = {
            // ---- GRAPHICS ----
            {
                NP_HEAD("PRESET"),
                NP_CHOICE("preset", "Quality preset", "LOW|MEDIUM|HIGH|ULTRA|CUSTOM", 3, "Custom when you change one",
                          "Sets every line below at once. Change any one of them and the preset reads Custom.", Panel::RayTracing),
                NP_BUTTON("detect", "Optimise for this PC", "DETECT", "Measures this PC's graphics card and picks the preset and ray tracing that keep the game smooth.", Panel::RayTracing),
                NP_HEAD("LIGHTING"),
                NP_TOGGLE("raytracing", "Ray tracing", 1, "Light bounces off the yellow walls for real: soft colour bleeding, dark corners, flicker that actually lights the room. Needs a graphics card with ray tracing.", Panel::RayTracing),
                NP_CHOICE("rtgi", "Ray-traced global illumination", "OFF|LOW|MEDIUM|HIGH", 3, nullptr, "How much bounced light the ray tracing works out. Higher is softer, warmer and slower.", Panel::RayTracing),
                NP_CHOICE("reflections", "Reflections", "SCREEN SPACE|RAY TRACED", 1, nullptr, "Ray traced: wet floors and the Poolrooms show what's really there, off screen too.", Panel::RayTracing),
                NP_CHOICE("shadows", "Shadows", "LOW|MEDIUM|HIGH|ULTRA", 3, nullptr, "Shadow sharpness and how far away they are drawn.", Panel::RayTracing),
                NP_CHOICE("ao", "Ambient occlusion", "OFF|SCREEN SPACE|RAY TRACED", 2, nullptr, "The dark in corners and under things that makes rooms feel solid.", Panel::RayTracing),
                NP_CHOICE("fog", "Volumetric fog and light", "LOW|MEDIUM|HIGH", 2, nullptr, "Haze in the air that light shines through: lamp beams, the flashlight, the fog outside.", Panel::RayTracing),
                NP_HEAD("DETAIL"),
                NP_CHOICE("textures", "Texture quality", "LOW|MEDIUM|HIGH|ULTRA", 3, nullptr, "Sharpness of the wallpaper, carpet and everything else. Uses graphics memory.", Panel::RayTracing),
                NP_CHOICE("filtering", "Texture filtering", "BILINEAR|TRILINEAR|4× ANISOTROPIC|8× ANISOTROPIC|16× ANISOTROPIC", 4, nullptr, "Keeps the carpet and long halls sharp at an angle.", Panel::RayTracing),
                NP_CHOICE("models", "Model detail", "LOW|MEDIUM|HIGH|ULTRA", 3, nullptr, "How detailed monsters, props and furniture are, and how near the simpler versions start.", Panel::RayTracing),
                NP_CHOICE("distance", "Draw distance", "NEAR|MEDIUM|FAR", 2, nullptr, "How far rooms, lights and props are drawn before the dark takes them.", Panel::RayTracing),
                NP_CHOICE("clutter", "Clutter and decals", "LOW|MEDIUM|HIGH|ULTRA", 3, nullptr, "Stains, junk, papers and small things lying around.", Panel::RayTracing),
                NP_CHOICE("aa", "Anti-aliasing", "OFF|SMAA|TAA|MSAA 4×", 2, nullptr, "Smooths jagged edges. TAA is the smoothest; SMAA is sharper.", Panel::RayTracing),
                NP_CHOICE("upscaling", "Upscaling", "OFF|QUALITY|BALANCED|PERFORMANCE", 0, nullptr, "Draws fewer pixels and sharpens them back up, for more frames. Takes effect at the next start.", Panel::RayTracing),
                NP_SLIDER("renderscale", "Render scale", "%.0f%%", 100, 50, 200, 5, "Pixels drawn compared with your screen. Over 100% is sharper and slower.", Panel::RayTracing),
                NP_TOGGLE("motionblur", "Motion blur", 0, "Blurs fast movement and quick turns.", Panel::RayTracing),
                NP_SLIDER("filmgrain", "Film grain", "%.0f%%", 40, 0, 100, 5, "The grain over the picture (it grows as sanity drops).", Panel::RayTracing),
                NP_TOGGLE("chromatic", "Chromatic aberration", 1, "Coloured fringes at the edges of the screen, like an old lens.", Panel::RayTracing),
                NP_TOGGLE("dof", "Depth of field", 1, "Blurs what's far away while you aim, and in cutscenes.", Panel::RayTracing),
                NP_TOGGLE("lensflares", "Lens flares", 1, "Streaks and glow when you look into bright lights.", Panel::RayTracing),
            },
            // ---- DISPLAY ----
            {
                NP_HEAD("SCREEN"),
                NP_CHOICE("monitor", "Monitor", "1 · PRIMARY", 0, nullptr, "Which screen the game shows on.", Panel::Plain),
                NP_CHOICE("displaymode", "Display mode", "FULLSCREEN|BORDERLESS|WINDOWED", 0, nullptr, "Fullscreen is the fastest; borderless makes switching windows quicker.", Panel::Plain),
                NP_CHOICE("resolution", "Resolution", "1280 × 720|1600 × 900|1920 × 1080|2560 × 1440|3440 × 1440|3840 × 2160", 3, nullptr, "The size of the picture in pixels.", Panel::Plain),
                NP_CHOICE("refresh", "Refresh rate", "60 Hz|75 Hz|120 Hz|144 Hz|165 Hz|240 Hz", 4, nullptr, "How many times a second the monitor updates.", Panel::Plain),
                NP_CHOICE("framelimit", "Frame rate limit", "30|60|90|120|144|165|UNLIMITED", 6, nullptr, "Caps the frame rate: smoother pacing, cooler and quieter, more battery.", Panel::Plain),
                NP_TOGGLE("vsync", "V-Sync", 0, "Stops tearing by waiting for the monitor. Adds a little delay.", Panel::Plain),
                NP_TOGGLE("lowlatency", "Low latency mode", 1, "Keeps the time between your mouse and the screen as short as possible.", Panel::Plain),
                NP_HEAD("VIEW"),
                NP_SLIDER("fov", "Field of view", "%.0f°", 95, 60, 120, 1, "How much of the Backrooms you see at once. Higher shows more of the hall around you (and more of what might be in it); lower feels closer and tenser. 95° is a good start on a monitor, 80° on a TV.", Panel::Fov),
                NP_SLIDER("weaponfov", "Weapon field of view", "%.0f°", 70, 50, 90, 1, "How big your gun and hands look.", Panel::Fov),
                NP_SLIDER("uiscale", "UI scale", "%.0f%%", 100, 75, 150, 5, "Size of the HUD and menus.", Panel::Plain),
                NP_SLIDER("safearea", "Safe area (TV)", "%.0f%%", 100, 80, 100, 1, "Pulls the HUD in from the edges for TVs that cut them off.", Panel::Plain),
                NP_HEAD("PICTURE"),
                NP_TOGGLE("hdr", "HDR", 1, "Brighter lights and deeper darks on an HDR screen.", Panel::Fov),
                NP_SLIDER("hdrpeak", "HDR peak brightness", "%.0f nits", 800, 400, 2000, 50, "Set to your screen's peak brightness.", Panel::Fov),
                NP_SLIDER("brightness", "Brightness", "%.0f", 50, 0, 100, 1, "Raise the brightness until the left symbol is barely visible.", Panel::Fov),
                NP_BUTTON("calibrate", "Brightness calibration", "CALIBRATE", "Raise the brightness until the left symbol is barely visible.", Panel::Fov),
            },
            // ---- AUDIO ----
            {
                NP_HEAD("VOLUME"),
                NP_SLIDER("vol_master", "Master", "%.0f", 100, 0, 100, 1, "Everything at once.", Panel::Voices),
                NP_SLIDER("vol_music", "Music", "%.0f", 70, 0, 100, 1, "The menu and the music in cutscenes.", Panel::Voices),
                NP_SLIDER("vol_effects", "Effects", "%.0f", 90, 0, 100, 1, "Footsteps, doors, pickups, the base.", Panel::Voices),
                NP_SLIDER("vol_weapons", "Weapons", "%.0f", 85, 0, 100, 1, "Shots, reloads, melee.", Panel::Voices),
                NP_SLIDER("vol_voices", "Voices · Foundation and agents", "%.0f", 100, 0, 100, 1, "Foundation Command on the radio and your agent's own lines (all recorded in Voice Studio). Turn this up if you lose them under the hum; subtitles are in Accessibility.", Panel::Voices),
                NP_SLIDER("vol_monsters", "Monsters", "%.0f", 95, 0, 100, 1, "Every entity's sounds.", Panel::Voices),
                NP_SLIDER("vol_ambience", "Ambience · the hum, the lights", "%.0f", 80, 0, 100, 1, "The fluorescent hum, buzzing, the building around you.", Panel::Voices),
                NP_SLIDER("vol_ui", "Menus and HUD", "%.0f", 60, 0, 100, 1, "Menu clicks and HUD beeps.", Panel::Voices),
                NP_HEAD("OUTPUT"),
                NP_CHOICE("output", "Output device", "DEFAULT · HEADPHONES", 0, nullptr, "Where the sound goes.", Panel::Voices),
                NP_CHOICE("speakers", "Speaker setup", "HEADPHONES|STEREO|5.1 SURROUND|7.1 SURROUND", 0, nullptr, "Match what you listen on.", Panel::Voices),
                NP_TOGGLE("hrtf", "3D audio (HRTF)", 1, "On headphones: hear above, behind and below you.", Panel::Voices),
                NP_CHOICE("dynamicrange", "Dynamic range", "NIGHT|NORMAL|WIDE", 2, "Night: quieter loud bits", "Wide: the full jump from silence to gunfire. Night: loud sounds come down so quiet ones stay heard.", Panel::Voices),
                NP_TOGGLE("mutebackground", "Mute when the game is in the background", 1, "Silent while you're in another window.", Panel::Voices),
                NP_HEAD("CO-OP VOICE CHAT"),
                NP_SLIDER("vol_chat", "Voice chat volume", "%.0f", 80, 0, 100, 1, "Your co-op partner's voice.", Panel::Voices),
                NP_CHOICE("talk", "Talk", "PUSH TO TALK · T|OPEN MIC|OFF", 0, nullptr, "Push to talk sends your voice only while you hold the key.", Panel::Voices),
                NP_CHOICE("microphone", "Microphone", "DEFAULT", 0, nullptr, "Which microphone is used.", Panel::Voices),
                NP_BUTTON("testmic", "Test", "TEST MIC", "Speak and watch the bars; you'll hear yourself back.", Panel::Voices),
            },
            // ---- CONTROLS ----
            {
                NP_HEAD("MOUSE"),
                NP_SLIDER("mouse_sens", "Look sensitivity", "%.1f", 4.5, 0.1, 10, 0.1, "How far the view turns as you move the mouse.", Panel::Plain),
                NP_SLIDER("mouse_aim", "Aim sensitivity", "%.1f×", 0.8, 0.2, 2.0, 0.1, "Turning speed while aiming, compared with looking.", Panel::Plain),
                NP_TOGGLE("mouse_invert", "Invert look", 0, "Mouse up looks down.", Panel::Plain),
                NP_TOGGLE("mouse_raw", "Raw mouse input", 1, "Reads the mouse directly, without the system's acceleration.", Panel::Plain),
                NP_HEAD("HOLD OR TOGGLE"),
                NP_CHOICE("hold_sprint", "Sprint", "HOLD|TOGGLE", 0, nullptr, "Hold: sprint while the key is down. Toggle: press once to start, again to stop.", Panel::Plain),
                NP_CHOICE("hold_crouch", "Crouch", "HOLD|TOGGLE", 1, nullptr, "Hold: crouch while the key is down. Toggle: press once to start, again to stop.", Panel::Plain),
                NP_CHOICE("hold_aim", "Aim", "HOLD|TOGGLE", 0, nullptr, "Hold: aim while the button is down. Toggle: press once to start, again to stop.", Panel::Plain),
                NP_HEAD("KEYS"),
                NP_KEY("key_forward", "Move forward", "W|UP"), NP_KEY("key_back", "Move back", "S|DOWN"),
                NP_KEY("key_left", "Move left", "A|LEFT"), NP_KEY("key_right", "Move right", "D|RIGHT"),
                NP_KEY("key_jump", "Jump", "SPACE|"), NP_KEY("key_crouch", "Crouch", "C|L CTRL"),
                NP_KEY("key_sprint", "Sprint", "L SHIFT|"), NP_KEY("key_interact", "Interact · pick up", "E|"),
                NP_KEY("key_reload", "Reload", "R|"), NP_KEY("key_flashlight", "Flashlight", "F|"),
                NP_KEY("key_melee", "Melee", "V|"), NP_KEY("key_wheel", "Weapon wheel", "Q|"),
                NP_KEY("key_items", "Quick items", "1 2 3 4|"), NP_KEY("key_build", "Build · base", "TAB|"),
                NP_KEY("key_map", "Map", "M|"), NP_KEY("key_ping", "Ping · radio", "Z|"), NP_KEY("key_pause", "Pause", "ESC|"),
                NP_KEY("key_leanleft", "Lean left", "|"), NP_KEY("key_leanright", "Lean right", "|"),
                NP_KEY("key_inventory", "Inventory", "I|"), NP_KEY("key_notes", "Notes", "N|"),
                NP_KEY("key_photo", "Photo mode", "F12|"), NP_KEY("key_talk", "Voice chat", "T|"),
            },
            // ---- CONTROLLER ----
            {
                NP_HEAD("LAYOUT"),
                NP_CHOICE("pad_layout", "Button layout", "DEFAULT|SOUTHPAW|LEGACY|CUSTOM", 0, nullptr, "Which buttons do what. Custom: remap every button under Customise.", Panel::Gyro),
                NP_CHOICE("pad_prompts", "Button prompts", "AUTO · XBOX|XBOX|PLAYSTATION|STEAM DECK|SWITCH", 0, nullptr, "The button pictures shown in the game. Auto matches the controller in your hand.", Panel::Gyro),
                NP_CHOICE("pad_sticks", "Stick layout", "DEFAULT|SOUTHPAW", 0, nullptr, "Southpaw swaps moving and looking between the sticks.", Panel::Gyro),
                NP_HEAD("LOOK"),
                NP_SLIDER("pad_look_h", "Look sensitivity · horizontal", "%.0f", 5, 1, 10, 1, "Turning speed left and right.", Panel::Gyro),
                NP_SLIDER("pad_look_v", "Look sensitivity · vertical", "%.0f", 4, 1, 10, 1, "Turning speed up and down.", Panel::Gyro),
                NP_SLIDER("pad_aim", "Aim sensitivity", "%.1f×", 0.7, 0.2, 2.0, 0.1, "Turning speed while aiming, compared with looking.", Panel::Gyro),
                NP_CHOICE("pad_curve", "Response curve", "LINEAR|EXPONENTIAL|DYNAMIC", 2, nullptr, "How the stick's tilt becomes turning. Dynamic: fine at the centre, fast at the edge.", Panel::Gyro),
                NP_TOGGLE("pad_invert", "Invert look", 0, "Stick up looks down.", Panel::Gyro),
                NP_SLIDER("pad_dead_l", "Left stick deadzone", "%.0f%%", 6, 0, 30, 1, "How far the left stick moves before you do. Raise it if you drift.", Panel::Gyro),
                NP_SLIDER("pad_dead_r", "Right stick deadzone", "%.0f%%", 8, 0, 30, 1, "How far the right stick moves before the view does.", Panel::Gyro),
                NP_HEAD("FEEL"),
                NP_SLIDER("pad_rumble", "Vibration", "%.0f%%", 80, 0, 100, 5, "Rumble for shots, hits and monsters close by.", Panel::Gyro),
                NP_TOGGLE("pad_triggers", "Trigger effects (DualSense)", 1, "Triggers push back on a DualSense: heavier for the shotgun, a click for the pistol.", Panel::Gyro),
                NP_CHOICE("pad_assist", "Aim assist", "OFF|SLOWDOWN ONLY|FULL", 1, nullptr, "Slowdown: the aim slows over a target. Full: it also follows them a little.", Panel::Gyro),
                NP_CHOICE("pad_gyro", "Gyro aiming", "OFF|WHEN AIMING|ALWAYS", 1, nullptr, "Aim by moving the controller or handheld (Steam Deck, ROG Ally, DualSense, Switch Pro). When aiming: only while you hold aim; Always: all the time. The sticks still work.", Panel::Gyro),
                NP_SLIDER("pad_gyro_sens", "Gyro sensitivity", "%.1f×", 1.0, 0.2, 3.0, 0.1, "How far the view turns as you tilt.", Panel::Gyro),
                NP_BUTTON("pad_customise", "Remap buttons", "CUSTOMISE", "Pick any action and press the button you want for it.", Panel::Gyro),
            },
            // ---- GAMEPLAY ----
            {
                NP_HEAD("CHALLENGE"),
                NP_CHOICE("difficulty", "Difficulty", "RECRUIT|AGENT|VETERAN|KETER", 1, "Change any time", "How hard the Backrooms push back. You can change it any time; the save remembers each change.", Panel::Difficulty),
                NP_CHOICE("aggression", "Monster aggression", "LOW|NORMAL|HIGH", 1, nullptr, "How quickly monsters notice you and how long they hunt.", Panel::Difficulty),
                NP_CHOICE("sanityfx", "Sanity effects", "REDUCED|FULL", 1, nullptr, "How hard low sanity hits: the grain, the whispers, things that aren't there.", Panel::Difficulty),
                NP_CHOICE("resources", "Resources found", "SCARCE|NORMAL|PLENTY", 1, nullptr, "How much ammo, Almond Water and scrap you find.", Panel::Difficulty),
                NP_TOGGLE("onelife", "One life (no new agents)", 0, "When your agent dies the run ends: no next agent through the breach.", Panel::Difficulty),
                NP_HEAD("HUD"),
                NP_CHOICE("hud", "HUD", "FULL|MINIMAL|IMMERSIVE", 0, "Minimal · Immersive", "Full: everything. Minimal: only what matters right now. Immersive: nothing on screen but prompts.", Panel::Plain),
                NP_TOGGLE("hud_compass", "Compass", 1, "The heading strip at the top.", Panel::Plain),
                NP_TOGGLE("hud_objective", "Objective", 1, "What you're doing, top right.", Panel::Plain),
                NP_TOGGLE("hud_wrist", "Wrist unit", 1, "Health, stamina, sanity and the pulse, bottom left.", Panel::Plain),
                NP_TOGGLE("hud_ammo", "Ammo and weapon", 1, "The gun, rounds and quick items, bottom right.", Panel::Plain),
                NP_CHOICE("crosshair", "Crosshair", "OFF|DOT|CROSS|CIRCLE", 1, nullptr, "The aim mark in the middle.", Panel::Plain),
                NP_TOGGLE("hud_hitmarkers", "Hit markers", 1, "A tick when your shot lands.", Panel::Plain),
                NP_TOGGLE("hud_prompts", "Interaction prompts", 1, "\"E · pick up\" when you're next to something.", Panel::Plain),
                NP_HEAD("CAMERA"),
                NP_SLIDER("camshake", "Camera shake", "%.0f%%", 70, 0, 100, 5, "Shaking from hits, monsters and shots.", Panel::Plain),
                NP_SLIDER("headbob", "Head bob", "%.0f%%", 60, 0, 100, 5, "The view's sway as you walk and run.", Panel::Plain),
                NP_CHOICE("chatter", "Agent chatter", "NORMAL|LESS|OFF", 0, "Less · Off", "How often your agent talks to themselves.", Panel::Plain),
                NP_TOGGLE("hints", "Tutorial hints", 1, "Tips the first time something comes up.", Panel::Plain),
                NP_CHOICE("units", "Units", "IMPERIAL · FT|METRIC · M", 0, nullptr, "Distances on the compass and objectives.", Panel::Plain),
                NP_CHOICE("language", "Language", "ENGLISH", 0, nullptr, "Text and subtitles.", Panel::Plain),
            },
            // ---- ACCESSIBILITY ----
            {
                NP_HEAD("SUBTITLES"),
                NP_TOGGLE("subtitles", "Subtitles", 1, "Words for everything said.", Panel::Captions),
                NP_CHOICE("subsize", "Subtitle size", "SMALL|NORMAL|LARGE|HUGE", 2, nullptr, "Size of the subtitles.", Panel::Captions),
                NP_SLIDER("subbg", "Subtitle background", "%.0f%%", 70, 0, 100, 5, "The dark band behind them.", Panel::Captions),
                NP_TOGGLE("subnames", "Speaker names", 1, "Who's talking, before the words.", Panel::Captions),
                NP_TOGGLE("captions", "Sound captions", 1, "Writes the important sounds on screen too: footsteps, growls, a door, the hum cutting out, and which side they come from. Works with subtitles or alone.", Panel::Captions),
                NP_TOGGLE("sounddir", "Direction of sounds on screen", 0, "Arcs round the crosshair point at loud sounds.", Panel::Captions),
                NP_HEAD("SEEING"),
                NP_CHOICE("colorblind", "Colour-blind mode", "OFF|PROTAN|DEUTAN|TRITAN", 0, "Protan · Deutan · Tritan", "Shifts the colours the game uses for health, danger and objectives.", Panel::Captions),
                NP_SLIDER("cbstrength", "Colour-blind strength", "%.0f%%", 0, 0, 100, 5, "How strong the colour shift is.", Panel::Captions),
                NP_CHOICE("textsize", "Text size", "NORMAL|LARGE|LARGER", 0, nullptr, "Size of the HUD and menu text.", Panel::Captions),
                NP_TOGGLE("contrast", "High-contrast HUD", 0, "Solid backgrounds and brighter text on the HUD.", Panel::Captions),
                NP_HEAD("COMFORT"),
                NP_TOGGLE("noflash", "Reduce flashing lights", 0, "Calms the flicker in Level 0 and the Electrical Station.", Panel::Captions),
                NP_TOGGLE("nodistort", "Reduce sanity distortion", 0, "Keeps the picture steady when sanity is low.", Panel::Captions),
                NP_TOGGLE("arachno", "Arachnophobia mode", 0, "Arachnophobia mode turns the Crawler into a shape with no legs.", Panel::Captions),
                NP_CHOICE("motionsick", "Motion sickness", "OFF|ON", 0, "Steadier camera", "A steadier camera: no head bob or shake, a dot in the middle.", Panel::Captions),
                NP_HEAD("INPUT"),
                NP_TOGGLE("holdpress", "Hold buttons become presses", 0, "Anything you'd hold is a single press instead.", Panel::Captions),
                NP_TOGGLE("autosprint", "Auto-sprint", 0, "Push forward fully and you sprint.", Panel::Captions),
            },
        };
        return tabs[AZStd::clamp(tab, 0, 6)];
    }
#undef NP_HEAD
#undef NP_CHOICE
#undef NP_SLIDER
#undef NP_TOGGLE
#undef NP_KEY
#undef NP_BUTTON

    inline AZStd::vector<AZStd::string> Split(const char* text)
    {
        AZStd::vector<AZStd::string> parts;
        AZStd::string current;
        for (const char* c = text; c && *c; ++c)
        {
            if (*c == '|') { parts.push_back(current); current.clear(); }
            else { current += *c; }
        }
        parts.push_back(current);
        return parts;
    }

    inline AZStd::string Path(const char* id) { return AZStd::string("/NoclipProtocol/Settings/") + id; }

    //! A setting's saved value (or its default): choices as their index, toggles 0/1, sliders as the number.
    inline double Number(const char* id)
    {
        for (int t = 0; t < 7; ++t)
        {
            for (const Line& line : TabLines(t))
            {
                if (line.m_kind != Kind::Head && AZStd::string_view(line.m_id) == id)
                {
                    double value = line.m_default;
                    if (auto* r = AZ::SettingsRegistry::Get())
                    {
                        if (line.m_kind == Kind::Toggle)
                        {
                            bool b = value != 0.0;
                            if (r->Get(b, Path(id))) { value = b ? 1.0 : 0.0; }
                        }
                        else
                        {
                            r->Get(value, Path(id));
                        }
                    }
                    return value;
                }
            }
        }
        return 0.0;
    }
    inline bool On(const char* id) { return Number(id) != 0.0; }
    inline int Index(const char* id) { return static_cast<int>(std::lround(Number(id))); }

    //! A key's binding: slot 0 primary, 1 secondary ("" = none).
    inline AZStd::string KeyName(const char* id, int slot)
    {
        for (const Line& line : TabLines(3))
        {
            if (line.m_kind == Kind::Key && AZStd::string_view(line.m_id) == id)
            {
                AZStd::string value = Split(line.m_options)[slot];
                if (auto* r = AZ::SettingsRegistry::Get())
                {
                    AZStd::string saved;
                    if (r->Get(saved, Path(id) + (slot == 0 ? "/0" : "/1")))
                    {
                        value = saved == "-" ? AZStd::string() : saved;
                    }
                }
                return value;
            }
        }
        return {};
    }

    //! A volume (0..100 slider) as a 0..1 gain, Master included.
    inline float Gain(const char* volumeId)
    {
        return static_cast<float>(Number(volumeId) / 100.0 * Number("vol_master") / 100.0);
    }
} // namespace NoclipProtocol::GameSettings
