// EditorCanvas.cpp -- the node canvas: nodes, pins, wires, zoom, the clipboard, and its menus.

#include "ComponentFile.h"
#include "EditorInternal.h"

#include "imgui_internal.h"
#include "imnodes_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_set>

namespace Kestrel {

// Pin attribute ids for imnodes: unique per pin within a component.
static int PinId(uint32_t node, bool output, int index) {
    return static_cast<int>(node) * (2 * MaxPinsPerSide) + (output ? MaxPinsPerSide : 0) + index;
}

// A bus pin (one pin standing for a group) has the negative of its first bit's id, less one.
static int BusPinId(uint32_t node, bool output, int first) {
    return -1 - PinId(node, output, first);
}

static void DecodePinId(int pinId, uint32_t& node, bool& output, int& index, bool* bus = nullptr) {
    if (bus) {
        *bus = pinId < 0;
    }
    if (pinId < 0) {
        pinId = -1 - pinId;
    }
    node = static_cast<uint32_t>(pinId / (2 * MaxPinsPerSide));
    output = (pinId % (2 * MaxPinsPerSide)) >= MaxPinsPerSide;
    index = pinId % MaxPinsPerSide;
}

// Pins with a one-bit wire on them, by PinId. A bus with any of these shows its bits.
static std::unordered_set<int> singleWirePins;

static void FindSingleWirePins(const Component& component) {
    singleWirePins.clear();
    for (const Wire& wire : component.wires) {
        if (wire.width != 1) {
            continue;
        }
        singleWirePins.insert(PinId(wire.fromNode, true, static_cast<int>(wire.fromPin)));
        singleWirePins.insert(PinId(wire.toNode, false, static_cast<int>(wire.toPin)));
    }
}

// Is this group drawn as one bus pin only? Otherwise its bits are listed under the bus pin.
static bool BusCollapsed(const Component& component, uint32_t node, bool output, const PinGroup& group) {
    if (group.count < 2 || component.expandedBuses.count(BusPinId(node, output, group.first))) {
        return false;
    }
    for (int pin = group.first; pin < group.first + group.count; pin++) {
        if (singleWirePins.count(PinId(node, output, pin))) {
            return false;
        }
    }
    return true;
}

// The imnodes attribute a wire end attaches to: the bus pin for a bus wire on a collapsed group
// or one spanning its whole group, else the first bit's own pin.
static int WireEndAttribute(const Component& component, const Node& node, bool output, int pin, int width) {
    PinGroup group = GroupOfPin(PinGroups(node, output), pin);
    if (group.count > 1 && (BusCollapsed(component, node.id, output, group) ||
                            (width > 1 && pin == group.first && width == group.count))) {
        return BusPinId(node.id, output, group.first);
    }
    return PinId(node.id, output, pin);
}

std::string BusText(const Simulation& simulation, const std::vector<int>& nets) {
    if (std::all_of(nets.begin(), nets.end(), [&](int net) { return simulation.Level(net) == Floating; })) {
        return "Z";
    }
    std::string text;
    for (int digit = (static_cast<int>(nets.size()) + 3) / 4 - 1; digit >= 0; digit--) {
        int value = 0;
        bool known = true;
        for (int bit = 0; bit < 4 && digit * 4 + bit < static_cast<int>(nets.size()); bit++) {
            uint8_t level = simulation.Level(nets[static_cast<size_t>(digit * 4 + bit)]);
            if (level == High) {
                value |= 1 << bit;
            }
            else if (level != Low) {
                known = false;
            }
        }
        text += known ? "0123456789ABCDEF"[value] : '?';
    }
    return text;
}

static uint8_t BusLevel(const Simulation& simulation, const std::vector<int>& nets) {
    bool anyHigh = false, allFloating = true;
    for (int net : nets) {
        uint8_t level = simulation.Level(net);
        if (level == Conflict) {
            return Conflict;
        }
        anyHigh |= level == High;
        allFloating &= level == Floating;
    }
    if (allFloating) {
        return Floating;
    }
    if (anyHigh) {
        return High;
    }
    return Low;
}

std::vector<int> PinNets(const Simulation& simulation, uint32_t node, bool output, int first, int count) {
    std::vector<int> nets;
    for (int pin = first; pin < first + count; pin++) {
        nets.push_back(output ? simulation.NetOfOutput(node, pin) : simulation.NetOfInput(node, pin));
    }
    return nets;
}

// Places a new node from the palette or the right-click menu.
static void PlaceNode(NodeKind kind, const std::string& typeName, ImVec2 screenPosition) {
    uint32_t id = AddNode(*app.current, kind, typeName);
    Node* node = app.current->FindNode(id);
    node->placement = Node::Placement::Screen;
    node->position = screenPosition;
}

void PlaceNodeAtCenter(NodeKind kind, const std::string& typeName) {
    float offset = static_cast<float>(app.spawnCount++ % 8) * 18.0f;
    PlaceNode(kind, typeName, ImVec2(app.canvasCenter.x - 60 + offset, app.canvasCenter.y - 40 + offset));
}

bool CanPlace(const Component& component) {
    return &component != app.current && !Uses(component, app.current->name);
}

// ---------------------------------------------------------------------------
// Colors

ImU32 LevelColor(uint8_t level) {
    switch (level) {
        case High:
            return Theme::LEVEL_HIGH_COLOR;
        case Floating:
            return Theme::LEVEL_FLOATING_COLOR;
        case Conflict:
            return Theme::LEVEL_CONFLICT_COLOR;
        default:
            return Theme::LEVEL_LOW_COLOR;
    }
}

const char* LevelText(uint8_t level) {
    static const char* const Texts[] = {"0", "1", "Z", "X (conflict)"};
    return Texts[level & 3];
}

static ImU32 TitleColor(const Node& node) {
    switch (node.kind) {
        case NodeKind::Input:
            return Theme::TITLE_INPUT_COLOR;
        case NodeKind::Output:
            return Theme::TITLE_OUTPUT_COLOR;
        case NodeKind::Component:
            return FindComponent(node.typeName) ? Theme::TITLE_COMPONENT_COLOR : Theme::TITLE_MISSING_COLOR;
        case NodeKind::Clock:
            return Theme::TITLE_CLOCK_COLOR;
        case NodeKind::FlipFlop:
        case NodeKind::FlipFlopSR:
            return Theme::TITLE_FLIP_FLOP_COLOR;
        default:
            return Theme::TITLE_GATE_COLOR;
    }
}

// ---------------------------------------------------------------------------
// Canvas

void DrawLed(bool on) {
    float radius = ImGui::GetTextLineHeight() * 0.5f;
    ImVec2 corner = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(radius * 2, radius * 2));
    ImVec2 center(corner.x + radius, corner.y + radius);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddCircleFilled(center, radius, on ? Theme::LED_ON_COLOR : Theme::LED_OFF_COLOR);
    if (on) {
        drawList->AddCircle(center, radius + Theme::LED_GLOW_THICKNESS, Theme::LED_GLOW_COLOR, 0, Theme::LED_GLOW_THICKNESS);
    }
}

