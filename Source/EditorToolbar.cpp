// EditorToolbar.cpp -- the toolbar under the menu bar. On the left, the mode toggle and the open
// component. Then, editing, the file and edit actions; simulating, the clock's controls. On the right,
// a chip naming the mode, and the view.

#include "EditorInternal.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace Kestrel {

float ToolbarHeight() {
    return ImGui::GetFrameHeight() + Theme::TOOLBAR_PADDING.y * 2;
}

static void Tooltip(const char* tip) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", tip);
    }
}

// A square icon button. active shows it lit, for toggles that are on.
// At least square, and wide enough for the icon with no padding to clip it.
static float IconButtonWidth(const char* icon) {
    return std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(icon).x + Theme::ICON_BUTTON_PADDING * 2);
}

static bool IconButton(const char* icon, const char* tip, bool active = false, bool enabled = true) {
    ImGui::PushStyleColor(ImGuiCol_Button, active ? Theme::Vec(Theme::ACCENT_DEEP_COLOR) : ImVec4(0, 0, 0, 0));
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Vec(Theme::ACCENT_COLOR));
    }
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, ImGui::GetStyle().FramePadding.y));  // the label is centered
    bool pressed = ImGui::Button((std::string(icon) + "##" + tip).c_str(), ImVec2(IconButtonWidth(icon), ImGui::GetFrameHeight()));
    ImGui::PopStyleVar();
    ImGui::EndDisabled();
    ImGui::PopStyleColor(active ? 2 : 1);
    Tooltip(tip);
    ImGui::SameLine(0, Theme::TOOLBAR_BUTTON_SPACING);
    return pressed;
}

static void Separator() {
    ImGui::SameLine(0, Theme::TOOLBAR_GROUP_SPACING);
    ImVec2 corner = ImGui::GetCursorScreenPos();
    float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(corner.x, corner.y + 3), ImVec2(corner.x, corner.y + height - 3),
                                        Theme::BORDER_COLOR);
    ImGui::Dummy(ImVec2(1, height));
    ImGui::SameLine(0, Theme::TOOLBAR_GROUP_SPACING);
}

// One button that switches mode: it shows where it goes.
static void DrawModeToggle() {
    bool editing = app.mode == Mode::Edit;
    ImGui::PushStyleColor(ImGuiCol_Button, Theme::Vec(Theme::ACCENT_COLOR));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Vec(Theme::Lighten(Theme::ACCENT_COLOR, 25)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Vec(Theme::ACCENT_DIM_COLOR));
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Vec(Theme::BACKGROUND_COLOR));
    const char* simulate = ICON_FA_PLAY "  Simulate";
    const char* edit = ICON_FA_PEN_TO_SQUARE "  Edit";
    float width = std::max(ImGui::CalcTextSize(simulate).x, ImGui::CalcTextSize(edit).x) + ImGui::GetStyle().FramePadding.x * 2;
    if (ImGui::Button((std::string(editing ? simulate : edit) + "##mode").c_str(), ImVec2(width, 0))) {  // same width either way
        if (editing) {
            EnterSimulation();
        }
        else {
            app.mode = Mode::Edit;
        }
    }
    ImGui::PopStyleColor(4);
    Tooltip(editing ? "Simulate (F5)" : "Back to editing (F5)");
}

