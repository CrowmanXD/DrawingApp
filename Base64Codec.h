#pragma once

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

#include <string>
#include <vector>

class Base64Codec {
public:
    /// Encode binary data to Base64 string using Windows Crypto API
    /// Returns empty string on failure
    static std::string encode(const std::vector<char>& data) {
#ifdef _WIN32
        DWORD b64Len = 0;
        if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(data.data()), 
            static_cast<DWORD>(data.size()), 
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, 
            NULL, 
            &b64Len)) {
            return "";
        }

        std::string base64Img(b64Len, '\0');
        if (!CryptBinaryToStringA(
            reinterpret_cast<const BYTE*>(data.data()), 
            static_cast<DWORD>(data.size()), 
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, 
            &base64Img[0], 
            &b64Len)) {
            return "";
        }

        // Remove null terminators
        while (!base64Img.empty() && base64Img.back() == '\0') {
            base64Img.pop_back();
        }
        return base64Img;
#else
        return "";  // Placeholder for non-Windows
#endif
    }

    /// Decode Base64 string to binary data using Windows Crypto API
    /// Returns empty vector on failure
    static std::vector<BYTE> decode(const std::string& b64) {
#ifdef _WIN32
        DWORD binaryLen = 0;
        if (!CryptStringToBinaryA(
            b64.c_str(), 
            static_cast<DWORD>(b64.length()), 
            CRYPT_STRING_BASE64, 
            NULL, 
            &binaryLen, 
            NULL, 
            NULL)) {
            return std::vector<BYTE>();
        }

        std::vector<BYTE> binaryData(binaryLen);
        if (!CryptStringToBinaryA(
            b64.c_str(), 
            static_cast<DWORD>(b64.length()), 
            CRYPT_STRING_BASE64, 
            binaryData.data(), 
            &binaryLen, 
            NULL, 
            NULL)) {
            return std::vector<BYTE>();
        }

        return binaryData;
#else
        return std::vector<BYTE>();  // Placeholder for non-Windows
#endif
    }
};
