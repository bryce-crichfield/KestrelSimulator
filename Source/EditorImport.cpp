// EditorImport.cpp -- File > Import from KiCad, and File > Save as.

#include "ComponentFile.h"
#include "EditorInternal.h"
#include "FileDialog.h"
#include "KicadImport.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Kestrel {

static const char* const BrowseLabel = ICON_FA_FOLDER_OPEN "  Browse...";

static std::string SchematicPath() {
    std::string schematic = Trim(app.importSchematic);
    if (schematic.size() > 1 && schematic.front() == '"' && schematic.back() == '"') {
        schematic = schematic.substr(1, schematic.size() - 2);  // pasted from "Copy as path"
    }
    return schematic;
}

// Case-insensitive.
static bool Contains(const std::string& text, const std::string& part) {
    auto lower = [](std::string value) {
        for (char& character : value) {
            character = static_cast<char>(tolower(static_cast<unsigned char>(character)));
        }
        return value;
    };
    return lower(text).find(lower(part)) != std::string::npos;
}

// Reads the board's inputs from the schematic, and picks a likely clock and resets among them.
static void ReadInputs() {
    std::string schematic = SchematicPath();
    app.importInputsFrom = schematic;
    app.importInputsError = ReadKicadInputs(schematic, app.importInputs);
    const std::vector<std::string>& inputs = app.importInputs;
    auto has = [&](const std::string& net) { return std::find(inputs.begin(), inputs.end(), net) != inputs.end(); };
    if (!has(app.importClock)) {
        app.importClock.clear();
        for (const std::string& input : inputs) {
            if (input == "CLK" || (app.importClock.empty() && Contains(input, "CLK"))) {
                app.importClock = input;
            }
        }
    }
    std::set<std::string> resets;
    for (const std::string& reset : app.importResets) {
        if (has(reset)) {
            resets.insert(reset);
        }
    }
    if (resets.empty()) {
        for (const std::string& input : inputs) {
            if (Contains(input, "RESET") || Contains(input, "RST")) {
                resets.insert(input);
            }
        }
    }
    app.importResets = resets;
}

// The search box at the top of a net dropdown.
static void NetFilter() {
    if (ImGui::IsWindowAppearing()) {
        app.importFilter[0] = 0;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "search nets", app.importFilter, sizeof app.importFilter);
}

