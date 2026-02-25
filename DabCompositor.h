#pragma once

#include "Dab.h"
#include <SFML/Graphics/Texture.hpp>

class Canvas;

// Renders dabs to canvas with blending
class DabCompositor {
public:
    DabCompositor();
    
    // Paint a single dab onto the canvas
    void paintDab(Canvas& canvas, const Dab& dab, const sf::Texture& brushTexture, float baseSize);
    
    // Paint multiple dabs
    void paintDabs(Canvas& canvas, const std::vector<Dab>& dabs, const sf::Texture& brushTexture, float baseSize);

private:
    // (Future: blend modes, texture masks, etc.)
};
