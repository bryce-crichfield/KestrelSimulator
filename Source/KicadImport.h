// KicadImport.h -- turns a KiCad schematic of 74-series logic into components.
//
// kicad-cli exports the schematic's netlist (a .net file can be given instead), and every part on
// it becomes what it is on the board:
//     74xx chips      an instance of the library's 74HC component of the same number (74LS and
//                     74HC alike: the logic is the same), pin for pin by package pin number
//     AT28C64B        a ROM, 8K x 8; its contents from an image (relative to the components
//                     directory), else all zeros
//     CY62256         a RAM, 32K x 8
//     capacitors      nothing: they have no logic
//     GND, VCC        Low and High
// and every net joins the pins KiCad says it joins. Nets that nothing on the board drives (the
// clock, reset) become the component's inputs; driven nets that nothing reads become its outputs.
//
// It makes two components, unsaved: the board, net for net, and "NAME Bench", the board with a
// clock on the clock net and a power-on reset (held for two clocks) on each reset net (a name
// starting with / is active low). An existing component of either name is replaced.
//
// The warnings say what a real board would get wrong: inputs left floating, nets two outputs
// fight over with no tri-state between them, and nets nothing reads or nothing drives.

#pragma once

#include "Circuit.h"

#include <string>
#include <utility>
#include <vector>

namespace Kestrel {

struct KicadImportOptions {
    std::string schematic;                                   // the top-level .kicad_sch, or a .net
    std::string name;                                        // the board component's name; "" for the file's name
    std::string clock = "CLK";                               // the board input the bench clocks
    std::vector<std::string> resets;                         // board inputs the bench holds in reset
    std::vector<std::pair<std::string, std::string>> images; // reference (U12) -> ROM or RAM image
};

struct KicadImportResult {
    std::string error;  // why nothing was imported, or ""
    Component* board = nullptr;
    Component* bench = nullptr;
    std::string summary;
    std::vector<std::string> warnings;
};

// Where kicad-cli is: the newest KiCad under Program Files, else just "kicad-cli" (on the PATH).
std::string FindKicadCli();
KicadImportResult ImportKicad(const KicadImportOptions& options);
// The board's inputs (the nets nothing on it drives: what the bench can clock or reset), without
// importing anything. Returns a problem, or "".
std::string ReadKicadInputs(const std::string& schematic, std::vector<std::string>& inputs);

}  // namespace Kestrel
