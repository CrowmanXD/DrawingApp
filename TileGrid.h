#pragma once

#include "Tile.h"
#include <vector>

class TileGrid {
public:
    TileGrid(sf::Vector2u canvasSize, unsigned tileSize);

    Tile& tileAt(sf::Vector2f position);
    std::vector<Tile*> tilesInBounds(sf::FloatRect bounds);

    void draw(sf::RenderTarget& target);

private:
    unsigned m_tileSize;
    unsigned m_columns;
    unsigned m_rows;

    std::vector<Tile> m_tiles;
};
