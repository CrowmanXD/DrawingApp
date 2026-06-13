#pragma once

#include <string>
#include <fstream>
#include "StringUtils.h"

class ConfigManager {
public:
    /// Get the API domain from settings.ini
    /// Returns fallback if file not found
    static std::string getApiDomain(const std::string& fallback = "fallback.trycloudflare.com") {
        std::string apiDomain = fallback;
        std::ifstream setFile("settings.ini");

        if (setFile.is_open()) {
            std::string line;
            while (std::getline(setFile, line)) {
                if (line.find("ApiDomain=") == 0) {
                    apiDomain = line.substr(10);  // Extract everything after "ApiDomain="
                    break;
                }
            }
            setFile.close();
        }

        return StringUtils::trimEnd(apiDomain);
    }

    /// Get the API token from settings.ini
    /// Returns fallback if file not found
    static std::string getApiToken(const std::string& fallback = "") {
        std::string apiToken = fallback;
        std::ifstream setFile("settings.ini");

        if (setFile.is_open()) {
            std::string line;
            while (std::getline(setFile, line)) {
                if (line.find("ApiToken=") == 0) {
                    apiToken = line.substr(9);  // Extract everything after "ApiToken="
                    break;
                }
            }
            setFile.close();
        }

        return StringUtils::trimEnd(apiToken);
    }
};
