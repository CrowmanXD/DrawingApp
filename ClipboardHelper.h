#pragma once
#include <SFML/Graphics/Image.hpp>

class ClipboardHelper {
public:
    static sf::Image getImage();
    static void setImage(const sf::Image& img);

private:
    static sf::Image s_internalClipboard;
    static unsigned long s_lastSequenceNumber; // Matches the Windows DWORD type
};