static std::string Lower(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

// The open component; its dropdown lists the library, to open another or (dragged) place one.
static void DrawComponentPicker() {
    ImGui::SameLine(0, Theme::TOOLBAR_GROUP_SPACING);
    std::string preview = std::string(ICON_FA_MICROCHIP "  ") + app.current->name + (app.current->dirty ? " *" : "");
    ImGui::SetNextItemWidth(Theme::COMPONENT_PICKER_WIDTH);
    ImGui::SetNextWindowSizeConstraints(ImVec2(Theme::COMPONENT_PICKER_WIDTH, 0), ImVec2(FLT_MAX, 480));
    bool open = ImGui::BeginCombo("##component", preview.c_str(), ImGuiComboFlags_HeightLargest);
    Tooltip("The component you're working on. Open another from the list, or drag one onto the canvas to place it.");
    if (!open) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        app.libraryFilter[0] = 0;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " filter", app.libraryFilter, sizeof app.libraryFilter);

    std::vector<Component*> sorted;
    for (auto& component : library) {
        sorted.push_back(component.get());
    }
    std::sort(sorted.begin(), sorted.end(), [](Component* a, Component* b) { return a->name < b->name; });
    std::string filter = Lower(app.libraryFilter);
    for (Component* component : sorted) {
        if (Lower(component->name).find(filter) == std::string::npos) {
            continue;
        }
        ImGui::PushID(component);
        bool current = component == app.current;
        std::string label = std::string(current ? ICON_FA_PEN_TO_SQUARE : ICON_FA_MICROCHIP) + "  " + component->name +
                            (component->dirty ? " *" : "");
        if (ImGui::Selectable(label.c_str(), current)) {
            OpenComponent(component);
        }
        if (app.mode == Mode::Edit && CanPlace(*component) && ImGui::BeginDragDropSource()) {
            NodeDrag drag{NodeKind::Component, ""};
            snprintf(drag.typeName, sizeof drag.typeName, "%s", component->name.c_str());
            ImGui::SetDragDropPayload(NodePayload, &drag, sizeof drag);
            ImGui::Text(ICON_FA_PLUS " %s", component->name.c_str());
            ImGui::EndDragDropSource();
        }
        if (!component->description.empty()) {
            ImGui::SetItemTooltip("%s", component->description.c_str());
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Selectable(ICON_FA_FILE_CIRCLE_PLUS "  New component") && app.mode == Mode::Edit) {
        NewComponent();
    }
    ImGui::EndCombo();
}

// EDITING or SIMULATING, as a pill.
static float ChipWidth(const char* text) {
    return ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2;
}

static void DrawModeChip(const char* text) {
    bool editing = app.mode == Mode::Edit;
    ImVec2 corner = ImGui::GetCursorScreenPos();
    ImVec2 size(ChipWidth(text), ImGui::GetFrameHeight());
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 color = editing ? Theme::ACCENT_COLOR : Theme::SUCCESS_COLOR;
    drawList->AddRectFilled(corner, ImVec2(corner.x + size.x, corner.y + size.y),
                            editing ? Theme::CHIP_EDIT_COLOR : Theme::CHIP_SIMULATE_COLOR, Theme::CHIP_ROUNDING);
    drawList->AddRect(corner, ImVec2(corner.x + size.x, corner.y + size.y), Theme::Alpha(color, 140), Theme::CHIP_ROUNDING);
    ImVec2 textSize = ImGui::CalcTextSize(text);
    drawList->AddText(ImVec2(corner.x + (size.x - textSize.x) * 0.5f, corner.y + (size.y - textSize.y) * 0.5f), color, text);
    ImGui::Dummy(size);
}

static void DrawEditTools() {
    if (IconButton(ICON_FA_FILE_CIRCLE_PLUS, "New component (Ctrl+N)")) {
        NewComponent();
    }
    if (IconButton(ICON_FA_FLOPPY_DISK, "Save all (Ctrl+S)")) {
        SaveAllWithStatus();
    }
    Separator();
    bool selection = HasSelection();
    if (IconButton(ICON_FA_SCISSORS, "Cut (Ctrl+X)", false, selection)) {
        CutSelection();
    }
    if (IconButton(ICON_FA_COPY, "Copy (Ctrl+C)", false, selection)) {
        CopySelection();
    }
    if (IconButton(ICON_FA_PASTE, "Paste (Ctrl+V)", false, !app.clipboard.nodes.empty())) {
        Paste();
    }
    if (IconButton(ICON_FA_TRASH, "Delete (Del)", false, selection)) {
        DeleteSelection();
    }
    Separator();
    if (IconButton(ICON_FA_SQUARE_PLUS, "Insert a part")) {
        ImGui::OpenPopup("Insert");
    }
    if (ImGui::BeginPopup("Insert")) {
        DrawAddNodeMenu(ImVec2(0, 0), true);
        ImGui::EndPopup();
    }
}

static void DrawSimulationTools() {
    Simulation& simulation = app.simulation;
    if (IconButton(simulation.running ? ICON_FA_PAUSE : ICON_FA_PLAY, simulation.running ? "Pause (Space)" : "Run (Space)",
                   simulation.running)) {
        simulation.running = !simulation.running;
    }
    if (IconButton(ICON_FA_FORWARD_STEP, "Step one clock cycle")) {
        HalfStep(simulation);
        HalfStep(simulation);
    }
    if (IconButton(ICON_FA_ANGLE_RIGHT, "Half step: one clock edge")) {
        HalfStep(simulation);
    }
    if (IconButton(ICON_FA_ROTATE_LEFT, "Reset: power-cycle, so flip-flops and RAM start over")) {
        BuildSimulation(simulation, *app.current);
    }
    Separator();

    ImGui::SetNextItemWidth(Theme::TOOLBAR_SLIDER_WIDTH);
    ImGui::SliderFloat("##hz", &simulation.frequency, 0.1f, 10000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic);
    Tooltip("Clock speed (Ctrl+click to type one)");
    ImGui::SameLine(0, Theme::TOOLBAR_GROUP_SPACING);

    // The clock: an LED, its level, and how many cycles it has run.
    ImVec2 corner = ImGui::GetCursorPos();
    ImGui::SetCursorPosY(corner.y + ImGui::GetStyle().FramePadding.y);
    DrawLed(simulation.clock);
    ImGui::SameLine();
    ImGui::SetCursorPosY(corner.y);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Theme::Vec(simulation.clock ? Theme::TEXT_COLOR : Theme::TEXT_DIM_COLOR), "%s",
                       simulation.clock ? "HIGH" : "LOW ");
    ImGui::SameLine(0, Theme::TOOLBAR_GROUP_SPACING);
    ImGui::TextDisabled("cycle %llu", static_cast<unsigned long long>(simulation.halfCycles / 2));
    Separator();

    if (IconButton(ICON_FA_DICE, "RAM powers up random, as real RAM does: catches reading memory before writing it.\n"
                                 "Off, RAM starts as zeros. Takes effect on Reset.",
                   simulation.randomRam)) {
        simulation.randomRam = !simulation.randomRam;
    }
}

