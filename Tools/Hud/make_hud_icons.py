#!/usr/bin/env python3
"""
Writes Gem/Source/Hud/HudIcons.h: the player UI's 24 x 24 icons (weapons, items, gear), traced from the approved
Player UI mockups' SVG icons (hearth-workspace noclip-protocol/mockups/ui-and-hearth-engine/generator/gen_player_ui.py).

    python3 Tools/Hud/make_hud_icons.py        (run from the project folder)
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "Base"))
sys.path.insert(0, os.path.join(HERE, "..", "Clutter"))
import make_base_pieces as mbp  # noqa: E402  (its SVG tracer)

# The mockups' icons (gen_player_ui.py ICONS), copied as they are.
ICONS = {
    "pistol": '<path d="M3 9 H19 V12 H13 L12 18 H8 L9 12 H3 Z M19 10 H21" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "shotgun": '<path d="M1 10 H17 L19 9 H23 V12 H15 L13 16 H9 L10 12 H1 Z" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "carbine": '<path d="M1 10 H15 V8 H18 V10 H23 V12 H14 L13 17 H10 V12 H4 L3 14 H1 Z" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "flaregun": '<path d="M3 8 H17 A3 3 0 0 1 17 13 H12 L11 19 H7 L8 13 H3 Z" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="20" cy="10" r="1.5" fill="{c}"/>',
    "knife": '<path d="M3 15 L15 5 L20 4 L19 9 L8 18 Z M5 17 L7 19" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "shield": '<path d="M12 3 L20 6 V12 C20 17 16 20 12 21 C8 20 4 17 4 12 V6 Z M9 9 H15" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "water": '<path d="M9 3 H15 V6 L17 9 V20 H7 V9 L9 6 Z M7 13 H17" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "battery": '<rect x="4" y="7" width="15" height="10" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M19 10 H21 V14 H19 M7 10 V14 M10 10 V14 M13 10 V14" stroke="{c}" stroke-width="1.6"/>',
    "flare": '<path d="M9 21 V9 H15 V21 Z M10 9 L12 3 L14 9" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "medkit": '<rect x="3" y="7" width="18" height="13" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M9 7 V4 H15 V7 M12 10 V17 M8.5 13.5 H15.5" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "bandage": '<path d="M4 16 L16 4 L20 8 L8 20 Z M9 11 L13 15 M11 9 L15 13" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "doc": '<path d="M6 3 H15 L19 7 V21 H6 Z M15 3 V7 H19 M9 11 H16 M9 14 H16 M9 17 H13" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "sample": '<path d="M9 3 H15 M10 3 V9 L6 19 Q6 21 7.5 21 H16.5 Q18 21 18 19 L14 9 V3 M8 15 H16" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "scrap": '<path d="M3 15 L9 8 L14 11 L21 6 L19 17 L11 19 Z" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "wire": '<circle cx="12" cy="12" r="7" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="12" cy="12" r="3" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M19 12 H23" stroke="{c}" stroke-width="1.6"/>',
    "fuel": '<path d="M6 5 H15 L18 8 V21 H6 Z M9 5 V3 H13 V5 M9 12 L15 18 M15 12 L9 18" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "helmet": '<path d="M4 15 C4 8 8 5 12 5 C16 5 20 8 20 15 Z M2 15 H22 M8 15 V18 H16 V15" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "vest": '<path d="M8 3 L12 6 L16 3 L20 6 V21 H4 V6 Z M8 12 H16 M8 16 H16" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "mask": '<path d="M5 9 C5 5 19 5 19 9 V13 C19 17 15 19 12 19 C9 19 5 17 5 13 Z" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="9" cy="11" r="2" fill="none" stroke="{c}" stroke-width="1.4"/><circle cx="15" cy="11" r="2" fill="none" stroke="{c}" stroke-width="1.4"/><path d="M10 16 H14" stroke="{c}" stroke-width="1.6"/>',
    "nv": '<rect x="3" y="8" width="18" height="8" fill="none" stroke="{c}" stroke-width="1.6"/><circle cx="8" cy="12" r="2" fill="none" stroke="{c}" stroke-width="1.4"/><circle cx="16" cy="12" r="2" fill="none" stroke="{c}" stroke-width="1.4"/><path d="M8 8 V5 H16 V8" fill="none" stroke="{c}" stroke-width="1.4"/>',
    "radio": '<rect x="6" y="8" width="12" height="13" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M10 8 V2 M9 12 H15 M9 15 H15" stroke="{c}" stroke-width="1.6"/>',
    "tracker": '<circle cx="12" cy="12" r="8" fill="none" stroke="{c}" stroke-width="1.5"/><path d="M12 12 L17 7" stroke="{c}" stroke-width="1.6"/><circle cx="15" cy="15" r="1.4" fill="{c}"/>',
    "light": '<path d="M3 9 H13 L19 5 V19 L13 15 H3 Z" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "shells": '<rect x="5" y="6" width="5" height="14" fill="none" stroke="{c}" stroke-width="1.5"/><rect x="14" y="6" width="5" height="14" fill="none" stroke="{c}" stroke-width="1.5"/><path d="M5 16 H10 M14 16 H19" stroke="{c}" stroke-width="1.5"/>',
    "rounds": '<path d="M7 20 V10 Q7 5 9 4 Q11 5 11 10 V20 Z M13 20 V10 Q13 5 15 4 Q17 5 17 10 V20 Z" fill="none" stroke="{c}" stroke-width="1.5"/>',
    "lock": '<rect x="6" y="11" width="12" height="9" fill="none" stroke="{c}" stroke-width="1.6"/><path d="M8.5 11 V8 A3.5 3.5 0 0 1 15.5 8 V11" fill="none" stroke="{c}" stroke-width="1.6"/>',
    "foundation": '<circle cx="12" cy="12" r="9" fill="none" stroke="{c}" stroke-width="1.4"/><circle cx="12" cy="12" r="4.5" fill="none" stroke="{c}" stroke-width="1.4"/><path d="M12 1.5 V5 M12 19 V22.5 M2.9 6.8 L5.9 8.5 M18.1 15.5 L21.1 17.2 M2.9 17.2 L5.9 15.5 M18.1 8.5 L21.1 6.8" stroke="{c}" stroke-width="1.6"/>',
}
NAMES = list(ICONS)


def main():
    lines = ["#pragma once", "", "// Generated by Tools/Hud/make_hud_icons.py: the player UI's 24 x 24 icons, traced from the approved",
             "// Player UI mockups' SVG icons. Do not edit by hand.", "", "namespace NoclipProtocol::HudIcons", "{",
             "    struct Stroke { float m_width; bool m_dashed; int m_first; int m_count; };",
             "    struct Fill { float m_x, m_y, m_r; };",
             "    struct Icon { int m_firstStroke; int m_strokes; int m_firstFill; int m_fills; };", ""]
    points, strokes, fills, icons = [], [], [], []
    for name in NAMES:
        st, fi = mbp.icon_data(ICONS[name])
        icons.append((len(strokes), len(st), len(fills), len(fi)))
        for width, dashed, line in st:
            strokes.append((width, dashed, len(points), len(line)))
            points.extend(line)
        fills.extend(fi)
    lines.append(f"    constexpr int IconCount = {len(NAMES)};")
    for k, name in enumerate(NAMES):
        lines.append(f"    constexpr int Icon_{name} = {k};")
    lines.append(f"    constexpr float Points[{len(points)}][2] = {{")
    for k in range(0, len(points), 6):
        lines.append("        " + " ".join(f"{{ {x:.2f}f, {y:.2f}f }}," for x, y in points[k:k + 6]))
    lines.append("    };")
    lines.append(f"    constexpr Stroke Strokes[{len(strokes)}] = {{")
    for width, dashed, first, count in strokes:
        lines.append(f"        {{ {width:.1f}f, {'true' if dashed else 'false'}, {first}, {count} }},")
    lines.append("    };")
    lines.append(f"    constexpr Fill Fills[{max(1, len(fills))}] = {{")
    for x, y, r in fills or [(0, 0, 0)]:
        lines.append(f"        {{ {x:.2f}f, {y:.2f}f, {r:.2f}f }},")
    lines.append("    };")
    lines.append("    constexpr Icon Icons[IconCount] = {")
    for (a, b, c, d), name in zip(icons, NAMES):
        lines.append(f"        {{ {a}, {b}, {c}, {d} }}, // {name}")
    lines.append("    };")
    # name lookup
    lines.append("    inline int Find(const char* name)")
    lines.append("    {")
    lines.append("        static const char* const names[IconCount] = { " + ", ".join(f'"{n}"' for n in NAMES) + " };")
    lines.append("        for (int i = 0; i < IconCount; ++i)")
    lines.append("        {")
    lines.append("            const char* a = names[i];")
    lines.append("            const char* b = name;")
    lines.append("            while (*a && *a == *b) { ++a; ++b; }")
    lines.append("            if (*a == 0 && *b == 0) { return i; }")
    lines.append("        }")
    lines.append("        return -1;")
    lines.append("    }")
    lines += ["} // namespace NoclipProtocol::HudIcons", ""]
    with open(mbp.path("Gem/Source/Hud/HudIcons.h"), "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {len(NAMES)} icons")


if __name__ == "__main__":
    main()
