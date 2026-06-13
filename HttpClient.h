#pragma once

#ifdef _WIN32
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "wininet.lib")
#endif

#include <string>
#include <optional>

/// RAII wrapper for WinINet handles
class WinINetHandle {
private:
    HINTERNET m_handle = NULL;

public:
    WinINetHandle() = default;

    explicit WinINetHandle(HINTERNET handle) : m_handle(handle) {}

    ~WinINetHandle() {
        close();
    }

    // Delete copy operations
    WinINetHandle(const WinINetHandle&) = delete;
    WinINetHandle& operator=(const WinINetHandle&) = delete;

    // Allow move operations
    WinINetHandle(WinINetHandle&& other) noexcept : m_handle(other.release()) {}
    WinINetHandle& operator=(WinINetHandle&& other) noexcept {
        if (this != &other) {
            close();
            m_handle = other.release();
        }
        return *this;
    }

    HINTERNET get() const { return m_handle; }
    HINTERNET release() {
        HINTERNET temp = m_handle;
        m_handle = NULL;
        return temp;
    }

    bool isValid() const { return m_handle != NULL; }

    void close() {
        if (m_handle != NULL) {
            InternetCloseHandle(m_handle);
            m_handle = NULL;
        }
    }

    explicit operator bool() const {
        return isValid();
    }
};

/// HTTP response data
struct HttpResponse {
    DWORD statusCode = 0;
    std::string body;
    bool success = false;
    std::string errorMessage;
};

/// Simple HTTP client using WinINet
class HttpClient {
public:
    /// Send HTTP POST request
    /// Returns response or error information
    static HttpResponse post(
        const std::string& domain,
        const std::string& path,
        const std::string& payload,
        const std::string& contentType = "application/json") {
#ifdef _WIN32
        HttpResponse response;

        WinINetHandle hSession(InternetOpenA(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
            INTERNET_OPEN_TYPE_DIRECT,
            NULL, NULL, 0));

        if (!hSession) {
            response.errorMessage = "Failed to open Internet session. Code: " + std::to_string(GetLastError());
            return response;
        }

        WinINetHandle hConnect(InternetConnectA(
            hSession.get(),
            domain.c_str(),
            INTERNET_DEFAULT_HTTPS_PORT,
            NULL, NULL,
            INTERNET_SERVICE_HTTP, 0, 0));

        if (!hConnect) {
            response.errorMessage = "Failed to connect to " + domain + ". Code: " + std::to_string(GetLastError());
            return response;
        }

        WinINetHandle hRequest(HttpOpenRequestA(
            hConnect.get(),
            "POST",
            path.c_str(),
            NULL, NULL, NULL,
            INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0));

        if (!hRequest) {
            response.errorMessage = "Failed to open HTTP request. Code: " + std::to_string(GetLastError());
            return response;
        }

        std::string headers = "Content-Type: " + contentType + "\r\n"
            "Accept: application/json\r\n"
            "Connection: close\r\n";

        if (!HttpSendRequestA(
            hRequest.get(),
            headers.c_str(),
            static_cast<DWORD>(headers.length()),
            const_cast<LPVOID>(static_cast<const void*>(payload.c_str())),
            static_cast<DWORD>(payload.length()))) {
            response.errorMessage = "Failed to send HTTP request. Code: " + std::to_string(GetLastError());
            return response;
        }

        DWORD statusCode = 0;
        DWORD length = sizeof(DWORD);
        if (!HttpQueryInfoA(
            hRequest.get(),
            HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
            &statusCode, &length, NULL)) {
            response.errorMessage = "Failed to query HTTP status code. Code: " + std::to_string(GetLastError());
            return response;
        }

        response.statusCode = statusCode;

        // Read response body
        char buffer[8192];
        DWORD bytesRead = 0;
        while (InternetReadFile(hRequest.get(), buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
            response.body.append(buffer, bytesRead);
        }

        response.success = (statusCode == 200);
        if (!response.success) {
            response.errorMessage = "Server returned status code " + std::to_string(statusCode);
        }

        return response;
#else
        HttpResponse response;
        response.errorMessage = "HTTP client not available on this platform";
        return response;
#endif
    }
};
