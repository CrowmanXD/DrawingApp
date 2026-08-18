#include "FileDialogs.h"
#include <windows.h>
#include <commdlg.h>

std::string FileDialogs::openFile(const char* filter) {
    OPENFILENAMEA ofn;
    CHAR szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    // OFN_NOCHANGEDIR ensures the dialog doesn't mess up your app's working directory
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn) == TRUE) {
        return ofn.lpstrFile;
    }
    return std::string();
}

std::string FileDialogs::saveFile(const char* filter) {
    OPENFILENAMEA ofn;
    CHAR szFile[260] = { 0 };
    ZeroMemory(&ofn, sizeof(OPENFILENAME));
    ofn.lStructSize = sizeof(OPENFILENAME);
    // Window is independent
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    // Filters what files are allowed
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    // OFN_OVERWRITEPROMPT warns the user if they are about to overwrite an existing image
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    // Automatically append .png if the user forgets to type it
    ofn.lpstrDefExt = "png";

    if (GetSaveFileNameA(&ofn) == TRUE) {
        return ofn.lpstrFile;
    }
    return std::string();
}