// KicadImport.cpp -- a KiCad netlist into a board component and its bench. See KicadImport.h.

#include "KicadImport.h"

#include "ComponentFile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace Kestrel {

// ---------------------------------------------------------------------------
// The netlist: an s-expression.

struct SExpr {
    std::string atom;
    std::vector<SExpr> list;
    bool isList = false;

    // The children that are lists starting with key.
    std::vector<const SExpr*> Find(const std::string& key) const {
        std::vector<const SExpr*> found;
        for (const SExpr& child : list) {
            if (child.isList && !child.list.empty() && child.list[0].atom == key) {
                found.push_back(&child);
            }
        }
        return found;
    }
    // (key value): the value of the first such child.
    std::string First(const std::string& key, const std::string& fallback = "") const {
        std::vector<const SExpr*> found = Find(key);
        return !found.empty() && found[0]->list.size() > 1 ? found[0]->list[1].atom : fallback;
    }
};

static SExpr ParseSExpr(const std::string& text) {
    std::vector<SExpr> stack(1);
    stack[0].isList = true;
    for (size_t at = 0; at < text.size();) {
        char character = text[at];
        if (isspace(static_cast<unsigned char>(character))) {
            at++;
        }
        else if (character == '(') {
            stack.emplace_back();
            stack.back().isList = true;
            at++;
        }
        else if (character == ')') {
            if (stack.size() > 1) {
                SExpr done = std::move(stack.back());
                stack.pop_back();
                stack.back().list.push_back(std::move(done));
            }
            at++;
        }
        else {
            SExpr atom;
            if (character == '"') {
                for (at++; at < text.size() && text[at] != '"'; at++) {
                    if (text[at] == '\\' && at + 1 < text.size()) {
                        at++;
                    }
                    atom.atom += text[at];
                }
                at++;
            }
            else {
                while (at < text.size() && !isspace(static_cast<unsigned char>(text[at])) && text[at] != '(' && text[at] != ')') {
                    atom.atom += text[at++];
                }
            }
            stack.back().list.push_back(std::move(atom));
        }
    }
    return stack[0].list.empty() ? SExpr{} : stack[0].list[0];
}

struct Part {
    std::string value, function;
};
using NetMember = std::pair<std::string, int>;  // reference, pin number

std::string FindKicadCli() {
    std::error_code error;
    std::string best;
    for (const char* root : {"C:/Program Files/KiCad", "C:/Program Files (x86)/KiCad"}) {
        if (!fs::is_directory(root, error)) {
            continue;
        }
        for (const auto& version : fs::directory_iterator(root, error)) {
            fs::path cli = version.path() / "bin" / "kicad-cli.exe";
            if (fs::exists(cli, error) && cli.string() > best) {
                best = cli.string();
            }
        }
    }
    return best.empty() ? "kicad-cli" : best;
}