static void DrawNetPickers() {
    if (app.importInputsFrom.empty()) {
        ImGui::TextDisabled("Choose a schematic to list its nets.");
        return;
    }
    if (!app.importInputsError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Vec(Theme::ERROR_COLOR));
        ImGui::TextWrapped("%s", app.importInputsError.c_str());
        ImGui::PopStyleColor();
        return;
    }
    if (app.importInputs.empty()) {
        ImGui::TextDisabled("Nothing on this board is an input: there is nothing to clock or reset.");
        return;
    }

    if (ImGui::BeginCombo("Clock net", app.importClock.empty() ? "(none)" : app.importClock.c_str(), ImGuiComboFlags_HeightLarge)) {
        NetFilter();
        if (ImGui::Selectable("(none)", app.importClock.empty())) {
            app.importClock.clear();
        }
        for (const std::string& input : app.importInputs) {
            if (Contains(input, app.importFilter) && ImGui::Selectable(input.c_str(), input == app.importClock)) {
                app.importClock = input;
                app.importResets.erase(input);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The board input the bench drives with its clock.");

    std::string preview;
    for (const std::string& reset : app.importResets) {
        preview += (preview.empty() ? "" : ", ") + reset;
    }
    if (ImGui::BeginCombo("Reset nets", preview.empty() ? "(none)" : preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        NetFilter();
        for (const std::string& input : app.importInputs) {
            if (!Contains(input, app.importFilter) || input == app.importClock) {
                continue;
            }
            bool on = app.importResets.count(input) > 0;
            if (ImGui::Checkbox(input.c_str(), &on)) {
                if (on) {
                    app.importResets.insert(input);
                }
                else {
                    app.importResets.erase(input);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Held in reset for the first two clocks; a name starting with / is active low.");
    ImGui::TextDisabled("%zu board inputs; the bench holds the others at 0.", app.importInputs.size());
}

static void RunImport() {
    KicadImportOptions options;
    options.schematic = SchematicPath();
    options.name = Trim(app.importName);
    options.clock = app.importClock;
    options.resets.assign(app.importResets.begin(), app.importResets.end());

    // The ROM window may be showing a node of the board this replaces.
    app.romEditorOpen = false;
    KicadImportResult result = ImportKicad(options);
    app.importError = result.error;
    app.importSummary = result.summary;
    app.importWarnings = result.warnings;
    app.importedBoard = result.board;
    if (result.board) {
        app.current = nullptr;  // the board may be the one open; reopen it fresh
        OpenComponent(result.board);
        for (Node& node : result.board->nodes) {
            node.placement = Node::Placement::Grid;
        }
        SetStatus("Imported '%s' (not saved yet)", result.board->name.c_str());
    }
}

void DrawImportDialog() {
    if (app.importRequested) {
        app.importRequested = false;
        app.importError.clear();
        app.importSummary.clear();
        app.importWarnings.clear();
        app.importedBoard = nullptr;
        ImGui::OpenPopup("Import from KiCad");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(640, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Import from KiCad", nullptr)) {
        return;
    }
    ImGui::TextWrapped("Makes a component from a KiCad schematic of 74-series logic, and a bench that clocks and resets it.");

    // The schematic: typed or pasted, or chosen with Browse. Its nets are read once it's chosen.
    const ImGuiStyle& style = ImGui::GetStyle();
    float browseWidth = ImGui::CalcTextSize(BrowseLabel).x + style.FramePadding.x * 2;
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browseWidth - style.ItemInnerSpacing.x);
    ImGui::InputTextWithHint("##schematic", "the top-level .kicad_sch (or a .net)", app.importSchematic, sizeof app.importSchematic);
    bool chosen = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    if (ImGui::Button(BrowseLabel)) {
        std::string path = OpenFileDialog("Import a KiCad schematic",
                                          "KiCad schematic (*.kicad_sch, *.net)|*.kicad_sch;*.net|All files|*.*", SchematicPath());
        if (!path.empty()) {
            snprintf(app.importSchematic, sizeof app.importSchematic, "%s", path.c_str());
            chosen = true;
        }
    }
    ImGui::SameLine(0, style.ItemInnerSpacing.x);
    ImGui::TextUnformatted("Schematic");
    if (chosen && !SchematicPath().empty() && SchematicPath() != app.importInputsFrom) {
        ReadInputs();
    }

    ImGui::InputTextWithHint("Name", "the schematic's file name", app.importName, sizeof app.importName);
    DrawNetPickers();

    ImGui::BeginDisabled(SchematicPath().empty());
    if (ImGui::Button(ICON_FA_FILE_IMPORT "  Import")) {
        RunImport();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "  Close")) {
        ImGui::CloseCurrentPopup();
    }

    if (!app.importError.empty()) {
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Vec(Theme::ERROR_COLOR));
        ImGui::TextWrapped("%s", app.importError.c_str());
        ImGui::PopStyleColor();
    }
    if (app.importedBoard) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", app.importSummary.c_str());
        ImGui::TextDisabled("Not saved yet: File > Save as... (or Save all) keeps it.");

        // ROMs and RAMs: where their contents come from, and a way to change it.
        bool anyMemory = false;
        for (const Node& node : app.importedBoard->nodes) {
            if (!IsMemory(node.kind)) {
                continue;
            }
            if (!anyMemory) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Memories (load a microcode or program image into each):");
                anyMemory = true;
            }
            ImGui::PushID(static_cast<int>(node.id));
            if (ImGui::SmallButton(ICON_FA_TABLE_CELLS " Contents...")) {
                OpenRomEditor(node.id);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            ImGui::Text("%s  %s", NodeTitle(node).c_str(),
                        node.memory->image.empty() ? "(no image)" : node.memory->image.c_str());
            ImGui::PopID();
        }

        if (!app.importWarnings.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(Theme::Vec(Theme::WARNING_COLOR), ICON_FA_TRIANGLE_EXCLAMATION " %zu warning(s):", app.importWarnings.size());
            ImGui::BeginChild("##warnings", ImVec2(0, 220), ImGuiChildFlags_Borders);
            for (const std::string& warning : app.importWarnings) {
                ImGui::TextWrapped("%s", warning.c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndPopup();
}

void DrawSaveAsDialog() {
    if (app.saveAsRequested) {
        app.saveAsRequested = false;
        snprintf(app.saveAsName, sizeof app.saveAsName, "%s", app.current->name.c_str());
        ImGui::OpenPopup("Save as");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Save as", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    bool enter = ImGui::InputText("Name", app.saveAsName, sizeof app.saveAsName, ImGuiInputTextFlags_EnterReturnsTrue);
    std::string name = app.saveAsName;
    const Component* other = FindComponent(name);
    const char* problem = !IsValidComponentName(name)     ? "Letters, digits, spaces, _ and - only"
                          : other && other != app.current ? "Another component has that name"
                                                          : nullptr;
    if (problem) {
        ImGui::TextColored(Theme::Vec(Theme::WARNING_COLOR), "%s", problem);
    }
    else {
        ImGui::TextDisabled("Saves %s%s.yaml", componentsDirectory.c_str(), name.c_str());
    }
    ImGui::BeginDisabled(problem != nullptr);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK "  Save") || (enter && !problem)) {
        if (name != app.current->name) {
            RenameComponent(*app.current, name);
        }
        if (SaveComponent(*app.current)) {
            SetStatus("Saved '%s'", name.c_str());
        }
        else {
            SetStatus("Couldn't save '%s' to %s", name.c_str(), componentsDirectory.c_str());
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "  Cancel")) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}  // namespace Kestrel
