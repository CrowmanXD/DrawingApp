#include "ClipboardHelper.h"
#include <vector>
#include <algorithm>
#include <utility>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

// Define internal cache variables
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

    // --- USER COPIED A FILE (CF_HDROP) ---
    HANDLE hDrop = GetClipboardData(CF_HDROP);
    if (hDrop) {
        HDROP dropInfo = static_cast<HDROP>(GlobalLock(hDrop));
        if (dropInfo) {
            char filePath[MAX_PATH];
            // Extract the path of the first copied file
            if (DragQueryFileA(dropInfo, 0, filePath, MAX_PATH)) {
                img.loadFromFile(filePath); // Let SFML load the image directly from the disk
            }
            GlobalUnlock(hDrop);
        }
        CloseClipboard();
        return img;
    }

    // --- USER COPIED PIXELS (CF_DIB - Snipping Tool, Browser) ---
    // Retrieve the handle to the clipboard data in Device-Independent Bitmap (DIB) format
    HANDLE hData = GetClipboardData(CF_DIB);
    if (hData) {
        // Lock the global memory handle to safely read the DIB data
        std::uint8_t* rawData = static_cast<std::uint8_t*>(GlobalLock(hData));
        if (rawData) {
            // The DIB memory block starts immediately with the BITMAPINFOHEADER structure.
            BITMAPINFOHEADER* bi = reinterpret_cast<BITMAPINFOHEADER*>(rawData);

            int width = bi->biWidth;
            int height = bi->biHeight;

            // Windows bitmaps can be stored top-down (negative height) or bottom-up (positive height).
            bool isBottomUp = (height > 0);
            if (height < 0) height = -height; // Normalize height to a positive value

            int bpp = bi->biBitCount; // Bits per pixel

            // Only process standard high-color (24-bit) and true-color (32-bit) formats
            if (bpp == 24 || bpp == 32) {

                // Calculate where the actual pixel data begins relative to rawData
                int offset = bi->biSize; // Standard DIB header size

                // If BI_BITFIELDS is used, three 32-bit color masks (12 bytes) follow the header
                if (bi->biCompression == BI_BITFIELDS) offset += 12;
                // Otherwise, if the bitmap includes a color palette, skip over it
                else if (bi->biClrUsed > 0) offset += bi->biClrUsed * sizeof(RGBQUAD);

                std::uint8_t* srcPixels = rawData + offset;

                // Windows requires every horizontal row (stride) to be aligned to a 4-byte boundary
                int rowStride = ((width * bpp + 31) / 32) * 4;

                // Prepare a destination vector for 4-channel (RGBA) internal SFML pixels
                std::vector<std::uint8_t> pixels(width * height * 4);
                bool hasAlpha = false;

                // Iterate through pixels and rearrange them for the internal SFML format (RGBA instead of BGR)
                for (int y = 0; y < height; ++y) {
                    // If bottom-up, invert the row index to read from the bottom of the source memory upward
                    int srcY = isBottomUp ? (height - 1 - y) : y;
                    std::uint8_t* srcRow = srcPixels + srcY * rowStride;
                    std::uint8_t* destRow = pixels.data() + y * width * 4;

                    for (int x = 0; x < width; ++x) {
                        // Windows DIB stores colors as Blue-Green-Red (BGR), while SFML expects Red-Green-Blue (RGB)
                        destRow[x * 4 + 0] = srcRow[x * (bpp / 8) + 2]; // Red (Reversed from BGR)
                        destRow[x * 4 + 1] = srcRow[x * (bpp / 8) + 1]; // Green
                        destRow[x * 4 + 2] = srcRow[x * (bpp / 8) + 0]; // Blue (Reversed from BGR)

                        // Handle the alpha channel depending on the source depth
                        if (bpp == 32) {
                            destRow[x * 4 + 3] = srcRow[x * (bpp / 8) + 3]; // Alpha
                            // Check if the image actually uses semi-transparency
                            if (destRow[x * 4 + 3] > 0 && destRow[x * 4 + 3] < 255) {
                                hasAlpha = true;
                            }
                        }
                        else {
                            // 24-bit images have no alpha channel; set destination alpha to fully opaque
                            destRow[x * 4 + 3] = 255;
                        }
                    }
                }

                // If it's a 32-bit image but all alpha channels were 0, treat it as opaque
                if (!hasAlpha && bpp == 32) {
                    for (size_t i = 0; i < pixels.size(); i += 4) pixels[i + 3] = 255;
                }

                // Transfer the parsed pixel array into the SFML image object
                img.resize(sf::Vector2u(width, height), pixels.data());
            }
            // Always unlock global memory when finished reading
            GlobalUnlock(hData);
        }
    }
    // Release ownership of the clipboard so other applications can access it
    CloseClipboard();

    return img;
#else
    return s_internalClipboard;
#endif
}

void ClipboardHelper::setImage(const sf::Image& img) {
    // Cache it in RAM before the OS gets it
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

    // Grab the Sequence Number AFTER writing to verify if the user copies 
    // something outside the app later
    s_lastSequenceNumber = GetClipboardSequenceNumber();
#endif
}