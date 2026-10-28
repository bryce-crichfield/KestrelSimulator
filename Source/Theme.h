// Theme.h -- every color, size and font the editor draws with, ImGui and the imnodes canvas alike.
// The look is Kestrel's: neutral near-black grays and an orange highlight.

#pragma once

#include "imgui.h"

#include "IconsFontAwesome6.h"

#include <cstdint>

namespace Kestrel::Theme {

// 0xRRGGBB (and an alpha) as ImGui's packed color.
constexpr ImU32 Rgb(uint32_t rgb, uint32_t alpha = 255) {
    return ((rgb >> 16) & 0xFF) << IM_COL32_R_SHIFT | ((rgb >> 8) & 0xFF) << IM_COL32_G_SHIFT |
           (rgb & 0xFF) << IM_COL32_B_SHIFT | (alpha & 0xFF) << IM_COL32_A_SHIFT;
}
inline ImVec4 Vec(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}
// color with its alpha replaced.
constexpr ImU32 Alpha(ImU32 color, uint32_t alpha) {
    return (color & ~IM_COL32_A_MASK) | (alpha & 0xFF) << IM_COL32_A_SHIFT;
}

// ---------------------------------------------------------------------------
// The palette. Everything below is made from these.

constexpr ImU32 BACKGROUND_COLOR = Rgb(0x111113);      // behind everything; the canvas
constexpr ImU32 PANEL_COLOR = Rgb(0x19191c);           // windows and side panels
constexpr ImU32 TOOLBAR_COLOR = Rgb(0x151518);         // the toolbar under the menu bar
constexpr ImU32 SURFACE_COLOR = Rgb(0x26262a);         // frames: inputs, buttons, headers
constexpr ImU32 SURFACE_HOVER_COLOR = Rgb(0x313136);
constexpr ImU32 SURFACE_ACTIVE_COLOR = Rgb(0x3b3b41);
constexpr ImU32 BORDER_COLOR = Rgb(0x2e2e33);

constexpr ImU32 TEXT_COLOR = Rgb(0xe4e4e7);            // neutral off-white
constexpr ImU32 TEXT_DIM_COLOR = Rgb(0x9a9aa2);        // hints and secondary text
constexpr ImU32 TEXT_DISABLED_COLOR = Rgb(0x5c5c63);

constexpr ImU32 ACCENT_COLOR = Rgb(0xff8a1f);          // Kestrel orange: selection, focus, the active thing
constexpr ImU32 ACCENT_DIM_COLOR = Rgb(0xd9711a);
constexpr ImU32 ACCENT_DEEP_COLOR = Rgb(0x6b3410);     // orange backgrounds behind text

constexpr ImU32 SUCCESS_COLOR = Rgb(0x4cc38a);
constexpr ImU32 WARNING_COLOR = Rgb(0xf5b93a);
constexpr ImU32 ERROR_COLOR = Rgb(0xef4b4b);
constexpr ImU32 INFO_COLOR = Rgb(0x5fa8e6);            // file links and other "where from" notes

// ---------------------------------------------------------------------------
// Logic levels, on wires, pins, switches and in the panels.

constexpr ImU32 LEVEL_HIGH_COLOR = Rgb(0x4cc38a);
constexpr ImU32 LEVEL_LOW_COLOR = Rgb(0x3a3f44);
constexpr ImU32 LEVEL_FLOATING_COLOR = Rgb(0x4a78b0);
constexpr ImU32 LEVEL_CONFLICT_COLOR = ERROR_COLOR;

constexpr ImU32 LED_ON_COLOR = Rgb(0xef4b4b);
constexpr ImU32 LED_OFF_COLOR = Rgb(0x3a1e1e);
constexpr ImU32 LED_GLOW_COLOR = Rgb(0xff8a1f, 120);

// ---------------------------------------------------------------------------
// The canvas (imnodes).

constexpr ImU32 CANVAS_COLOR = BACKGROUND_COLOR;
constexpr ImU32 GRID_LINE_COLOR = Rgb(0x1d1d21);
constexpr ImU32 GRID_LINE_PRIMARY_COLOR = Rgb(0x2a2a2f);
constexpr ImU32 NODE_COLOR = Rgb(0x1c1c20);
constexpr ImU32 NODE_HOVER_COLOR = Rgb(0x232328);
constexpr ImU32 NODE_SELECTED_COLOR = Rgb(0x2a2a30);
constexpr ImU32 NODE_OUTLINE_COLOR = Rgb(0x3b3b41);
constexpr ImU32 WIRE_COLOR = Rgb(0x8c8c94);            // wires while editing; simulating, they show their level
constexpr ImU32 WIRE_HOVER_COLOR = ACCENT_DIM_COLOR;
constexpr ImU32 WIRE_SELECTED_COLOR = ACCENT_COLOR;
constexpr ImU32 PIN_COLOR = Rgb(0x8c8c94);
constexpr ImU32 PIN_HOVER_COLOR = ACCENT_COLOR;
constexpr ImU32 BOX_SELECTOR_COLOR = Rgb(0xff8a1f, 40);
constexpr ImU32 BOX_SELECTOR_OUTLINE_COLOR = Rgb(0xff8a1f, 160);

// Node title bars, by kind of node.
constexpr ImU32 TITLE_INPUT_COLOR = Rgb(0x2f6b4a);
constexpr ImU32 TITLE_OUTPUT_COLOR = Rgb(0x8c3030);
constexpr ImU32 TITLE_COMPONENT_COLOR = Rgb(0x2d5a8a);
constexpr ImU32 TITLE_MISSING_COLOR = Rgb(0xb3261e);   // a component that isn't in the library
constexpr ImU32 TITLE_CLOCK_COLOR = Rgb(0xb0601a);
constexpr ImU32 TITLE_FLIP_FLOP_COLOR = Rgb(0x6a4a8c);
constexpr ImU32 TITLE_GATE_COLOR = Rgb(0x3d3d44);
constexpr int TITLE_HOVER_LIGHTEN = 25;
constexpr int TITLE_SELECTED_LIGHTEN = 45;
constexpr int WIRE_HOVER_LIGHTEN = 30;

// The mode chip on the toolbar.
constexpr ImU32 CHIP_EDIT_COLOR = Rgb(0xff8a1f, 40);
constexpr ImU32 CHIP_SIMULATE_COLOR = Rgb(0x4cc38a, 40);

// The address the memory window is showing the circuit read.
constexpr ImU32 MEMORY_CURRENT_COLOR = Rgb(0xff8a1f, 90);

// ---------------------------------------------------------------------------
// Fonts. The files are copied into Binary/Fonts when CMake configures.

constexpr const char* FONT_FILE = "JetBrainsMono-Regular.ttf";
constexpr const char* FONT_BOLD_FILE = "JetBrainsMono-Bold.ttf";
constexpr const char* ICON_FONT_FILE = FONT_ICON_FILE_NAME_FAS;
constexpr float FONT_SIZE = 15.0f;
constexpr float ICON_SIZE = 13.0f;                     // a little smaller than the text, so they sit on its line
constexpr float ICON_OFFSET_Y = 0.0f;

extern ImFont* regularFont;
extern ImFont* boldFont;                               // section titles

// ---------------------------------------------------------------------------
// Sizes and spacing, at zoom 1.

constexpr ImVec2 WINDOW_PADDING(10, 10);
constexpr ImVec2 FRAME_PADDING(7, 4);
constexpr ImVec2 ITEM_SPACING(8, 6);
constexpr ImVec2 ITEM_INNER_SPACING(6, 4);
constexpr ImVec2 CELL_PADDING(5, 3);
constexpr float INDENT_SPACING = 18;
constexpr float SCROLLBAR_SIZE = 12;
constexpr float GRAB_MIN_SIZE = 10;

constexpr float WINDOW_ROUNDING = 4;
constexpr float CHILD_ROUNDING = 3;
constexpr float FRAME_ROUNDING = 3;
constexpr float POPUP_ROUNDING = 4;
constexpr float SCROLLBAR_ROUNDING = 6;
constexpr float GRAB_ROUNDING = 2;
constexpr float TAB_ROUNDING = 3;
constexpr float WINDOW_BORDER = 1;
constexpr float FRAME_BORDER = 0;
constexpr float POPUP_BORDER = 1;
constexpr float SEPARATOR_TEXT_BORDER = 2;
constexpr ImVec2 SEPARATOR_TEXT_PADDING(0, 4);

// Inside canvas nodes; scaled by the zoom.
constexpr ImVec2 CANVAS_ITEM_SPACING(8, 4);
constexpr ImVec2 CANVAS_FRAME_PADDING(4, 3);
constexpr ImVec2 NODE_PADDING(8, 6);
constexpr float NODE_ROUNDING = 4;
constexpr float NODE_BORDER = 1;
constexpr float GRID_SPACING = 24;
constexpr float LINK_THICKNESS = 3;
constexpr float BUS_THICKNESS_SCALE = 2.5f;            // a bus wire is this much thicker than a single one
constexpr float WIRE_LABEL_SCALE = 0.85f;              // wire-label tags' text, against the canvas font
constexpr ImU32 WIRE_LABEL_BACKGROUND_COLOR = Rgb(0x1c1c20, 230);
constexpr float PIN_RADIUS = 4;
constexpr float PIN_QUAD_SIZE = 8;

// Side panels, until they are dragged wider or narrower.
constexpr float PROPERTIES_WIDTH = 300;
constexpr float SIMULATION_WIDTH = 320;

// The toolbar.
constexpr ImVec2 TOOLBAR_PADDING(8, 6);
constexpr float TOOLBAR_BUTTON_SPACING = 2;            // between buttons in a group
constexpr float TOOLBAR_GROUP_SPACING = 8;             // either side of a separator
constexpr float TOOLBAR_SLIDER_WIDTH = 150;
constexpr float ICON_BUTTON_PADDING = 4;               // either side of an icon-only button's icon
constexpr float COMPONENT_PICKER_WIDTH = 220;
constexpr float CHIP_ROUNDING = 10;                    // the mode chip is a pill
constexpr float ZOOM_STEP = 1.25f;                     // the toolbar's zoom buttons

constexpr float SPLITTER_GRIP = 8;                     // the draggable width between panels
constexpr float SPLITTER_LINE = 3;
constexpr float LED_GLOW_THICKNESS = 2;

// Loads the fonts into ImGui's atlas: call between rlImGuiBeginInitImGui and rlImGuiEndInitImGui.
void LoadFonts();
// Sets ImGui's and imnodes' styles from the values above. Needs both contexts.
void Apply();
// SeparatorText in the bold font and the accent color.
void SectionTitle(const char* text);
// Lightens each channel of color by amount.
ImU32 Lighten(ImU32 color, int amount);

}  // namespace Kestrel::Theme
