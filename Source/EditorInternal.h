// EditorInternal.h -- what the editor's parts share. Not for use outside the editor; Editor.h is
// its interface.
//
//   Editor.cpp        the app state, actions, menu bar, shortcuts, the frame and the window
//   EditorToolbar.cpp the toolbar under the menu bar: the mode, the component picker, and tools for the mode
//   EditorCanvas.cpp  the node canvas: nodes, pins, wires, zoom, the clipboard, and its menus
//   EditorPanels.cpp  the properties and simulation panels
//   EditorMemory.cpp  the ROM and RAM contents window
//   EditorImport.cpp  the KiCad import and Save as dialogs

#pragma once

#include "Circuit.h"
#include "Simulation.h"
#include "Theme.h"

#include "imgui.h"
#include "imnodes.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace Kestrel {

// Copied nodes, and the wires between them.
struct Clipboard {
    std::vector<Node> nodes;
    std::vector<Wire> wires;
};

// What the palette and library drag onto the canvas.
constexpr const char* NodePayload = "KESTREL_NODE";
struct NodeDrag {
    NodeKind kind;
    char typeName[MaxNameLength];
};

enum class Mode { Edit,
                  Simulate };

struct App {
    Component* current = nullptr;
    Mode mode = Mode::Edit;
    Simulation simulation;

    std::string status;
    double statusUntil = 0;

    ImVec2 canvasCenter{400, 300};
    ImVec2 canvasPosition{0, 0}, canvasSize{0, 0};
    ImVec2 popupScreenPosition{0, 0};
    int spawnCount = 0;
    int hoveredNode = -1;
    int contextNode = -1;  // the node right-clicked
    bool showMiniMap = true;
    bool snapToGrid = true;   // dropped nodes land on the drawn grid
    bool wireLabels = false;  // pins name what they connect to, instead of wires being drawn
    bool showControls = false;  // Help > Controls
    Clipboard clipboard;
    int pasteCount = 0;                     // pastes since the last copy, each further along
    std::vector<uint32_t> selectAfterDraw;  // pasted nodes, selected once imnodes knows them
    char libraryFilter[64] = "";
    bool quitRequested = false;
    char typeBuffer[256] = "";
    bool quit = false;

    char nameBuffer[MaxNameLength] = "";
    const Component* nameBufferOwner = nullptr;
    char labelBuffer[MaxNameLength] = "";
    uint32_t labelBufferNode = 0;
    char descriptionBuffer[1024] = "";
    const Component* descriptionBufferOwner = nullptr;

    // Side panel widths; the splitters between panels and canvas change them.
    float propertiesWidth = Theme::PROPERTIES_WIDTH, simulationWidth = Theme::SIMULATION_WIDTH;
    ImNodesStyle baseNodeStyle;  // imnodes sizes at zoom 1

    // ROM contents window
    bool romEditorOpen = false;
    Component* romComponent = nullptr;
    uint32_t romNode = 0;
    int romView = 0;  // 0 hex, 1 bits
    bool romFollow = true;
    int romLastAddress = -1;
    char romPath[260] = "";

    // File > Import from KiCad
    bool importRequested = false;
    char importSchematic[260] = "";
    char importName[MaxNameLength] = "";
    std::string importClock = "CLK";
    std::set<std::string> importResets;
    std::vector<std::string> importInputs;  // the board's inputs, read from the schematic
    std::string importInputsFrom;           // the schematic they were read from
    std::string importInputsError;
    char importFilter[64] = "";
    std::string importSummary, importError;
    std::vector<std::string> importWarnings;
    Component* importedBoard = nullptr;

    // File > Save as
    bool saveAsRequested = false;
    char saveAsName[MaxNameLength] = "";
};

extern App app;

// Editor.cpp
void SetStatus(const char* format, ...);
void EnsureEditor(Component& component);
void OpenComponent(Component* component);
void NewComponent();
void SaveAllWithStatus();
void EnterSimulation();

// EditorCanvas.cpp
ImU32 LevelColor(uint8_t level);
const char* LevelText(uint8_t level);
void DrawLed(bool on);
// Several bits as hex: "Z" when nothing drives any of them, "?" for a digit with a bit that's
// floating or in conflict.
std::string BusText(const Simulation& simulation, const std::vector<int>& nets);
std::vector<int> PinNets(const Simulation& simulation, uint32_t node, bool output, int first, int count);
// The parts to add; components from the library too unless withComponents is false.
void DrawAddNodeMenu(ImVec2 screenPosition, bool atCenter, bool withComponents = true);
bool CanPlace(const Component& component);
void PlaceNodeAtCenter(NodeKind kind, const std::string& typeName = "");
bool HasSelection();
void DeleteSelection();
void DrawEditMenuItems(bool withPaste);  // Cut, Copy, (Paste,) Duplicate, Delete
void CopySelection();
void CutSelection();
// Pastes at the mouse if it's over the canvas, otherwise a step on from what was copied.
void Paste();
void DuplicateSelection();
void SelectAll();
void ResetZoom(Component& component);
void ZoomBy(Component& component, float factor);  // about the middle of the canvas
void AutoLayout(Component& component);             // in columns by depth from the inputs, on the grid
void DrawCanvas(ImVec2 position, ImVec2 size);

// EditorToolbar.cpp
float ToolbarHeight();
void DrawToolbar(ImVec2 position, float width);

// EditorPanels.cpp
void DrawSplitter(const char* id, float x, float top, float height, float& panelWidth, float direction);
void DrawProperties(ImVec2 position, ImVec2 size);
void DrawSimulationPanel(ImVec2 position, ImVec2 size);

// EditorMemory.cpp
void OpenRomEditor(uint32_t nodeId);
void DrawRomEditor();

// EditorImport.cpp
void DrawImportDialog();
void DrawSaveAsDialog();

}  // namespace Kestrel
