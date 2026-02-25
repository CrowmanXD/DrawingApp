#pragma once

#include <SFML/System/Vector2.hpp>
#include <SFML/Graphics/Color.hpp>

// Single brush stamp (dab)
struct Dab {
    sf::Vector2f position;
    float size = 1.0f;         // brush size multiplier
    float opacity = 1.0f;      // 0.0 - 1.0
    float flow = 1.0f;         // 0.0 - 1.0 (paint amount)
    float rotation = 0.0f;     // future: textured brushes
    sf::Color color;
    
    Dab() : color(sf::Color::Black) {}
    Dab(sf::Vector2f pos, float sz = 1.0f, float op = 1.0f)
        : position(pos), size(sz), opacity(op), color(sf::Color::Black) {}
};
