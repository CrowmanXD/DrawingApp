#pragma once

#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>

class Canvas;

class Tile {
public:
    Tile(unsigned tileSize, sf::Vector2u position);

    sf::RenderTexture& texture();

    void saveState();
    void restoreState();

    void apply(Canvas& canvas) const;

    void draw(sf::RenderTarget& target) const;
private:
    sf::RenderTexture m_current;
    sf::RenderTexture m_backup;

    sf::Vector2u m_position;
};
