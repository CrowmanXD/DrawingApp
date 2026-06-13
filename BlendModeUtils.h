#pragma once

#include <string>
#include "Canvas.h"
#include "StringUtils.h"

class BlendModeUtils {
public:
    /// Convert string representation to LayerBlendMode enum
    /// Examples: "multiply", "add", "passthrough", "normal"
    static LayerBlendMode fromString(const std::string& modeStr) {
        std::string lower = StringUtils::toLower(modeStr);

        if (lower.find("multiply") != std::string::npos) {
            return LayerBlendMode::Multiply;
        }
        else if (lower.find("add") != std::string::npos || lower.find("dodge") != std::string::npos) {
            return LayerBlendMode::Add;
        }
        else if (lower.find("passthrough") != std::string::npos) {
            return LayerBlendMode::PassThrough;
        }
        else {
            return LayerBlendMode::Normal;
        }
    }

    /// Convert integer to LayerBlendMode enum
    static LayerBlendMode fromInt(int modeInt) {
        switch (modeInt) {
            case 0: return LayerBlendMode::Normal;
            case 1: return LayerBlendMode::Multiply;
            case 2: return LayerBlendMode::Add;
            case 3: return LayerBlendMode::PassThrough;
            default: return LayerBlendMode::Normal;
        }
    }

    /// Convert LayerBlendMode to integer
    static int toInt(LayerBlendMode mode) {
        return static_cast<int>(mode);
    }

    /// Get human-readable name for blend mode
    static std::string getName(LayerBlendMode mode) {
        switch (mode) {
            case LayerBlendMode::Normal: return "Normal";
            case LayerBlendMode::Multiply: return "Multiply";
            case LayerBlendMode::Add: return "Add (Linear Dodge)";
            case LayerBlendMode::PassThrough: return "Pass Through";
            default: return "Unknown";
        }
    }
};
