#pragma once
#include <SFML/Graphics/Image.hpp>

class ClipboardHelper {
public:
    // Reads the OS clipboard and returns an image if one exists
    static sf::Image getImage();
};