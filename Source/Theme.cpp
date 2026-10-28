// Theme.cpp -- the editor's fonts and styles. See Theme.h.

#include "Theme.h"

#include "imnodes.h"
#include "raylib.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace Kestrel::Theme {

ImFont* regularFont = nullptr;
ImFont* boldFont = nullptr;

static std::string FontPath(const char* file) {
    return std::string(GetApplicationDirectory()) + "Fonts/" + file;
}

// A text font with the icons merged into it; the default font if the file is missing.
static ImFont* AddFont(const char* file, float size) {
    ImGuiIO& io = ImGui::GetIO();
    std::string path = FontPath(file);
    ImFontConfig config;
    config.PixelSnapH = true;
    ImFont* font = FileExists(path.c_str()) ? io.Fonts->AddFontFromFileTTF(path.c_str(), size, &config) : nullptr;
    if (!font) {
        fprintf(stderr, "Missing font %s; using the default\n", path.c_str());
        config.SizePixels = size;
        font = io.Fonts->AddFontDefault(&config);
    }

    std::string iconPath = FontPath(ICON_FONT_FILE);
    if (FileExists(iconPath.c_str())) {
        ImFontConfig icons;
        icons.MergeMode = true;
        icons.PixelSnapH = true;
        icons.GlyphMinAdvanceX = ICON_SIZE;  // icons line up in columns
        icons.GlyphOffset = ImVec2(0, ICON_OFFSET_Y);
        io.Fonts->AddFontFromFileTTF(iconPath.c_str(), ICON_SIZE, &icons);
    }
    else {
        fprintf(stderr, "Missing icon font %s\n", iconPath.c_str());
    }
    return font;
}

void LoadFonts() {
    ImGui::GetIO().Fonts->Clear();  // rlImGui added its default; ours replace it
    regularFont = AddFont(FONT_FILE, FONT_SIZE);
    boldFont = AddFont(FONT_BOLD_FILE, FONT_SIZE);
    ImGui::GetIO().FontDefault = regularFont;
}

ImU32 Lighten(ImU32 color, int amount) {
    int r = std::min(255, static_cast<int>((color >> IM_COL32_R_SHIFT) & 0xFF) + amount);
    int g = std::min(255, static_cast<int>((color >> IM_COL32_G_SHIFT) & 0xFF) + amount);
    int b = std::min(255, static_cast<int>((color >> IM_COL32_B_SHIFT) & 0xFF) + amount);
    return IM_COL32(r, g, b, (color >> IM_COL32_A_SHIFT) & 0xFF);
}

