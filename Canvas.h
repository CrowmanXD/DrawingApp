#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/System/Vector2.hpp>
#include <unordered_set>
#include <vector>

#include "Tile.h"
#include "UndoStack.h"
#include "TileGrid.h"

class Canvas {
public:
    explicit Canvas(sf::Vector2u size);

    void draw(const sf::Drawable& drawable, sf::Vector2f position);

    void beginStroke();
    void endStroke();

    sf::RenderTexture& getTexture();            // pentru desen
    const sf::Texture& getFinalTexture() const; // pentru afisare

    void clear(const sf::Color& color = sf::Color::White);

private:
    sf::RenderTexture m_texture;

    std::vector<Tile> m_tilesBefore;
    std::vector<Tile> m_tilesAfter;
};
