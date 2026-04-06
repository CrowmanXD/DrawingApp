#include "ClipboardHelper.h"
#include <vector>
#include <algorithm>
#include <utility>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

// Define our internal cache variables
sf::Image ClipboardHelper::s_internalClipboard;
unsigned long ClipboardHelper::s_lastSequenceNumber = 0;

sf::Image ClipboardHelper::getImage() {
#ifdef _WIN32
    // Bypass the Windows API entirely if the OS clipboard hasn't changed
    if (GetClipboardSequenceNumber() == s_lastSequenceNumber && s_internalClipboard.getSize().x > 0) {
        return s_internalClipboard;
    }

    sf::Image img;
    if (!OpenClipboard(nullptr)) return img;

    HBITMAP hBitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (hBitmap) {
        BITMAP bm;
        GetObject(hBitmap, sizeof(bm), &bm);

        BITMAPINFOHEADER bi = { 0 };
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bm.bmWidth;
        bi.biHeight = -bm.bmHeight;
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        HDC hdc = GetDC(nullptr);
        std::vector<std::uint8_t> pixels(bm.bmWidth * bm.bmHeight * 4);
        GetDIBits(hdc, hBitmap, 0, bm.bmHeight, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        // If ANY pixel uses the alpha channel, preserve it.
        bool hasAlpha = false;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]); // Swap Red and Blue
            if (pixels[i + 3] > 0) hasAlpha = true;
        }

        if (!hasAlpha) {
            for (size_t i = 0; i < pixels.size(); i += 4) pixels[i + 3] = 255;
        }

        img.resize(sf::Vector2u(bm.bmWidth, bm.bmHeight), pixels.data());
    }
    CloseClipboard();
    return img;
#else
    return s_internalClipboard;
#endif
}

void ClipboardHelper::setImage(const sf::Image& img) {
    // Cache it in RAM before the OS gets its hands on it
    s_internalClipboard = img;

#ifdef _WIN32
    sf::Vector2u size = img.getSize();
    if (size.x == 0 || size.y == 0) return;

    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    size_t headerSize = sizeof(BITMAPINFOHEADER);
    size_t dataSize = size.x * size.y * 4;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, headerSize + dataSize);

    if (hMem) {
        std::uint8_t* memData = static_cast<std::uint8_t*>(GlobalLock(hMem));
        BITMAPINFOHEADER* bi = reinterpret_cast<BITMAPINFOHEADER*>(memData);

        bi->biSize = sizeof(BITMAPINFOHEADER);
        bi->biWidth = size.x;
        bi->biHeight = -static_cast<LONG>(size.y);
        bi->biPlanes = 1;
        bi->biBitCount = 32;
        bi->biCompression = BI_RGB;
        bi->biSizeImage = static_cast<DWORD>(dataSize);
        bi->biXPelsPerMeter = 0;
        bi->biYPelsPerMeter = 0;
        bi->biClrUsed = 0;
        bi->biClrImportant = 0;

        std::uint8_t* destPixels = memData + headerSize;
        const std::uint8_t* srcPixels = img.getPixelsPtr();

        for (size_t i = 0; i < dataSize; i += 4) {
            destPixels[i] = srcPixels[i + 2];
            destPixels[i + 1] = srcPixels[i + 1];
            destPixels[i + 2] = srcPixels[i];
            destPixels[i + 3] = srcPixels[i + 3];
        }

        GlobalUnlock(hMem);
        SetClipboardData(CF_DIB, hMem);
    }
    CloseClipboard();

    // Grab the Sequence Number AFTER writing so we can verify if the user copies 
    // something outside the app later
    s_lastSequenceNumber = GetClipboardSequenceNumber();
#endif
}