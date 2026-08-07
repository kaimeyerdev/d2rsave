// Canonical Diablo II: Resurrected item-name color palette.
//
// D2R (like classic D2) tags each item-name string with a text-color code
// (`ÿc<char>`) that maps to a fixed RGB palette. This header re-creates
// that palette as `ftxui::Color` values so the terminal dashboard can
// render item names with the same visual language players know from
// in-game: gold uniques, green sets, yellow rares, blue magic, white
// normals.
//
// Two variants per palette entry:
//   * `Full`   -- the canonical bright color, used for "unowned" /
//                 "undiscovered" items so they stand out as things to
//                 chase.
//   * `Muted`  -- a darkened variant (~35-45% brightness) used to
//                 indicate ownership without dropping the type-color
//                 signal entirely. Replaces the previous `| dim` (which
//                 collapsed every quality to the same grey).
//
// Only depends on ftxui; only pulled in by translation units that
// already link ftxui (currently just `dashboard_ftxui.cpp`).
//
// Colors are exposed as `inline` accessor functions (not `inline const`
// globals) because `ftxui::Color::RGB(...)`'s ctor calls into
// `Terminal::ColorSupport()`, which touches ftxui's own static state.
// Constructing palette colors during our TU's static init hits the
// classic init-order fiasco (crashes release builds with SIGSEGV inside
// `Terminal::Quirks::SetColorSupport`). Function-local statics defer
// construction to first use, safely after `main()` starts.

#pragma once

#include "d2r/DashboardModel.hpp"   // ChronicleKind
#include "d2r/Item.hpp"             // ItemQuality

#include <ftxui/screen/color.hpp>

namespace d2r::item_colors {

// ---------------------------------------------------------------------------
// Canonical D2R text-color palette. RGB values match the classic D2/D2R
// tag colors (ÿc0..ÿc9, ÿc; etc.) that the game uses for item-name text.
// Each accessor is a Meyers-singleton wrapper to avoid the static-init
// order fiasco with ftxui's terminal-detection state.
// ---------------------------------------------------------------------------

// ÿc0 White -- Normal / Superior / Inferior item names.
[[nodiscard]] inline const ftxui::Color& normalFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0xFF, 0xFF, 0xFF);
    return c;
}
[[nodiscard]] inline const ftxui::Color& normalMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x60, 0x60, 0x60);
    return c;
}

// ÿc3 Blue -- Magic item names.
[[nodiscard]] inline const ftxui::Color& magicFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0x69, 0x69, 0xFF);
    return c;
}
[[nodiscard]] inline const ftxui::Color& magicMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x2D, 0x2D, 0x6E);
    return c;
}

// ÿc9 Yellow -- Rare item names.
[[nodiscard]] inline const ftxui::Color& rareFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0xFF, 0xFF, 0x64);
    return c;
}
[[nodiscard]] inline const ftxui::Color& rareMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x6E, 0x6E, 0x2A);
    return c;
}

// ÿc2 Green -- Set item names.
[[nodiscard]] inline const ftxui::Color& setFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0x00, 0xFF, 0x00);
    return c;
}
[[nodiscard]] inline const ftxui::Color& setMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x00, 0x6E, 0x00);
    return c;
}

// ÿc4 Gold/Tan -- Unique item names.
[[nodiscard]] inline const ftxui::Color& uniqueFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0xC7, 0xB3, 0x77);
    return c;
}
[[nodiscard]] inline const ftxui::Color& uniqueMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x58, 0x4F, 0x34);
    return c;
}

// ÿc8 Orange -- Crafted item names.
[[nodiscard]] inline const ftxui::Color& craftFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0xFF, 0x88, 0x00);
    return c;
}
[[nodiscard]] inline const ftxui::Color& craftMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x6E, 0x3B, 0x00);
    return c;
}

// ÿc7 Tan/gold-orange -- Runeword item names (distinct from unique gold).
[[nodiscard]] inline const ftxui::Color& runewordFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0xFF, 0xA8, 0x00);
    return c;
}
[[nodiscard]] inline const ftxui::Color& runewordMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x6E, 0x48, 0x00);
    return c;
}

// ÿc1 Red -- Ethereal / negative modifiers (not a base quality but useful
// when we want to flag an ethereal item).
[[nodiscard]] inline const ftxui::Color& etherealFull() {
    static const ftxui::Color c = ftxui::Color::RGB(0xC8, 0x64, 0x64);
    return c;
}
[[nodiscard]] inline const ftxui::Color& etherealMuted() {
    static const ftxui::Color c = ftxui::Color::RGB(0x58, 0x2C, 0x2C);
    return c;
}

// ---------------------------------------------------------------------------
// Lookups.
// ---------------------------------------------------------------------------

// Color for an item's quality tier. `owned == true` returns the muted
// variant so ownership is conveyed through desaturation instead of the
// old flat `dim` (which collapsed every quality to grey). Superior and
// Inferior fall through to Normal white -- in-game their name text is
// still white, only the prefix/suffix changes.
[[nodiscard]] inline ftxui::Color forQuality(ItemQuality q, bool owned = false) noexcept {
    switch (q) {
        case ItemQuality::Magic:  return owned ? magicMuted()  : magicFull();
        case ItemQuality::Rare:   return owned ? rareMuted()   : rareFull();
        case ItemQuality::Set:    return owned ? setMuted()    : setFull();
        case ItemQuality::Unique: return owned ? uniqueMuted() : uniqueFull();
        case ItemQuality::Craft:  return owned ? craftMuted()  : craftFull();
        case ItemQuality::None:
        case ItemQuality::Inferior:
        case ItemQuality::Normal:
        case ItemQuality::Superior:
        case ItemQuality::Unknown:
        default:                  return owned ? normalMuted() : normalFull();
    }
}

// Color for a chronicle row's kind. Same ownership rules as `forQuality`.
[[nodiscard]] inline ftxui::Color forKind(ChronicleKind k, bool owned = false) noexcept {
    switch (k) {
        case ChronicleKind::Unique:   return owned ? uniqueMuted()   : uniqueFull();
        case ChronicleKind::Set:      return owned ? setMuted()      : setFull();
        case ChronicleKind::Runeword: return owned ? runewordMuted() : runewordFull();
    }
    return owned ? normalMuted() : normalFull();
}

}  // namespace d2r::item_colors
