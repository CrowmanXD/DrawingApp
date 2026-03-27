#pragma once
#include <string>

class FileDialogs {
public:
    // Returns the selected file path, or an empty string if the user cancels
    static std::string openFile(const char* filter);
    static std::string saveFile(const char* filter);
};
