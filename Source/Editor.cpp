// Editor.cpp -- the editor's state and actions, its menu bar and shortcuts, and the window.

#include "Editor.h"

#include "ComponentFile.h"
#include "EditorInternal.h"

#include "raylib.h"
#include "rlImGui.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace Kestrel {

App app;

void SetStatus(const char* format, ...) {
    char text[256];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    app.status = text;
    app.statusUntil = GetTime() + 4.0;
}

void EnsureEditor(Component& component) {
    if (!component.editor) {
        component.editor = ImNodes::EditorContextCreate();
        for (Node& node : component.nodes) {
            node.placement = Node::Placement::Grid;
        }
    }
}

void OpenComponent(Component* component) {
    if (!component || component == app.current) {
        return;
    }
    app.current = component;
    EnsureEditor(*component);
    app.hoveredNode = -1;
}

void NewComponent() {
    Component* component = CreateComponent(UniqueComponentName("Untitled"));
    OpenComponent(component);
    SetStatus("Created '%s'", component->name.c_str());
}

void SaveAllWithStatus() {
    int failures = SaveAll();
    if (failures) {
        SetStatus("Couldn't save %d component(s) to %s", failures, componentsDirectory.c_str());
    }
    else {
        SetStatus("Saved to %s", componentsDirectory.c_str());
    }
}

void EnterSimulation() {
    PruneWires(*app.current);
    BuildSimulation(app.simulation, *app.current);
    app.simulation.running = false;
    app.mode = Mode::Simulate;
}

static void ToggleMode() {
    if (app.mode == Mode::Edit) {
        EnterSimulation();
    }
    else {
        app.mode = Mode::Edit;
    }
}

static void DrawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }
    bool editing = app.mode == Mode::Edit;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS "  New component", "Ctrl+N", false, editing)) {
            NewComponent();
        }
        if (ImGui::MenuItem(ICON_FA_FILE_IMPORT "  Import from KiCad...", nullptr, false, editing)) {
            app.importRequested = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FILE_PEN "  Save as...", "Ctrl+Shift+S", false, editing)) {
            app.saveAsRequested = true;
        }
        if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save all", "Ctrl+S")) {
            SaveAllWithStatus();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_POWER_OFF "  Quit")) {
            app.quitRequested = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem(ICON_FA_KEYBOARD "  Controls", "F1", &app.showControls);
        ImGui::EndMenu();
    }

    // The latest status message, against the right edge.
    if (!app.status.empty() && GetTime() < app.statusUntil) {
        float width = ImGui::CalcTextSize(app.status.c_str()).x;
        float right = ImGui::GetWindowContentRegionMax().x - width - ImGui::GetStyle().ItemSpacing.x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 24, right));
        ImGui::TextDisabled("%s", app.status.c_str());
    }
    ImGui::EndMainMenuBar();
}

static void DrawQuitDialog() {
    if (app.quitRequested) {
        app.quitRequested = false;
        if (AnyDirty()) {
            ImGui::OpenPopup("Unsaved changes");
        }
        else {
            app.quit = true;
        }
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Some components have unsaved changes.");
        if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save all and quit")) {
            if (SaveAll() == 0) {
                app.quit = true;
            }
            else {
                SetStatus("Couldn't save everything; not quitting");
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Quit without saving")) {
            app.quit = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static void DrawControls() {
    if (!app.showControls) {
        return;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin(ICON_FA_KEYBOARD "  Controls", &app.showControls, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    static const char* const Rows[][2] = {
        {"Add a part", "Right-click the canvas, or drag one in from the palette"},
        {"Wire", "Drag from an output pin to an input pin"},
        {"Bus", "Several wires into one input; pins A0, A1, ... show as one square bus pin"},
        {"Bus bits", "+ on a bus pin shows its bits, to wire them one at a time"},
        {"Pull a wire off", "Ctrl+click its end"},
        {"Node actions", "Right-click a node or a wire"},
        {"Pan", "Middle-drag"},
        {"Zoom", "Mouse wheel; Ctrl+0 resets"},
        {"Snap to grid", "G toggles"},
        {"Auto layout", "L"},
        {"Wires / labels", "W switches"},
        {"Memory contents", "Double-click a ROM or RAM"},
        {"Use as a part", "Input and Output nodes become this component's pins"},
        {"Edit / simulate", "F5"},
        {"Run / pause", "Space, while simulating"},
        {"Cut, copy, paste", "Ctrl+X, Ctrl+C, Ctrl+V"},
        {"Duplicate", "Ctrl+D"},
        {"Select all", "Ctrl+A"},
        {"Delete", "Del"},
        {"New component", "Ctrl+N"},
        {"Save all / Save as", "Ctrl+S, Ctrl+Shift+S"},
    };
    if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("how");
        for (const auto& row : Rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(Theme::Vec(Theme::ACCENT_COLOR), "%s", row[0]);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", row[1]);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

static void HandleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
        ToggleMode();
    }
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (app.mode == Mode::Edit) {
            app.saveAsRequested = true;
        }
    }
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        SaveAllWithStatus();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false) && app.mode == Mode::Edit) {
        NewComponent();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) {
        app.showControls = !app.showControls;
    }
    if (app.mode == Mode::Edit && io.KeyCtrl && !io.KeyShift && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            CopySelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            CutSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            Paste();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            DuplicateSelection();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            SelectAll();
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_0, false)) {
        ResetZoom(*app.current);
    }
    if (!io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            app.wireLabels = !app.wireLabels;
        }
        if (app.mode == Mode::Edit && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            app.snapToGrid = !app.snapToGrid;
        }
        if (app.mode == Mode::Edit && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
            AutoLayout(*app.current);
        }
    }
    if (app.mode == Mode::Simulate && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        app.simulation.running = !app.simulation.running;
    }
}