// Zoom and minimap, against the right edge.
static void DrawViewTools() {
    Component& component = *app.current;
    const ImGuiStyle& style = ImGui::GetStyle();
    float side = ImGui::GetFrameHeight();
    float zoomWidth = ImGui::CalcTextSize("300%").x + style.FramePadding.x * 2;
    const char* chip = app.mode == Mode::Edit ? ICON_FA_PEN "  EDITING" : ICON_FA_BOLT "  SIMULATING";
    float width = ChipWidth(chip) + IconButtonWidth(ICON_FA_MAGNIFYING_GLASS_MINUS) + IconButtonWidth(ICON_FA_MAGNIFYING_GLASS_PLUS) +
                  IconButtonWidth(ICON_FA_MAP) + zoomWidth + Theme::TOOLBAR_BUTTON_SPACING * 3 +
                  Theme::TOOLBAR_GROUP_SPACING * 4 + 2;
    float right = ImGui::GetWindowContentRegionMax().x - width;
    ImGui::SameLine(0, 0);
    if (ImGui::GetCursorPosX() < right) {
        ImGui::SetCursorPosX(right);
    }

    DrawModeChip(chip);
    Separator();

    if (IconButton(ICON_FA_MAGNIFYING_GLASS_MINUS, "Zoom out (mouse wheel)")) {
        ZoomBy(component, 1.0f / Theme::ZOOM_STEP);
    }
    char zoom[16];
    snprintf(zoom, sizeof zoom, "%d%%", static_cast<int>(component.zoom * 100 + 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    if (ImGui::Button((std::string(zoom) + "##zoom").c_str(), ImVec2(zoomWidth, side))) {
        ResetZoom(component);
    }
    ImGui::PopStyleColor();
    Tooltip("Reset zoom (Ctrl+0)");
    ImGui::SameLine(0, Theme::TOOLBAR_BUTTON_SPACING);
    if (IconButton(ICON_FA_MAGNIFYING_GLASS_PLUS, "Zoom in (mouse wheel)")) {
        ZoomBy(component, Theme::ZOOM_STEP);
    }
    Separator();
    if (IconButton(ICON_FA_MAP, "Minimap", app.showMiniMap)) {
        app.showMiniMap = !app.showMiniMap;
    }
}

void DrawToolbar(ImVec2 position, float width) {
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(ImVec2(width, ToolbarHeight()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Theme::TOOLBAR_PADDING);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Theme::Vec(Theme::TOOLBAR_COLOR));
    ImGui::Begin("Toolbar", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    DrawModeToggle();
    DrawComponentPicker();
    Separator();
    if (app.mode == Mode::Edit) {
        DrawEditTools();
    }
    else {
        DrawSimulationTools();
    }
    DrawViewTools();

    ImVec2 corner = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(corner.x, corner.y + ToolbarHeight() - 1),
                                        ImVec2(corner.x + width, corner.y + ToolbarHeight() - 1), Theme::BORDER_COLOR);
    ImGui::End();
}

}  // namespace Kestrel