// Reads the netlist, exporting it from the schematic first. Returns a problem, or "".
static std::string ReadNetlist(const std::string& schematic, std::map<std::string, Part>& parts,
                               std::map<std::string, std::vector<NetMember>>& nets) {
    std::string path = schematic;
    fs::path exported;
    if (fs::path(schematic).extension() != ".net") {
        std::error_code error;
        exported = fs::temp_directory_path(error) / "KestrelSimulator-import.net";
        fs::remove(exported, error);
        std::string cli = FindKicadCli();
        std::string command = "\"" + cli + "\" sch export netlist -o \"" + exported.string() + "\" \"" + schematic + "\"";
#ifdef _WIN32
        command = "\"" + command + " >NUL 2>&1\"";  // cmd /c drops the outer quotes
#else
        command += " >/dev/null 2>&1";
#endif
        if (std::system(command.c_str()) != 0 || !fs::exists(exported, error)) {
            return "kicad-cli couldn't export the netlist (is KiCad installed? tried " + cli + ")";
        }
        path = exported.string();
    }
    std::vector<uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        return "can't open " + path;
    }
    if (!exported.empty()) {
        std::error_code error;
        fs::remove(exported, error);
    }

    SExpr root = ParseSExpr(std::string(bytes.begin(), bytes.end()));
    std::vector<const SExpr*> components = root.Find("components"), netList = root.Find("nets");
    if (components.empty() || netList.empty()) {
        return path + " isn't a KiCad netlist";
    }
    for (const SExpr* comp : components[0]->Find("comp")) {
        Part part{comp->First("value"), ""};
        for (const SExpr* property : comp->Find("property")) {
            if (property->First("name") == "Function") {
                part.function = property->First("value");
            }
        }
        parts[comp->First("ref")] = part;
    }
    for (const SExpr* net : netList[0]->Find("net")) {
        std::vector<NetMember>& members = nets[net->First("name")];
        for (const SExpr* member : net->Find("node")) {
            members.push_back({member->First("ref"), std::atoi(member->First("pin").c_str())});
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Parts

// 74LS163, 74HC541, 74HCT08... -> the library's 74HC component, or nullptr.
static Component* ChipModel(const std::string& value) {
    static const std::regex pattern("^74[A-Z]*([0-9]+)[A-Z]*$");
    std::smatch match;
    return std::regex_match(value, match, pattern) ? FindComponent("74HC" + match[1].str()) : nullptr;
}

struct MemoryModel {
    const char* value;
    NodeKind kind;
    std::vector<int> address, data;              // hardware pin numbers of A0.., D0..
    std::vector<std::pair<int, const char*>> controls;
};

static const MemoryModel Memories[] = {
    {"AT28C64B", NodeKind::Rom, {10, 9, 8, 7, 6, 5, 4, 3, 25, 24, 21, 23, 2}, {11, 12, 13, 15, 16, 17, 18, 19}, {{20, "/CE"}, {22, "/OE"}}},
    {"CY62256", NodeKind::Ram, {10, 9, 8, 7, 6, 5, 4, 3, 25, 24, 21, 23, 2, 26, 1}, {11, 12, 13, 15, 16, 17, 18, 19}, {{20, "/CE"}, {22, "/OE"}, {27, "/WE"}}},
};

static const MemoryModel* FindMemory(const std::string& value) {
    for (const MemoryModel& model : Memories) {
        if (value.rfind(model.value, 0) == 0) {
            return &model;
        }
    }
    return nullptr;
}

static bool IsPowerNet(const std::string& net) {
    return net == "GND" || net == "VCC";
}

// "/ControlUnit/{slash}SC_MR" -> "/SC_MR". Labels can't hold '.', which separates node and pin.
static std::string PinLabel(const std::string& net) {
    std::string name = net[0] == '/' ? net.substr(net.rfind('/') + 1) : net;
    for (size_t at; (at = name.find("{slash}")) != std::string::npos;) {
        name.replace(at, 7, "/");
    }
    std::replace(name.begin(), name.end(), '.', '_');
    return name;
}

// U12 sorts before U101: by letter, then number.
static bool ReferenceLess(const std::string& a, const std::string& b) {
    auto number = [](const std::string& reference) {
        std::string digits;
        for (char character : reference) {
            if (isdigit(static_cast<unsigned char>(character))) {
                digits += character;
            }
        }
        return digits.empty() ? 0 : std::atoi(digits.c_str());
    };
    if (a[0] != b[0]) {
        return a[0] < b[0];
    }
    return number(a) != number(b) ? number(a) < number(b) : a < b;
}

// Puts a component built on the side into the library: as a new one, or in place of the one of
// that name (keeping its editor, so an open component stays open).
static Component* Install(Component& built) {
    Component* component = FindComponent(built.name);
    if (!component) {
        component = CreateComponent(built.name);
    }
    component->description = built.description;
    component->nodes = std::move(built.nodes);
    component->wires = std::move(built.wires);
    component->pendingWires = std::move(built.pendingWires);
    component->expandedBuses.clear();
    component->nextId = built.nextId;
    component->dirty = true;
    return component;
}

static uint32_t Place(Component& component, NodeKind kind, const std::string& typeName, const std::string& label, ImVec2 at) {
    uint32_t id = AddNode(component, kind, typeName, label);
    component.FindNode(id)->position = at;
    return id;
}

static std::string Pin(uint32_t node, const std::string& pin = "") {
    return std::to_string(node) + (pin.empty() ? "" : "." + pin);
}

// ---------------------------------------------------------------------------

// Builds the board; with install, puts it and its bench in the library. inputs gets the board's inputs.
static KicadImportResult Import(const KicadImportOptions& options, bool install, std::vector<std::string>& inputs) {
    KicadImportResult result;
    std::string name = options.name.empty() ? fs::path(options.schematic).stem().string() : options.name;
    if (!IsValidComponentName(name) || !IsValidComponentName(name + " Bench")) {
        result.error = "'" + name + "' can't be a component name (letters, digits, spaces, _ and -, up to " +
                       std::to_string(MaxNameLength - 7) + " characters)";
        return result;
    }
    std::map<std::string, Part> parts;
    std::map<std::string, std::vector<NetMember>> nets;
    result.error = ReadNetlist(options.schematic, parts, nets);
    if (!result.error.empty()) {
        return result;
    }
    for (auto& entry : parts) {
        const Component* chip = ChipModel(entry.second.value);
        if (chip && (chip->name == name || chip->name == name + " Bench")) {
            result.error = "the board can't be named after a part on it (" + chip->name + ")";
            return result;
        }
    }
    // Images given win; otherwise a ROM or RAM keeps the image it had when this board was last imported.
    std::map<std::string, std::string> images;
    if (const Component* previous = FindComponent(name)) {
        for (const Node& node : previous->nodes) {
            if (IsMemory(node.kind) && !node.memory->image.empty()) {
                images[node.label.substr(0, node.label.find(' '))] = node.memory->image;
            }
        }
    }
    for (auto& [reference, image] : options.images) {
        images[reference] = image;
    }

    Component board;
    board.name = name;
    board.description = "Imported from " + fs::path(options.schematic).filename().string() +
                        ". Edit the schematic and import it again, not this.";
    std::vector<std::string>& warnings = result.warnings;
    auto at = [&] {
        size_t index = board.nodes.size();
        return ImVec2(40.0f + 260.0f * static_cast<float>(index / 12), 40.0f + 140.0f * static_cast<float>(index % 12));
    };

    struct Endpoint {
        uint32_t node = 0;  // 0 for a ROM's /WE, which the model doesn't have
        std::string pin;
        bool drives = false, reads = false, triState = false;
    };
    std::map<NetMember, Endpoint> endpoints;

    std::vector<std::string> references;
    for (auto& entry : parts) {
        references.push_back(entry.first);
    }
    std::sort(references.begin(), references.end(), ReferenceLess);
    for (const std::string& reference : references) {
        const Part& part = parts[reference];
        std::string label = Trim(reference + " " + part.function);
        if (Component* chip = ChipModel(part.value)) {
            uint32_t node = Place(board, NodeKind::Component, chip->name, "", at());
            board.FindNode(node)->label = label;
            for (const Node& pin : chip->nodes) {
                if ((pin.kind != NodeKind::Input && pin.kind != NodeKind::Output) || pin.packagePin <= 0) {
                    continue;
                }
                Endpoint endpoint{node, pin.label, pin.kind == NodeKind::Output, pin.kind == NodeKind::Input, false};
                for (const Wire& wire : chip->wires) {
                    const Node* from = chip->FindNode(wire.fromNode);
                    if (wire.toNode == pin.id && from && from->kind == NodeKind::TriState) {
                        endpoint.triState = true;
                    }
                }
                endpoints[{reference, pin.packagePin}] = endpoint;
            }
            continue;
        }
        if (const MemoryModel* memory = FindMemory(part.value)) {
            uint32_t node = Place(board, memory->kind, "", "", at());
            board.FindNode(node)->label = label;
            MemoryData& data = *board.FindNode(node)->memory;
            data.Resize(static_cast<int>(memory->address.size()), static_cast<int>(memory->data.size()));
            std::fill(data.words.begin(), data.words.end(), 0);
            auto image = images.find(reference);
            if (image != images.end()) {
                data.image = image->second;
                std::string problem = LoadRomImage(data);
                if (!problem.empty()) {
                    warnings.push_back(label + ": " + problem);
                }
            }
            else if (memory->kind == NodeKind::Rom) {
                warnings.push_back(label + ": no image given, so it reads all zeros");
            }
            bool ram = memory->kind == NodeKind::Ram;
            for (size_t bit = 0; bit < memory->address.size(); bit++) {
                endpoints[{reference, memory->address[bit]}] = {node, "A" + std::to_string(bit), false, true, false};
            }
            for (size_t bit = 0; bit < memory->data.size(); bit++) {
                endpoints[{reference, memory->data[bit]}] = {node, "D" + std::to_string(bit), true, ram, true};
            }
            for (auto& [number, pin] : memory->controls) {
                endpoints[{reference, number}] = {node, pin, false, true, false};
            }
            if (!ram) {
                endpoints[{reference, 27}] = {0, "/WE", false, false, false};
            }
            continue;
        }
        if (reference[0] == '#' || reference[0] == 'C' || part.value == "C" || part.value == "R") {
            continue;  // power flags, capacitors: no logic
        }
        warnings.push_back(label + " (" + part.value + "): no model for this part; its pins are left out");
    }

    std::map<std::string, uint32_t> constants;
    std::vector<std::string> outputs;
    for (auto& [net, members] : nets) {
        struct Driver {
            std::string end;
            bool triState;
            std::string who;
        };
        std::vector<Driver> drivers;
        std::vector<std::pair<std::string, NetMember>> readers;
        for (const NetMember& member : members) {
            auto part = parts.find(member.first);
            if (part == parts.end() || part->second.value == "C") {
                continue;
            }
            auto found = endpoints.find(member);
            if (found == endpoints.end()) {
                if (!IsPowerNet(net) && ChipModel(part->second.value)) {
                    static const std::regex unconnected(R"(unconnected-\(.*?-(.*)-Pad\d+\))");
                    std::smatch match;
                    std::string pin = std::to_string(member.second);
                    warnings.push_back(std::regex_match(net, match, unconnected)
                                           ? member.first + " pin " + pin + " (" + match[1].str() + ") is unconnected"
                                           : member.first + " pin " + pin + ", a power pin, is on " + net);
                }
                continue;
            }
            const Endpoint& endpoint = found->second;
            if (endpoint.node == 0) {
                if (net != "VCC") {
                    warnings.push_back(member.first + " /WE is on " + net + ", not VCC: the real chip could be written; the simulation never writes it");
                }
                continue;
            }
            if (endpoint.drives) {
                drivers.push_back({Pin(endpoint.node, endpoint.pin), endpoint.triState, member.first});
            }
            if (endpoint.reads) {
                readers.push_back({Pin(endpoint.node, endpoint.pin), member});
            }
        }
        if (IsPowerNet(net)) {
            if (!constants.count(net)) {
                constants[net] = Place(board, net == "VCC" ? NodeKind::High : NodeKind::Low, "", "", at());
            }
            drivers.insert(drivers.begin(), {Pin(constants[net]), false, net});
        }
        if (net.rfind("unconnected-", 0) == 0) {
            for (auto& reader : readers) {
                warnings.push_back(reader.second.first + " pin " + std::to_string(reader.second.second) +
                                   " is an input left floating (the simulation reads 0; a real HC input wanders)");
            }
            continue;
        }
        std::string label = PinLabel(net);
        if (drivers.empty() && !readers.empty()) {
            drivers.push_back({Pin(Place(board, NodeKind::Input, "", label, at())), false, "input"});
            inputs.push_back(label);
        }
        std::vector<std::string> totem, all;
        for (const Driver& driver : drivers) {
            all.push_back(driver.who);
            if (!driver.triState) {
                totem.push_back(driver.who);
            }
        }
        if (drivers.size() > 1 && !totem.empty()) {
            auto join = [](const std::vector<std::string>& list) {
                std::string text;
                for (const std::string& item : list) {
                    text += (text.empty() ? "" : ", ") + item;
                }
                return text;
            };
            warnings.push_back("net " + net + ": " + join(all) + " drive it together, and " + join(totem) +
                               " can't let go: a short on the real board");
        }
        if (!drivers.empty() && readers.empty()) {
            if (IsPowerNet(net)) {
                continue;
            }
            readers.push_back({Pin(Place(board, NodeKind::Output, "", label, at())), {"output", 0}});
            outputs.push_back(label);
        }
        for (auto& reader : readers) {
            board.pendingWires.push_back({drivers[0].end, reader.first});
        }
        for (size_t index = 1; index < drivers.size(); index++) {
            board.pendingWires.push_back({drivers[index].end, readers[0].first});
        }
    }
    if (!install) {
        return result;
    }
    Component& installed = *Install(board);
    for (const std::string& problem : ResolveWires(installed)) {
        warnings.push_back(problem);
    }

    // The bench: a clock, and reset held for the first two rising edges (two flip-flops shifting in a 1).
    Component benchBuilt;
    benchBuilt.name = name + " Bench";
    benchBuilt.description = name + " with a clock and a power-on reset.";
    Component& bench = benchBuilt;
    uint32_t instance = Place(bench, NodeKind::Component, name, "", ImVec2(400, 40));
    bench.FindNode(instance)->label = "Board";
    uint32_t clock = Place(bench, NodeKind::Clock, "", "", ImVec2(40, 40));
    uint32_t high = Place(bench, NodeKind::High, "", "", ImVec2(40, 140));
    uint32_t reset1 = Place(bench, NodeKind::FlipFlop, "", "", ImVec2(200, 140));
    uint32_t reset2 = Place(bench, NodeKind::FlipFlop, "", "", ImVec2(200, 240));
    bench.FindNode(reset1)->label = "reset 1";
    bench.FindNode(reset2)->label = "reset 2";
    bench.pendingWires = {{Pin(high), Pin(reset1, "D")}, {Pin(clock), Pin(reset1, "CLK")},
                          {Pin(reset1, "Q"), Pin(reset2, "D")}, {Pin(clock), Pin(reset2, "CLK")}};
    std::set<std::string> resets(options.resets.begin(), options.resets.end());
    for (const std::string& input : inputs) {
        if (input == options.clock) {
            bench.pendingWires.push_back({Pin(clock), Pin(instance, input)});
        }
        else if (resets.count(input)) {
            bench.pendingWires.push_back({Pin(reset2, input[0] == '/' ? "Q" : "/Q"), Pin(instance, input)});
        }
        else {
            warnings.push_back("board input " + input + ": nothing on the board drives it; the bench leaves it at 0");
        }
    }
    std::vector<std::string> benchNets = options.resets;
    benchNets.insert(benchNets.begin(), options.clock);
    for (const std::string& net : benchNets) {
        if (!net.empty() && std::find(inputs.begin(), inputs.end(), net) == inputs.end()) {
            warnings.push_back("bench: " + net + " is not a board input, so the bench leaves it alone");
        }
    }
    for (size_t index = 0; index < outputs.size(); index++) {
        uint32_t output = Place(bench, NodeKind::Output, "", outputs[index], ImVec2(900, 40.0f + 60.0f * static_cast<float>(index)));
        bench.pendingWires.push_back({Pin(instance, outputs[index]), Pin(output)});
    }
    Component& installedBench = *Install(benchBuilt);
    for (const std::string& problem : ResolveWires(installedBench)) {
        warnings.push_back(problem);
    }

    auto list = [](const std::vector<std::string>& items) {
        std::string text;
        for (const std::string& item : items) {
            text += (text.empty() ? "" : ", ") + item;
        }
        return text.empty() ? std::string("-") : text;
    };
    result.board = &installed;
    result.bench = &installedBench;
    result.summary = name + ": " + std::to_string(parts.size()) + " parts, " + std::to_string(nets.size()) +
                     " nets; inputs " + list(inputs) + "; outputs " + list(outputs);
    return result;
}

KicadImportResult ImportKicad(const KicadImportOptions& options) {
    std::vector<std::string> inputs;
    return Import(options, true, inputs);
}

std::string ReadKicadInputs(const std::string& schematic, std::vector<std::string>& inputs) {
    KicadImportOptions options;
    options.schematic = schematic;
    options.name = "Preview";  // any valid name: nothing is installed
    inputs.clear();
    return Import(options, false, inputs).error;
}

}  // namespace Kestrel
