#include "ClipboardHelper.h"
#include <vector>
#include <algorithm>
#include <utility>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

sf::Image ClipboardHelper::getImage() {
    sf::Image img;

#ifdef _WIN32
    if (!OpenClipboard(nullptr)) return img;

    // Grab the raw Bitmap data from Windows
    HBITMAP hBitmap = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    if (hBitmap) {
        BITMAP bm;
        GetObject(hBitmap, sizeof(bm), &bm);

        // Force Windows to give us a clean 32-bit pixel array
        BITMAPINFOHEADER bi = { 0 };
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bm.bmWidth;
        bi.biHeight = -bm.bmHeight; // Negative ensures the image isn't flipped upside-down
        bi.biPlanes = 1;
        bi.biBitCount = 32;
        bi.biCompression = BI_RGB;

        HDC hdc = GetDC(nullptr);

        std::vector<std::uint8_t> pixels(bm.bmWidth * bm.bmHeight * 4);

        GetDIBits(hdc, hBitmap, 0, bm.bmHeight, pixels.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
        ReleaseDC(nullptr, hdc);

        // Windows stores pixels as BGRA, but SFML expects RGBA.
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i + 2]); // Swap Red and Blue
            pixels[i + 3] = 255;                 // Force Alpha to fully opaque
        }

        img.resize(sf::Vector2u(bm.bmWidth, bm.bmHeight), pixels.data());
    }
    CloseClipboard();
#endif

    return img;
}