#pragma once

#include <string>
#include <fstream>
#include "StringUtils.h"

class ConfigManager {
private:
    static std::string readConfigValue(const std::string& key, const std::string& fallback) {
        std::string result = fallback;
        std::ifstream setFile("settings.ini");

        if (setFile.is_open()) {
            std::string line;
            while (std::getline(setFile, line)) {
                if (line.find(key) == 0) {
                    result = line.substr(key.length());
                    break;
                }
            }
        }

        return StringUtils::trimEnd(result);
    }

public:
    static std::string getApiDomain(const std::string& fallback = "fallback.trycloudflare.com") {
        return readConfigValue("ApiDomain=", fallback);
    }

    static std::string getApiToken(const std::string& fallback = "") {
        return readConfigValue("ApiToken=", fallback);
    }
};
