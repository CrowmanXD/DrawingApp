#include "TileGrid.h"

TileGrid::TileGrid(sf::Vector2u canvasSize, unsigned tileSize)
    : m_tileSize(tileSize) {

    m_columns = (canvasSize.x + tileSize - 1) / tileSize;
    m_rows = (canvasSize.y + tileSize - 1) / tileSize;

    m_tiles.reserve(m_columns * m_rows);

    for (unsigned y = 0; y < m_rows; ++y) {
        for (unsigned x = 0; x < m_columns; ++x) {
            m_tiles.emplace_back(
                tileSize,
                sf::Vector2u(x * tileSize, y * tileSize)
            );
        }
    }
}

Tile& TileGrid::tileAt(sf::Vector2f position) {
    unsigned x = static_cast<unsigned>(position.x) / m_tileSize;
    unsigned y = static_cast<unsigned>(position.y) / m_tileSize;

    // Safety check to prevent crash if mouse goes outside window
    if (x >= m_columns) x = m_columns - 1;
    if (y >= m_rows) y = m_rows - 1;

    return m_tiles[y * m_columns + x];
}

void TileGrid::draw(sf::RenderTarget& target) {
    for (auto& tile : m_tiles)
        tile.draw(target);
}