void SectionTitle(const char* text) {
    ImGui::PushFont(boldFont, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(ACCENT_COLOR));
    ImGui::SeparatorText(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

static void ApplyImGui() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = WINDOW_PADDING;
    style.FramePadding = FRAME_PADDING;
    style.ItemSpacing = ITEM_SPACING;
    style.ItemInnerSpacing = ITEM_INNER_SPACING;
    style.CellPadding = CELL_PADDING;
    style.IndentSpacing = INDENT_SPACING;
    style.ScrollbarSize = SCROLLBAR_SIZE;
    style.GrabMinSize = GRAB_MIN_SIZE;
    style.WindowRounding = WINDOW_ROUNDING;
    style.ChildRounding = CHILD_ROUNDING;
    style.FrameRounding = FRAME_ROUNDING;
    style.PopupRounding = POPUP_ROUNDING;
    style.ScrollbarRounding = SCROLLBAR_ROUNDING;
    style.GrabRounding = GRAB_ROUNDING;
    style.TabRounding = TAB_ROUNDING;
    style.WindowBorderSize = WINDOW_BORDER;
    style.FrameBorderSize = FRAME_BORDER;
    style.PopupBorderSize = POPUP_BORDER;
    style.SeparatorTextBorderSize = SEPARATOR_TEXT_BORDER;
    style.SeparatorTextPadding = SEPARATOR_TEXT_PADDING;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.FontSizeBase = FONT_SIZE;
    style.FontScaleDpi = IsWindowState(FLAG_WINDOW_HIGHDPI) ? 1.0f : GetWindowScaleDPI().y;  // as rlImGui does

    ImVec4* colors = style.Colors;
    auto set = [&](ImGuiCol slot, ImU32 color) { colors[slot] = Vec(color); };
    set(ImGuiCol_Text, TEXT_COLOR);
    set(ImGuiCol_TextDisabled, TEXT_DIM_COLOR);
    set(ImGuiCol_WindowBg, PANEL_COLOR);
    set(ImGuiCol_ChildBg, Alpha(BACKGROUND_COLOR, 0));
    set(ImGuiCol_PopupBg, Alpha(PANEL_COLOR, 250));
    set(ImGuiCol_Border, BORDER_COLOR);
    set(ImGuiCol_BorderShadow, Alpha(BACKGROUND_COLOR, 0));
    set(ImGuiCol_FrameBg, SURFACE_COLOR);
    set(ImGuiCol_FrameBgHovered, SURFACE_HOVER_COLOR);
    set(ImGuiCol_FrameBgActive, SURFACE_ACTIVE_COLOR);
    set(ImGuiCol_TitleBg, BACKGROUND_COLOR);
    set(ImGuiCol_TitleBgActive, SURFACE_COLOR);
    set(ImGuiCol_TitleBgCollapsed, BACKGROUND_COLOR);
    set(ImGuiCol_MenuBarBg, BACKGROUND_COLOR);
    set(ImGuiCol_ScrollbarBg, Alpha(BACKGROUND_COLOR, 0));
    set(ImGuiCol_ScrollbarGrab, SURFACE_ACTIVE_COLOR);
    set(ImGuiCol_ScrollbarGrabHovered, Lighten(SURFACE_ACTIVE_COLOR, 20));
    set(ImGuiCol_ScrollbarGrabActive, ACCENT_DIM_COLOR);
    set(ImGuiCol_CheckMark, ACCENT_COLOR);
    set(ImGuiCol_SliderGrab, ACCENT_DIM_COLOR);
    set(ImGuiCol_SliderGrabActive, ACCENT_COLOR);
    set(ImGuiCol_Button, SURFACE_COLOR);
    set(ImGuiCol_ButtonHovered, SURFACE_HOVER_COLOR);
    set(ImGuiCol_ButtonActive, ACCENT_DEEP_COLOR);
    set(ImGuiCol_Header, Alpha(ACCENT_DEEP_COLOR, 200));
    set(ImGuiCol_HeaderHovered, SURFACE_HOVER_COLOR);
    set(ImGuiCol_HeaderActive, ACCENT_DEEP_COLOR);
    set(ImGuiCol_Separator, BORDER_COLOR);
    set(ImGuiCol_SeparatorHovered, ACCENT_DIM_COLOR);
    set(ImGuiCol_SeparatorActive, ACCENT_COLOR);
    set(ImGuiCol_ResizeGrip, Alpha(ACCENT_COLOR, 40));
    set(ImGuiCol_ResizeGripHovered, Alpha(ACCENT_COLOR, 140));
    set(ImGuiCol_ResizeGripActive, ACCENT_COLOR);
    set(ImGuiCol_InputTextCursor, ACCENT_COLOR);
    set(ImGuiCol_Tab, SURFACE_COLOR);
    set(ImGuiCol_TabHovered, SURFACE_HOVER_COLOR);
    set(ImGuiCol_TabSelected, ACCENT_DEEP_COLOR);
    set(ImGuiCol_TabSelectedOverline, ACCENT_COLOR);
    set(ImGuiCol_TabDimmed, PANEL_COLOR);
    set(ImGuiCol_TabDimmedSelected, SURFACE_COLOR);
    set(ImGuiCol_PlotLines, TEXT_DIM_COLOR);
    set(ImGuiCol_PlotLinesHovered, ACCENT_COLOR);
    set(ImGuiCol_PlotHistogram, ACCENT_DIM_COLOR);
    set(ImGuiCol_PlotHistogramHovered, ACCENT_COLOR);
    set(ImGuiCol_TableHeaderBg, SURFACE_COLOR);
    set(ImGuiCol_TableBorderStrong, BORDER_COLOR);
    set(ImGuiCol_TableBorderLight, Alpha(BORDER_COLOR, 140));
    set(ImGuiCol_TableRowBg, Alpha(BACKGROUND_COLOR, 0));
    set(ImGuiCol_TableRowBgAlt, Alpha(TEXT_COLOR, 8));
    set(ImGuiCol_TextLink, ACCENT_COLOR);
    set(ImGuiCol_TextSelectedBg, Alpha(ACCENT_COLOR, 80));
    set(ImGuiCol_DragDropTarget, ACCENT_COLOR);
    set(ImGuiCol_NavCursor, ACCENT_COLOR);
    set(ImGuiCol_NavWindowingHighlight, Alpha(TEXT_COLOR, 180));
    set(ImGuiCol_NavWindowingDimBg, Alpha(BACKGROUND_COLOR, 120));
    set(ImGuiCol_ModalWindowDimBg, Alpha(BACKGROUND_COLOR, 160));
}

static void ApplyImNodes() {
    ImNodesStyle& style = ImNodes::GetStyle();
    style.GridSpacing = GRID_SPACING;
    style.NodeCornerRounding = NODE_ROUNDING;
    style.NodePadding = NODE_PADDING;
    style.NodeBorderThickness = NODE_BORDER;
    style.LinkThickness = LINK_THICKNESS;
    style.PinCircleRadius = PIN_RADIUS;
    style.PinQuadSideLength = PIN_QUAD_SIZE;
    style.Flags |= ImNodesStyleFlags_GridLines;

    unsigned int* colors = style.Colors;
    colors[ImNodesCol_GridBackground] = CANVAS_COLOR;
    colors[ImNodesCol_GridLine] = GRID_LINE_COLOR;
    colors[ImNodesCol_GridLinePrimary] = GRID_LINE_PRIMARY_COLOR;
    colors[ImNodesCol_NodeBackground] = NODE_COLOR;
    colors[ImNodesCol_NodeBackgroundHovered] = NODE_HOVER_COLOR;
    colors[ImNodesCol_NodeBackgroundSelected] = NODE_SELECTED_COLOR;
    colors[ImNodesCol_NodeOutline] = NODE_OUTLINE_COLOR;
    colors[ImNodesCol_TitleBar] = TITLE_GATE_COLOR;
    colors[ImNodesCol_TitleBarHovered] = Lighten(TITLE_GATE_COLOR, TITLE_HOVER_LIGHTEN);
    colors[ImNodesCol_TitleBarSelected] = Lighten(TITLE_GATE_COLOR, TITLE_SELECTED_LIGHTEN);
    colors[ImNodesCol_Link] = WIRE_COLOR;
    colors[ImNodesCol_LinkHovered] = WIRE_HOVER_COLOR;
    colors[ImNodesCol_LinkSelected] = WIRE_SELECTED_COLOR;
    colors[ImNodesCol_Pin] = PIN_COLOR;
    colors[ImNodesCol_PinHovered] = PIN_HOVER_COLOR;
    colors[ImNodesCol_BoxSelector] = BOX_SELECTOR_COLOR;
    colors[ImNodesCol_BoxSelectorOutline] = BOX_SELECTOR_OUTLINE_COLOR;
    colors[ImNodesCol_MiniMapBackground] = Alpha(PANEL_COLOR, 200);
    colors[ImNodesCol_MiniMapBackgroundHovered] = Alpha(PANEL_COLOR, 240);
    colors[ImNodesCol_MiniMapOutline] = BORDER_COLOR;
    colors[ImNodesCol_MiniMapOutlineHovered] = ACCENT_DIM_COLOR;
    colors[ImNodesCol_MiniMapNodeBackground] = SURFACE_HOVER_COLOR;
    colors[ImNodesCol_MiniMapNodeBackgroundHovered] = SURFACE_ACTIVE_COLOR;
    colors[ImNodesCol_MiniMapNodeBackgroundSelected] = ACCENT_DIM_COLOR;
    colors[ImNodesCol_MiniMapNodeOutline] = BORDER_COLOR;
    colors[ImNodesCol_MiniMapLink] = WIRE_COLOR;
    colors[ImNodesCol_MiniMapLinkSelected] = WIRE_SELECTED_COLOR;
    colors[ImNodesCol_MiniMapCanvas] = Alpha(CANVAS_COLOR, 200);
    colors[ImNodesCol_MiniMapCanvasOutline] = BORDER_COLOR;
}

void Apply() {
    ApplyImGui();
    ApplyImNodes();
}

}  // namespace Kestrel::Theme
