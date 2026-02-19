#include "Canvas.h"
#include "StrokeUndoCommand.h"

Canvas::Canvas(sf::Vector2u size) {
    m_texture.resize(size);
    m_texture.clear(sf::Color::White);
    m_texture.display();
}

void Canvas::beginStroke() {
    m_inStroke = true;
    // Capture the canvas state before drawing
    m_strokeBackup = m_texture.getTexture().copyToImage();
}

void Canvas::endStroke() {
    if (!m_inStroke)
        return;

    m_inStroke = false;

    // Capture the canvas state after drawing
    auto beforeImage = std::make_unique<sf::Image>(m_strokeBackup);
    auto afterImage = std::make_unique<sf::Image>(m_texture.getTexture().copyToImage());

    // Create and push undo command
    auto cmd = std::make_unique<StrokeUndoCommand>(
        std::move(beforeImage),
        std::move(afterImage)
    );
    m_undoStack.push(std::move(cmd));
}

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f position) {
    m_texture.draw(drawable);
    m_texture.display();
}

void Canvas::undo() {
    m_undoStack.undo(*this);
}

void Canvas::redo() {
    m_undoStack.redo(*this);
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