static void DrawNode(Node& node) {
    Component& component = *app.current;
    Simulation& simulation = app.simulation;
    bool simulating = app.mode == Mode::Simulate;

    // Positions are kept at zoom 1, so they're handed to imnodes scaled every frame.
    float zoom = component.zoom;
    if (node.placement == Node::Placement::Screen) {
        ImNodes::SetNodeScreenSpacePos(static_cast<int>(node.id), node.position);
    }
    else {
        ImNodes::SetNodeGridSpacePos(static_cast<int>(node.id), ImVec2(node.position.x * zoom, node.position.y * zoom));
    }
    node.placement = Node::Placement::None;

    ImU32 title = TitleColor(node);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, Theme::Lighten(title, Theme::TITLE_HOVER_LIGHTEN));
    ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, Theme::Lighten(title, Theme::TITLE_SELECTED_LIGHTEN));

    ImNodes::BeginNode(static_cast<int>(node.id));

    std::string titleText = NodeTitle(node);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(titleText.c_str());
    ImNodes::EndNodeTitleBar();

    int inputs = InputCount(node), outputs = OutputCount(node);
    float width = ImGui::CalcTextSize(titleText.c_str()).x;
    for (int index = 0; index < inputs; index++) {
        width = std::max(width, ImGui::CalcTextSize(PinName(node, false, index).c_str()).x + 30 * zoom);
    }
    for (int index = 0; index < outputs; index++) {
        width = std::max(width, ImGui::CalcTextSize(PinName(node, true, index).c_str()).x + 30 * zoom);
    }
    for (bool output : {false, true}) {
        for (const PinGroup& group : PinGroups(node, output)) {
            if (group.count < 2) {
                continue;
            }
            std::string name = PinName(node, output, group.first) + ".." + PinName(node, output, group.first + group.count - 1);
            if (simulating) {
                name += " = " + std::string(static_cast<size_t>((group.count + 3) / 4), '0');
            }
            width = std::max(width, ImGui::CalcTextSize(name.c_str()).x + 40 * zoom);
        }
    }
    width = std::max(width, 60.0f * zoom);

    if (simulating && node.kind == NodeKind::Input && node.width == 1) {
        uint32_t value = SwitchValue(simulation, node.id);
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::PushStyleColor(ImGuiCol_Button, value ? Theme::LEVEL_HIGH_COLOR : Theme::LEVEL_LOW_COLOR);
        if (ImGui::Button(value ? " 1 " : " 0 ")) {
            SetSwitch(simulation, node.id, !value);
        }
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (simulating && node.kind == NodeKind::Input && node.width > 1) {
        uint32_t value = SwitchValue(simulation, node.id);
        ImGui::PushID(static_cast<int>(node.id));
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("00000000").x + 10 * zoom);
        if (ImGui::InputScalar("##value", ImGuiDataType_U32, &value, nullptr, nullptr, "%X",
                               ImGuiInputTextFlags_CharsHexadecimal)) {
            SetSwitch(simulation, node.id, value);
        }
        ImGui::PopID();
    }
    if (simulating && node.kind == NodeKind::Output && node.width == 1) {
        DrawLed(simulation.Value(simulation.NetOfInput(node.id, 0)) != 0);
    }
    if (simulating && node.kind == NodeKind::Output && node.width > 1) {
        ImGui::Text("%s", BusText(simulation, PinNets(simulation, node.id, false, 0, node.width)).c_str());
    }
    if (simulating && IsMemory(node.kind)) {
        size_t address = 0;
        for (int bit = 0; bit < node.memory->addressBits; bit++) {
            address |= static_cast<size_t>(simulation.Value(simulation.NetOfInput(node.id, bit))) << bit;
        }
        const std::vector<uint32_t>* words = &node.memory->words;
        if (node.kind == NodeKind::Ram) {
            auto found = simulation.topRams.find(node.id);
            words = found == simulation.topRams.end() ? nullptr : &simulation.rams[static_cast<size_t>(found->second)].words;
        }
        if (words) {
            ImGui::TextDisabled("[%zX] = %X", address, static_cast<unsigned>((*words)[address]));
        }
    }

    // One pin. A bus pin is square, and has a +/- to show or hide its bits.
    auto drawPin = [&](bool output, int first, int count, bool bus, bool collapsed) {
        int attribute = bus ? BusPinId(node.id, output, first) : PinId(node.id, output, first);
        std::string name = PinName(node, output, first);
        if (bus) {
            name += ".." + PinName(node, output, first + count - 1);
        }
        if (simulating) {
            std::vector<int> nets = PinNets(simulation, node.id, output, first, count);
            ImU32 color = LevelColor(BusLevel(simulation, nets));
            ImNodes::PushColorStyle(ImNodesCol_Pin, color);
            ImNodes::PushColorStyle(ImNodesCol_PinHovered, color);
            if (bus) {
                name += " = " + BusText(simulation, nets);
            }
        }
        ImNodesPinShape shape = bus ? ImNodesPinShape_QuadFilled : ImNodesPinShape_CircleFilled;
        if (output) {
            ImNodes::BeginOutputAttribute(attribute, shape);
        }
        else {
            ImNodes::BeginInputAttribute(attribute, shape);
        }
        float buttonWidth = 0;
        if (bus) {
            bool forced = !collapsed && !component.expandedBuses.count(attribute);  // single wires on its bits
            ImGui::PushID(attribute);
            ImGui::BeginDisabled(forced);
            if (!output && ImGui::SmallButton(collapsed ? "+" : "-")) {
                if (collapsed) {
                    component.expandedBuses.insert(attribute);
                }
                else {
                    component.expandedBuses.erase(attribute);
                }
            }
            if (!output) {
                ImGui::SameLine();
            }
            if (output) {
                buttonWidth = ImGui::CalcTextSize("+").x + ImGui::GetStyle().FramePadding.x * 2 +
                              ImGui::GetStyle().ItemSpacing.x;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (output) {
            float indent = std::max(0.0f, width - ImGui::CalcTextSize(name.c_str()).x - buttonWidth);
            ImGui::Indent(indent);
            ImGui::TextUnformatted(name.c_str());
            ImGui::Unindent(indent);
            if (bus) {
                ImGui::SameLine();
                ImGui::PushID(attribute);
                ImGui::BeginDisabled(!collapsed && !component.expandedBuses.count(attribute));
                if (ImGui::SmallButton(collapsed ? "+" : "-")) {
                    if (collapsed) {
                        component.expandedBuses.insert(attribute);
                    }
                    else {
                        component.expandedBuses.erase(attribute);
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
        }
        else {
            ImGui::TextUnformatted(name.c_str());
        }
        if (output) {
            ImNodes::EndOutputAttribute();
        }
        else {
            ImNodes::EndInputAttribute();
        }
        if (simulating) {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    };
    for (bool output : {false, true}) {
        for (const PinGroup& group : PinGroups(node, output)) {
            bool collapsed = BusCollapsed(component, node.id, output, group);
            if (group.count > 1) {
                drawPin(output, group.first, group.count, true, collapsed);
            }
            if (collapsed) {
                continue;
            }
            if (group.count > 1) {
                ImGui::Indent(10 * zoom);
            }
            for (int pin = group.first; pin < group.first + group.count; pin++) {
                drawPin(output, pin, 1, false, false);
            }
            if (group.count > 1) {
                ImGui::Unindent(10 * zoom);
            }
        }
    }
    if (inputs == 0 && outputs == 0) {
        ImGui::Dummy(ImVec2(width, 1));
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
}

// Lets a palette or library item be dragged onto the canvas.
static void NodeDragSource(NodeKind kind, const std::string& typeName, const char* label) {
    if (ImGui::BeginDragDropSource()) {
        NodeDrag drag{kind, ""};
        snprintf(drag.typeName, sizeof drag.typeName, "%s", typeName.c_str());
        ImGui::SetDragDropPayload(NodePayload, &drag, sizeof drag);
        ImGui::Text(ICON_FA_PLUS " %s", label);
        ImGui::EndDragDropSource();
    }
}

void DrawAddNodeMenu(ImVec2 screenPosition, bool atCenter, bool withComponents) {
    auto item = [&](NodeKind kind, const std::string& typeName, const char* label, const char* tooltip, bool enabled) {
        if (ImGui::MenuItem(label, nullptr, false, enabled)) {
            if (atCenter) {
                PlaceNodeAtCenter(kind, typeName);
            }
            else {
                PlaceNode(kind, typeName, screenPosition);
            }
        }
        if (enabled) {
            NodeDragSource(kind, typeName, label);
        }
        if (tooltip && *tooltip) {
            ImGui::SetItemTooltip("%s", tooltip);
        }
    };
    auto primitives = [&](std::initializer_list<NodeKind> kinds) {
        for (NodeKind kind : kinds) {
            const PrimitiveInfo& primitive = Primitives[static_cast<int>(kind)];
            item(kind, "", primitive.name, primitive.description, true);
        }
    };

    Theme::SectionTitle("Pins");
    primitives({NodeKind::Input, NodeKind::Output});
    Theme::SectionTitle("Gates");
    primitives({NodeKind::Not, NodeKind::And, NodeKind::Or, NodeKind::Nand, NodeKind::Nor, NodeKind::Xor, NodeKind::Xnor,
                NodeKind::Buffer, NodeKind::TriState});
    Theme::SectionTitle("Memory and timing");
    primitives({NodeKind::FlipFlop, NodeKind::FlipFlopSR, NodeKind::Rom, NodeKind::Ram, NodeKind::Clock, NodeKind::Low,
                NodeKind::High});
    Theme::SectionTitle("Input/output");
    primitives({NodeKind::Terminal});
    if (!withComponents) {
        return;
    }
    Theme::SectionTitle("Components");
    bool any = false;
    for (auto& component : library) {
        if (component.get() == app.current) {
            continue;
        }
        any = true;
        bool allowed = CanPlace(*component);
        std::string tooltip = allowed ? component->description
                                      : "It uses '" + app.current->name + "', so it can't go inside it";
        item(NodeKind::Component, component->name, component->name.c_str(), tooltip.c_str(), allowed);
    }
    if (!any) {
        ImGui::TextDisabled("(none yet)");
    }
}

bool HasSelection() {
    ImNodes::EditorContextSet(app.current->editor);
    return ImNodes::NumSelectedNodes() > 0 || ImNodes::NumSelectedLinks() > 0;
}

static std::vector<uint32_t> SelectedNodes() {
    ImNodes::EditorContextSet(app.current->editor);
    std::vector<int> ids(static_cast<size_t>(ImNodes::NumSelectedNodes()));
    if (!ids.empty()) {
        ImNodes::GetSelectedNodes(ids.data());
    }
    return std::vector<uint32_t>(ids.begin(), ids.end());
}

void DeleteSelection() {
    Component& component = *app.current;
    ImNodes::EditorContextSet(component.editor);
    int linkCount = ImNodes::NumSelectedLinks();
    if (linkCount > 0) {
        std::vector<int> links(static_cast<size_t>(linkCount));
        ImNodes::GetSelectedLinks(links.data());
        for (int link : links) {
            RemoveWire(component, static_cast<uint32_t>(link));
        }
    }
    int nodeCount = ImNodes::NumSelectedNodes();
    if (nodeCount > 0) {
        std::vector<int> nodes(static_cast<size_t>(nodeCount));
        ImNodes::GetSelectedNodes(nodes.data());
        for (int node : nodes) {
            RemoveNode(component, static_cast<uint32_t>(node));
        }
    }
    ImNodes::ClearLinkSelection();
    ImNodes::ClearNodeSelection();
}

void CopySelection() {
    const Component& component = *app.current;
    std::vector<uint32_t> selected = SelectedNodes();
    if (selected.empty()) {
        return;
    }
    Clipboard clipboard;
    std::set<uint32_t> ids(selected.begin(), selected.end());
    for (const Node& node : component.nodes) {
        if (ids.count(node.id)) {
            clipboard.nodes.push_back(node);
            if (node.memory) {
                clipboard.nodes.back().memory = std::make_shared<MemoryData>(*node.memory);  // its own copy
            }
        }
    }
    for (const Wire& wire : component.wires) {
        if (ids.count(wire.fromNode) && ids.count(wire.toNode)) {
            clipboard.wires.push_back(wire);
        }
    }
    app.clipboard = std::move(clipboard);
    app.pasteCount = 0;
    SetStatus("Copied %zu node(s)", app.clipboard.nodes.size());
}

void CutSelection() {
    CopySelection();
    DeleteSelection();
}

// A screen position as a node position: grid space at zoom 1.
static ImVec2 ScreenToNodePosition(const Component& component, ImVec2 screen) {
    ImNodes::EditorContextSet(component.editor);
    ImVec2 panning = ImNodes::EditorContextGetPanning();
    return ImVec2((screen.x - app.canvasPosition.x - panning.x) / component.zoom,
                  (screen.y - app.canvasPosition.y - panning.y) / component.zoom);
}

static void PasteAt(bool atMouse) {
    Component& component = *app.current;
    const Clipboard& clipboard = app.clipboard;
    if (clipboard.nodes.empty()) {
        return;
    }
    ImVec2 corner(1e9f, 1e9f);
    for (const Node& node : clipboard.nodes) {
        corner = ImVec2(std::min(corner.x, node.position.x), std::min(corner.y, node.position.y));
    }
    ImVec2 mouse = ImGui::GetMousePos();
    bool overCanvas = mouse.x >= app.canvasPosition.x && mouse.y >= app.canvasPosition.y &&
                      mouse.x < app.canvasPosition.x + app.canvasSize.x && mouse.y < app.canvasPosition.y + app.canvasSize.y;
    float step = 30.0f * static_cast<float>(++app.pasteCount);
    ImVec2 target = atMouse && overCanvas ? ScreenToNodePosition(component, mouse) : ImVec2(corner.x + step, corner.y + step);

    std::map<uint32_t, uint32_t> newIds;
    int skipped = 0;
    app.selectAfterDraw.clear();
    for (Node node : clipboard.nodes) {
        if (node.kind == NodeKind::Component) {
            const Component* type = FindComponent(node.typeName);
            if (!type || !CanPlace(*type)) {
                skipped++;
                continue;
            }
        }
        uint32_t oldId = node.id;
        node.id = component.nextId++;
        if (node.memory) {
            node.memory = std::make_shared<MemoryData>(*node.memory);
        }
        if (node.kind == NodeKind::Input || node.kind == NodeKind::Output) {
            for (const Node& other : component.nodes) {
                if (other.kind == node.kind && other.label == node.label) {
                    node.label = UniquePinLabel(component, node.kind);  // pin names must stay unique
                    break;
                }
            }
        }
        node.position = ImVec2(node.position.x - corner.x + target.x, node.position.y - corner.y + target.y);
        node.placement = Node::Placement::Grid;
        newIds[oldId] = node.id;
        app.selectAfterDraw.push_back(node.id);
        component.nodes.push_back(node);
    }
    for (const Wire& wire : clipboard.wires) {
        if (newIds.count(wire.fromNode) && newIds.count(wire.toNode)) {
            Connect(component, newIds[wire.fromNode], static_cast<int>(wire.fromPin), newIds[wire.toNode],
                    static_cast<int>(wire.toPin), static_cast<int>(wire.width));
        }
    }
    component.dirty = true;
    if (skipped) {
        SetStatus("Pasted %zu node(s); %d component(s) can't go inside '%s'", newIds.size(), skipped, component.name.c_str());
    }
    else {
        SetStatus("Pasted %zu node(s)", newIds.size());
    }
}

void Paste() {
    PasteAt(true);
}

void DuplicateSelection() {
    Clipboard kept = app.clipboard;
    int keptCount = app.pasteCount;
    CopySelection();
    PasteAt(false);  // just beside the original
    app.clipboard = std::move(kept);
    app.pasteCount = keptCount;
}

void SelectAll() {
    ImNodes::EditorContextSet(app.current->editor);
    for (const Node& node : app.current->nodes) {
        ImNodes::SelectNode(static_cast<int>(node.id));
    }
}

constexpr float MinZoom = 0.25f, MaxZoom = 3.0f;

// imnodes has no zoom of its own: its sizes are scaled here, and node positions in DrawNode.
static void ScaleNodeStyle(float zoom) {
    const ImNodesStyle& base = app.baseNodeStyle;
    ImNodesStyle& style = ImNodes::GetStyle();
    style.GridSpacing = base.GridSpacing * zoom;
    style.NodeCornerRounding = base.NodeCornerRounding * zoom;
    style.NodePadding = ImVec2(base.NodePadding.x * zoom, base.NodePadding.y * zoom);
    style.NodeBorderThickness = base.NodeBorderThickness * zoom;
    style.LinkThickness = base.LinkThickness * zoom;
    style.LinkHoverDistance = base.LinkHoverDistance * zoom;
    style.PinCircleRadius = base.PinCircleRadius * zoom;
    style.PinQuadSideLength = base.PinQuadSideLength * zoom;
    style.PinTriangleSideLength = base.PinTriangleSideLength * zoom;
    style.PinLineThickness = base.PinLineThickness * zoom;
    style.PinHoverRadius = base.PinHoverRadius * zoom;
    style.PinOffset = base.PinOffset * zoom;
}

// Zooms to newZoom about pivot (relative to the canvas's corner): that point stays put on screen.
static void ZoomAbout(Component& component, float newZoom, ImVec2 pivot) {
    newZoom = std::clamp(newZoom, MinZoom, MaxZoom);
    ImNodes::EditorContextSet(component.editor);
    ImVec2 panning = ImNodes::EditorContextGetPanning();
    float ratio = newZoom / component.zoom;
    ImNodes::EditorContextResetPanning(
        ImVec2(pivot.x - (pivot.x - panning.x) * ratio, pivot.y - (pivot.y - panning.y) * ratio));
    component.zoom = newZoom;
}

void ZoomBy(Component& component, float factor) {
    if (component.editor) {
        ZoomAbout(component, component.zoom * factor, ImVec2(app.canvasSize.x * 0.5f, app.canvasSize.y * 0.5f));
    }
}

void ResetZoom(Component& component) {
    if (!component.editor) {
        return;
    }
    ImNodes::EditorContextSet(component.editor);
    ImVec2 panning = ImNodes::EditorContextGetPanning();
    ImNodes::EditorContextResetPanning(ImVec2(panning.x / component.zoom, panning.y / component.zoom));
    component.zoom = 1.0f;
}

static float SnapToGrid(float value) {
    return std::round(value / Theme::GRID_SPACING) * Theme::GRID_SPACING;
}

// A layered layout: each node one column right of the furthest node feeding it, with feedback
// wires (a register's output back to its input) left out so they can't push columns along forever.
// Within a column, nodes are ordered to sit level with what they connect to, which untangles wires.
void AutoLayout(Component& component) {
    if (component.nodes.empty() || !component.editor) {
        return;
    }
    ImNodes::EditorContextSet(component.editor);
    size_t count = component.nodes.size();
    std::map<uint32_t, size_t> index;
    for (size_t i = 0; i < count; i++) {
        index[component.nodes[i].id] = i;
    }
    std::vector<std::vector<size_t>> next(count), previous(count);
    for (const Wire& wire : component.wires) {
        auto from = index.find(wire.fromNode), to = index.find(wire.toNode);
        if (from != index.end() && to != index.end() && from->second != to->second) {
            next[from->second].push_back(to->second);
        }
    }

    // Depth-first from the inputs first: edges back to a node still being visited are feedback.
    // Finishing order, reversed, is an order where every kept wire runs forward.
    std::vector<int> state(count, 0);  // 0 unseen, 1 visiting, 2 done
    std::vector<size_t> finished;
    std::vector<std::vector<size_t>> forward(count);
    auto visit = [&](size_t start) {
        std::vector<std::pair<size_t, size_t>> stack{{start, 0}};
        state[start] = 1;
        while (!stack.empty()) {
            auto& [node, edge] = stack.back();
            if (edge < next[node].size()) {
                size_t target = next[node][edge++];
                if (state[target] == 0) {
                    forward[node].push_back(target);
                    state[target] = 1;
                    stack.push_back({target, 0});
                }
                else if (state[target] == 2) {
                    forward[node].push_back(target);
                }
            }
            else {
                state[node] = 2;
                finished.push_back(node);
                stack.pop_back();
            }
        }
    };
    for (size_t i = 0; i < count; i++) {
        if (component.nodes[i].kind == NodeKind::Input && state[i] == 0) {
            visit(i);
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (state[i] == 0) {
            visit(i);
        }
    }

    std::vector<int> layer(count, 0);
    for (auto it = finished.rbegin(); it != finished.rend(); ++it) {
        for (size_t target : forward[*it]) {
            layer[target] = std::max(layer[target], layer[*it] + 1);
            previous[target].push_back(*it);
        }
    }
    int lastLayer = *std::max_element(layer.begin(), layer.end());
    for (size_t i = 0; i < count; i++) {
        if (component.nodes[i].kind == NodeKind::Input) {
            layer[i] = 0;
        }
        else if (component.nodes[i].kind == NodeKind::Output) {
            layer[i] = std::max(lastLayer, 1);  // outputs line up on the right
        }
    }
    lastLayer = *std::max_element(layer.begin(), layer.end());

    // Columns, first in their current order top to bottom, then sorted by where their neighbours are.
    std::vector<std::vector<size_t>> columns(static_cast<size_t>(lastLayer) + 1);
    for (size_t i = 0; i < count; i++) {
        columns[static_cast<size_t>(layer[i])].push_back(i);
    }
    std::vector<float> rank(count);
    for (auto& column : columns) {
        std::sort(column.begin(), column.end(),
                  [&](size_t a, size_t b) { return component.nodes[a].position.y < component.nodes[b].position.y; });
        for (size_t row = 0; row < column.size(); row++) {
            rank[column[row]] = static_cast<float>(row);
        }
    }
    auto sortColumn = [&](std::vector<size_t>& column, bool byPrevious) {
        std::vector<float> key(count);
        for (size_t node : column) {
            const auto& neighbours = byPrevious ? previous[node] : forward[node];
            float sum = 0;
            for (size_t neighbour : neighbours) {
                sum += rank[neighbour];
            }
            key[node] = neighbours.empty() ? rank[node] : sum / static_cast<float>(neighbours.size());
        }
        std::stable_sort(column.begin(), column.end(), [&](size_t a, size_t b) { return key[a] < key[b]; });
        for (size_t row = 0; row < column.size(); row++) {
            rank[column[row]] = static_cast<float>(row);
        }
    };
    for (int sweep = 0; sweep < 4; sweep++) {
        for (size_t c = 1; c < columns.size(); c++) {
            sortColumn(columns[c], true);
        }
        for (size_t c = columns.size() - 1; c-- > 0;) {
            sortColumn(columns[c], false);
        }
    }

    // Place the columns, each centred on the tallest, at the top left of where the nodes were.
    float zoom = component.zoom;
    ImVec2 origin(FLT_MAX, FLT_MAX);
    std::vector<ImVec2> size(count);
    for (size_t i = 0; i < count; i++) {
        ImVec2 dimensions = ImNodes::GetNodeDimensions(static_cast<int>(component.nodes[i].id));
        size[i] = ImVec2(dimensions.x / zoom, dimensions.y / zoom);
        origin.x = std::min(origin.x, component.nodes[i].position.x);
        origin.y = std::min(origin.y, component.nodes[i].position.y);
    }
    const float columnGap = Theme::GRID_SPACING * 4, rowGap = Theme::GRID_SPACING;
    std::vector<float> heights;
    float tallest = 0;
    for (const auto& column : columns) {
        float height = 0;
        for (size_t node : column) {
            height += size[node].y + rowGap;
        }
        heights.push_back(height);
        tallest = std::max(tallest, height);
    }
    float x = SnapToGrid(origin.x);
    for (size_t c = 0; c < columns.size(); c++) {
        float width = 0;
        float y = SnapToGrid(origin.y + (tallest - heights[c]) * 0.5f);
        for (size_t node : columns[c]) {
            Node& placed = component.nodes[node];
            placed.position = ImVec2(x, y);
            placed.placement = Node::Placement::Grid;
            y = SnapToGrid(y + size[node].y + rowGap + Theme::GRID_SPACING * 0.49f);
            width = std::max(width, size[node].x);
        }
        if (!columns[c].empty()) {
            x = SnapToGrid(x + width + columnGap + Theme::GRID_SPACING * 0.49f);
        }
    }
    component.dirty = true;
    SetStatus("Laid out %zu nodes", count);
}

// Labels in place of wires: each pin names what it's wired to, in a tag beside it.
static void DrawWireLabels(const Component& component, ImVec2 position, ImVec2 size) {
    ImNodesEditorContext& editor = ImNodes::EditorContextGet();
    struct Tag {
        std::vector<std::string> names;
        ImU32 color;
    };
    std::map<std::pair<int, bool>, Tag> tags;  // attribute, output
    bool simulating = app.mode == Mode::Simulate;
    for (const Wire& wire : component.wires) {
        const Node* from = component.FindNode(wire.fromNode);
        const Node* to = component.FindNode(wire.toNode);
        if (!from || !to) {
            continue;
        }
        int width = static_cast<int>(wire.width);
        ImU32 color = Theme::TEXT_COLOR;
        if (simulating) {
            color = LevelColor(BusLevel(app.simulation, PinNets(app.simulation, wire.fromNode, true,
                                                                static_cast<int>(wire.fromPin), width)));
        }
        Tag& out = tags[{WireEndAttribute(component, *from, true, static_cast<int>(wire.fromPin), width), true}];
        out.names.push_back(EndpointText(*to, false, static_cast<int>(wire.toPin), width));
        out.color = color;
        Tag& in = tags[{WireEndAttribute(component, *to, false, static_cast<int>(wire.toPin), width), false}];
        in.names.push_back(EndpointText(*from, true, static_cast<int>(wire.fromPin), width));
        in.color = color;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(position, ImVec2(position.x + size.x, position.y + size.y), true);
    ImFont* font = ImGui::GetFont();
    // Tags grow more slowly than the canvas when zooming in, so they don't swamp the nodes.
    float tagZoom = std::min(std::sqrt(component.zoom), component.zoom);
    float fontSize = ImGui::GetFontSize() / component.zoom * tagZoom * Theme::WIRE_LABEL_SCALE;
    float padding = 4 * tagZoom, gap = 10 * component.zoom;
    for (const auto& [key, tag] : tags) {
        int pin = ImNodes::ObjectPoolFind(editor.Pins, key.first);
        if (pin < 0) {
            continue;
        }
        std::string text;
        for (size_t i = 0; i < tag.names.size() && i < 3; i++) {
            text += (i ? ", " : "") + tag.names[i];
        }
        if (tag.names.size() > 3) {
            text += " +" + std::to_string(tag.names.size() - 3);
        }
        text = key.second ? ICON_FA_ARROW_RIGHT " " + text : text + " " ICON_FA_ARROW_RIGHT;
        ImVec2 anchor = editor.Pins.Pool[pin].Pos;
        ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0, text.c_str());
        float left = key.second ? anchor.x + gap : anchor.x - gap - textSize.x - padding * 2;
        ImVec2 corner(left, anchor.y - textSize.y * 0.5f - padding * 0.5f);
        ImVec2 end(left + textSize.x + padding * 2, anchor.y + textSize.y * 0.5f + padding * 0.5f);
        drawList->AddRectFilled(corner, end, Theme::WIRE_LABEL_BACKGROUND_COLOR, Theme::CHIP_ROUNDING * tagZoom);
        drawList->AddRect(corner, end, Theme::Alpha(tag.color, 120), Theme::CHIP_ROUNDING * tagZoom);
        drawList->AddText(font, fontSize, ImVec2(left + padding, anchor.y - textSize.y * 0.5f), tag.color, text.c_str());
    }
    drawList->PopClipRect();
}

// The Edit actions as menu items; the canvas's right-click menus and the Edit menu share them.
void DrawEditMenuItems(bool withPaste) {
    bool selection = HasSelection();
    if (ImGui::MenuItem(ICON_FA_SCISSORS "  Cut", "Ctrl+X", false, selection)) {
        CutSelection();
    }
    if (ImGui::MenuItem(ICON_FA_COPY "  Copy", "Ctrl+C", false, selection)) {
        CopySelection();
    }
    if (withPaste && ImGui::MenuItem(ICON_FA_PASTE "  Paste", "Ctrl+V", false, !app.clipboard.nodes.empty())) {
        Paste();
    }
    if (ImGui::MenuItem(ICON_FA_CLONE "  Duplicate", "Ctrl+D", false, selection)) {
        DuplicateSelection();
    }
    if (ImGui::MenuItem(ICON_FA_TRASH "  Delete", "Del", false, selection)) {
        DeleteSelection();
    }
}

// Right-click: a node (and whatever else is selected with it), a wire, or the empty canvas.
static void DrawContextMenus(int hoveredLink) {
    Component& component = *app.current;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImNodes::IsEditorHovered() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
        app.popupScreenPosition = ImGui::GetMousePos();
        if (app.hoveredNode >= 0) {
            if (!ImNodes::IsNodeSelected(app.hoveredNode)) {
                ImNodes::ClearNodeSelection();
                ImNodes::ClearLinkSelection();
                ImNodes::SelectNode(app.hoveredNode);
            }
            app.contextNode = app.hoveredNode;
            ImGui::OpenPopup("Node");
        }
        else if (hoveredLink >= 0) {
            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
            ImNodes::SelectLink(hoveredLink);
            ImGui::OpenPopup("Wire");
        }
        else {
            ImGui::OpenPopup("Canvas");
        }
    }

    if (ImGui::BeginPopup("Node")) {
        int count = ImNodes::NumSelectedNodes();
        const Node* node = component.FindNode(static_cast<uint32_t>(app.contextNode));
        if (count > 1) {
            ImGui::TextDisabled("%d nodes", count);
        }
        else if (node) {
            ImGui::TextDisabled("%s", NodeTitle(*node).c_str());
        }
        ImGui::Separator();
        if (count == 1 && node) {
            if (IsMemory(node->kind) && ImGui::MenuItem(ICON_FA_TABLE_CELLS "  Contents...")) {
                OpenRomEditor(node->id);
            }
            Component* type = node->kind == NodeKind::Component ? FindComponent(node->typeName) : nullptr;
            if (type && ImGui::MenuItem(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE "  Open component")) {
                OpenComponent(type);
            }
            if (IsMemory(node->kind) || type) {
                ImGui::Separator();
            }
        }
        DrawEditMenuItems(false);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Wire")) {
        if (ImGui::MenuItem(ICON_FA_TRASH "  Delete wire", "Del")) {
            DeleteSelection();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Canvas")) {
        if (ImGui::MenuItem(ICON_FA_PASTE "  Paste here", "Ctrl+V", false, !app.clipboard.nodes.empty())) {
            Paste();
        }
        if (ImGui::MenuItem(ICON_FA_OBJECT_GROUP "  Select all", "Ctrl+A", false, !component.nodes.empty())) {
            SelectAll();
        }
        DrawAddNodeMenu(app.popupScreenPosition, false);
        ImGui::EndPopup();
    }
}

void DrawCanvas(ImVec2 position, ImVec2 size) {
    Component& component = *app.current;
    Simulation& simulation = app.simulation;
    bool editing = app.mode == Mode::Edit;

    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Canvas", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    app.canvasCenter = ImVec2(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
    app.canvasPosition = position;
    app.canvasSize = size;

    EnsureEditor(component);
    ImNodes::EditorContextSet(component.editor);
    ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = editing ? &ImGui::GetIO().KeyCtrl : nullptr;

    if (editing) {
        PruneWires(component);
    }

    // Mouse wheel zooms about the point under the cursor: that point stays put on screen.
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseWheel != 0 && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        ZoomAbout(component, component.zoom * std::pow(1.15f, io.MouseWheel),
                  ImVec2(io.MousePos.x - position.x, io.MousePos.y - position.y));
    }
    float zoom = component.zoom;
    ScaleNodeStyle(zoom);
    // Dragged nodes jump from grid line to grid line; the spacing is the zoomed one, so it matches the drawn grid.
    if (app.snapToGrid && editing) {
        ImNodes::GetStyle().Flags |= ImNodesStyleFlags_GridSnapping;
    }
    else {
        ImNodes::GetStyle().Flags &= ~ImNodesStyleFlags_GridSnapping;
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * zoom);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Theme::CANVAS_ITEM_SPACING.x * zoom, Theme::CANVAS_ITEM_SPACING.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(Theme::CANVAS_FRAME_PADDING.x * zoom, Theme::CANVAS_FRAME_PADDING.y * zoom));

    ImNodes::BeginNodeEditor();
    FindSingleWirePins(component);
    for (Node& node : component.nodes) {
        DrawNode(node);
    }

    for (const Wire& wire : component.wires) {
        const Node* from = component.FindNode(wire.fromNode);
        const Node* to = component.FindNode(wire.toNode);
        if (!from || !to || app.wireLabels) {
            continue;
        }
        int width = static_cast<int>(wire.width);
        if (!editing) {
            ImU32 color = LevelColor(BusLevel(simulation, PinNets(simulation, wire.fromNode, true,
                                                                  static_cast<int>(wire.fromPin), width)));
            ImNodes::PushColorStyle(ImNodesCol_Link, color);
            ImNodes::PushColorStyle(ImNodesCol_LinkHovered, Theme::Lighten(color, Theme::WIRE_HOVER_LIGHTEN));
            ImNodes::PushColorStyle(ImNodesCol_LinkSelected, Theme::Lighten(color, Theme::WIRE_HOVER_LIGHTEN));
        }
        if (width > 1) {
            ImNodes::PushStyleVar(ImNodesStyleVar_LinkThickness, ImNodes::GetStyle().LinkThickness * Theme::BUS_THICKNESS_SCALE);
        }
        ImNodes::Link(static_cast<int>(wire.id), WireEndAttribute(component, *from, true, static_cast<int>(wire.fromPin), width),
                      WireEndAttribute(component, *to, false, static_cast<int>(wire.toPin), width));
        if (width > 1) {
            ImNodes::PopStyleVar();
        }
        if (!editing) {
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
            ImNodes::PopColorStyle();
        }
    }

    if (app.showMiniMap) {
        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    }
    ImNodes::EndNodeEditor();
    if (app.wireLabels) {
        DrawWireLabels(component, position, size);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ScaleNodeStyle(1.0f);

    if (!app.selectAfterDraw.empty()) {  // imnodes knows the pasted nodes now
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        for (uint32_t id : app.selectAfterDraw) {
            ImNodes::SelectNode(static_cast<int>(id));
        }
        app.selectAfterDraw.clear();
    }

    // Positions: remember where nodes were dragged.
    for (Node& node : component.nodes) {
        ImVec2 now = ImNodes::GetNodeGridSpacePos(static_cast<int>(node.id));
        now = ImVec2(now.x / zoom, now.y / zoom);
        if (std::fabs(now.x - node.position.x) > 0.5f || std::fabs(now.y - node.position.y) > 0.5f) {
            node.position = now;
            component.dirty = true;
        }
        // Once let go, a dragged (so selected) node settles onto the grid lines. Others stay put, so
        // opening a component doesn't change it.
        if (app.snapToGrid && editing && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
            ImNodes::IsNodeSelected(static_cast<int>(node.id))) {
            ImVec2 snapped(SnapToGrid(node.position.x), SnapToGrid(node.position.y));
            if (snapped.x != node.position.x || snapped.y != node.position.y) {
                node.position = snapped;
                node.placement = Node::Placement::Grid;
                component.dirty = true;
            }
        }
    }

    int hovered = -1;
    app.hoveredNode = ImNodes::IsNodeHovered(&hovered) ? hovered : -1;
    int hoveredLink = -1;
    bool linkHovered = ImNodes::IsLinkHovered(&hoveredLink);
    if (linkHovered) {
        for (const Wire& wire : component.wires) {
            const Node* from = component.FindNode(wire.fromNode);
            const Node* to = component.FindNode(wire.toNode);
            if (wire.id != static_cast<uint32_t>(hoveredLink) || !from || !to) {
                continue;
            }
            int width = static_cast<int>(wire.width);
            std::string text = EndpointText(*from, true, static_cast<int>(wire.fromPin), width) + " -> " +
                               EndpointText(*to, false, static_cast<int>(wire.toPin), width);
            if (width > 1) {
                text += "  (" + std::to_string(width) + " bits)";
            }
            if (!editing) {
                text += " = " + BusText(simulation, PinNets(simulation, wire.fromNode, true, static_cast<int>(wire.fromPin), width));
            }
            ImGui::SetTooltip("%s", text.c_str());
        }
    }
    if (app.hoveredNode >= 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        OpenRomEditor(static_cast<uint32_t>(app.hoveredNode));  // does nothing unless it's a ROM
    }

    if (editing) {
        int startPin, endPin;
        if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
            uint32_t fromNode, toNode;
            bool fromOutput, toOutput, fromBus, toBus;
            int fromIndex, toIndex;
            DecodePinId(startPin, fromNode, fromOutput, fromIndex, &fromBus);
            DecodePinId(endPin, toNode, toOutput, toIndex, &toBus);
            const Node* from = component.FindNode(fromNode);
            const Node* to = component.FindNode(toNode);
            if (fromOutput && !toOutput && from && to) {
                // A bus pin carries its whole group. A bit pin wired to a bus pin starts a bus of
                // the same width there.
                int fromWidth = fromBus ? GroupOfPin(PinGroups(*from, true), fromIndex).count : 1;
                int toWidth = toBus ? GroupOfPin(PinGroups(*to, false), toIndex).count : 1;
                int width = std::max(fromWidth, toWidth);
                bool fits = fromIndex + width <= OutputCount(*from) && toIndex + width <= InputCount(*to);
                if ((fromWidth == toWidth || fromWidth == 1 || toWidth == 1) && fits) {
                    Connect(component, fromNode, fromIndex, toNode, toIndex, width);
                }
                else {
                    SetStatus("Can't wire %d bits to %d: show the bus's bits (+) to wire some of them", fromWidth, toWidth);
                }
            }
        }
        int destroyedLink;
        if (ImNodes::IsLinkDestroyed(&destroyedLink)) {
            RemoveWire(component, static_cast<uint32_t>(destroyedLink));
        }

        bool canvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (canvasFocused && !ImGui::GetIO().WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
            DeleteSelection();
        }

        // Parts dragged in from the palette or the library.
        if (ImGui::BeginDragDropTargetCustom(ImRect(position, ImVec2(position.x + size.x, position.y + size.y)),
                                             ImGui::GetID("canvas drop"))) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(NodePayload)) {
                const NodeDrag& drag = *static_cast<const NodeDrag*>(payload->Data);
                PlaceNode(drag.kind, drag.typeName, ImGui::GetMousePos());
            }
            ImGui::EndDragDropTarget();
        }

        DrawContextMenus(linkHovered ? hoveredLink : -1);
    }
    else if (app.hoveredNode >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        app.contextNode = app.hoveredNode;
        ImGui::OpenPopup("Node");
    }
    if (!editing && ImGui::BeginPopup("Node")) {
        const Node* node = component.FindNode(static_cast<uint32_t>(app.contextNode));
        ImGui::TextDisabled("%s", node ? NodeTitle(*node).c_str() : "");
        if (node && IsMemory(node->kind) && ImGui::MenuItem(ICON_FA_TABLE_CELLS "  Show contents")) {
            OpenRomEditor(node->id);
        }
        ImGui::EndPopup();
    }

    ImGui::End();
}

}  // namespace Kestrel
