#include "Canvas.h"

Canvas::Canvas(sf::Vector2u size) {
    m_texture.resize(size);
    m_texture.clear(sf::Color::White);
    m_texture.display();
}

void Canvas::beginStroke() {
    m_tilesBefore.clear();
    // mai târziu: capture tile-uri
}

void Canvas::endStroke() {
    m_tilesAfter.clear();
    // mai târziu: push UndoCommand
}

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f) {
    m_texture.draw(drawable);
    m_texture.display();
}

sf::RenderTexture& Canvas::getTexture() {
    return m_texture;
}

const sf::Texture& Canvas::getFinalTexture() const {
    return m_texture.getTexture();
}

void Canvas::clear(const sf::Color& color) {
    m_texture.clear(color);
    m_texture.display();
}
