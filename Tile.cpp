#include "Tile.h"
#include "Canvas.h"
#include <SFML/Graphics/Color.hpp>

Tile::Tile(unsigned tileSize, sf::Vector2u position)
    : m_position(position)
{
    // Create the textures manually
    m_current.resize({ tileSize, tileSize });
    m_backup.resize({ tileSize, tileSize });

    m_current.clear(sf::Color::Transparent);
    m_backup.clear(sf::Color::Transparent);

    m_current.display();
    m_backup.display();
}

sf::RenderTexture& Tile::texture() {
    return m_current;
}

void Tile::saveState() {
    m_backup.clear(sf::Color::Transparent);
    m_backup.draw(sf::Sprite(m_current.getTexture()));
    m_backup.display();
}

void Tile::restoreState() {
    m_current.clear(sf::Color::Transparent);
    m_current.draw(sf::Sprite(m_backup.getTexture()));
    m_current.display();
}

void Tile::apply(Canvas& canvas) const {
    // Create the sprite on-the-fly.
    // This ensures it always points to the valid texture at the current memory address.
    sf::Sprite s(m_current.getTexture());
    s.setPosition(sf::Vector2f(m_position));

    // Draw directly to canvas (ignoring the position arg in Canvas::draw for now)
    canvas.draw(s, sf::Vector2f(m_position));
}

void Tile::draw(sf::RenderTarget& target) const
{
    // Create a sprite from the current texture
    sf::Sprite sprite(m_current.getTexture());

    // Set its position
    sprite.setPosition({ static_cast<float>(m_position.x), static_cast<float>(m_position.y) });

    // Draw it to the target (the window or canvas)
    target.draw(sprite);
}
