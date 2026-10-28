// FileDialog.h -- the system's Open file dialog. Kept apart because windows.h and raylib.h clash.

#pragma once

#include <string>

namespace Kestrel {

// Asks for a file to open; filter is "Description|*.ext;*.ext2" pairs, separated by '|'.
// Returns the chosen path, or "" if cancelled (or there is no dialog on this system).
std::string OpenFileDialog(const char* title, const char* filter, const std::string& startPath = "");

}  // namespace Kestrel