// Advances the clock in real time while running, without stalling the UI.
static void RunClock(float frameSeconds) {
    Simulation& simulation = app.simulation;
    if (app.mode != Mode::Simulate || !simulation.running) {
        simulation.pendingHalfCycles = 0;
        return;
    }
    simulation.pendingHalfCycles += frameSeconds * simulation.frequency * 2.0;
    double deadline = GetTime() + 0.010;  // spend at most ~10 ms per frame simulating
    while (simulation.pendingHalfCycles >= 1.0 && GetTime() < deadline) {
        HalfStep(simulation);
        simulation.pendingHalfCycles -= 1.0;
    }
    simulation.pendingHalfCycles = std::min(simulation.pendingHalfCycles, 2.0);  // drop what we can't keep up with
}

static void DrawFrame() {
    float width = static_cast<float>(GetScreenWidth());
    float height = static_cast<float>(GetScreenHeight());

    DrawMenuBar();
    DrawToolbar(ImVec2(0, ImGui::GetFrameHeight()), width);
    float top = ImGui::GetFrameHeight() + ToolbarHeight();
    float bodyHeight = height - top;

    // Keep the panels a usable size and leave the canvas at least MinCanvasWidth.
    const float MinPanelWidth = 150, MinCanvasWidth = 200;
    auto clampWidth = [&](float& panelWidth, float others) {
        panelWidth = std::max(MinPanelWidth, std::min(panelWidth, width - others - MinCanvasWidth));
    };

    if (app.mode == Mode::Edit) {
        clampWidth(app.propertiesWidth, 0);
        float propertiesWidth = app.propertiesWidth;
        DrawCanvas(ImVec2(0, top), ImVec2(width - propertiesWidth, bodyHeight));
        DrawProperties(ImVec2(width - propertiesWidth, top), ImVec2(propertiesWidth, bodyHeight));
        DrawSplitter("##properties splitter", width - propertiesWidth, top, bodyHeight, app.propertiesWidth, -1);
    }
    else {
        clampWidth(app.simulationWidth, 0);
        float panelWidth = app.simulationWidth;
        DrawCanvas(ImVec2(0, top), ImVec2(width - panelWidth, bodyHeight));
        DrawSimulationPanel(ImVec2(width - panelWidth, top), ImVec2(panelWidth, bodyHeight));
        DrawSplitter("##simulation splitter", width - panelWidth, top, bodyHeight, app.simulationWidth, -1);
    }
    DrawRomEditor();
    DrawImportDialog();
    DrawSaveAsDialog();

    DrawQuitDialog();
    DrawControls();
    HandleShortcuts();
}

int RunEditor(const char* screenshotPath, bool simulateOnStart) {
    std::vector<std::string> problems = LoadLibrary();
    for (const std::string& problem : problems) {
        fprintf(stderr, "%s\n", problem.c_str());
    }
    if (library.empty()) {
        CreateComponent("Untitled");
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1440, 880, "Kestrel Circuit Workbench");
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    rlImGuiBeginInitImGui();
    Theme::LoadFonts();
    rlImGuiEndInitImGui();
    ImGui::GetIO().IniFilename = nullptr;
    ImNodes::CreateContext();
    Theme::Apply();
    app.baseNodeStyle = ImNodes::GetStyle();
    app.simulation.randomRam = true;

    std::vector<Component*> sorted;
    for (auto& component : library) {
        sorted.push_back(component.get());
    }
    std::sort(sorted.begin(), sorted.end(), [](Component* a, Component* b) { return a->name < b->name; });
    OpenComponent(sorted.front());
    if (!problems.empty()) {
        SetStatus("%s", problems.front().c_str());
    }
    if (simulateOnStart) {
        EnterSimulation();
    }

    int frame = 0;
    while (!app.quit) {
        if (WindowShouldClose()) {
            app.quitRequested = true;
        }
        RunClock(GetFrameTime());

        BeginDrawing();
        ImVec4 background = Theme::Vec(Theme::BACKGROUND_COLOR);
        ClearBackground(ColorFromNormalized(Vector4{background.x, background.y, background.z, 1}));
        rlImGuiBegin();
        DrawFrame();
        rlImGuiEnd();
        EndDrawing();

        if (screenshotPath && ++frame == 10) {
            TakeScreenshot(screenshotPath);
            break;
        }
    }

    for (auto& component : library) {
        if (component->editor) {
            ImNodes::EditorContextFree(component->editor);
        }
    }
    ImNodes::DestroyContext();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}

}  // namespace Kestrel
