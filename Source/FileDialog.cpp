// FileDialog.cpp -- the system's Open file dialog. See FileDialog.h.

#include "FileDialog.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include <filesystem>

namespace Kestrel {

static std::wstring Wide(const std::string& text) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<size_t>(count > 0 ? count - 1 : 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), count);
    return wide;
}

static std::string Narrow(const wchar_t* text) {
    int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(static_cast<size_t>(count > 0 ? count - 1 : 0), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, narrow.data(), count, nullptr, nullptr);
    return narrow;
}

std::string OpenFileDialog(const char* title, const char* filter, const std::string& startPath) {
    std::wstring filters = Wide(filter);
    for (wchar_t& character : filters) {
        if (character == L'|') {
            character = L'\0';
        }
    }
    filters += L'\0';  // the list ends with two zeros

    wchar_t path[MAX_PATH] = L"";
    std::wstring directory;
    std::error_code error;
    if (!startPath.empty()) {
        std::filesystem::path start(Wide(startPath));
        directory = std::filesystem::is_directory(start, error) ? start.wstring() : start.parent_path().wstring();
    }
    std::wstring wideTitle = Wide(title);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter = filters.c_str();
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = wideTitle.c_str();
    dialog.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? Narrow(path) : "";
}

}  // namespace Kestrel

#else

namespace Kestrel {

std::string OpenFileDialog(const char*, const char*, const std::string&) {
    return "";
}

}  // namespace Kestrel

#endif
