#include "Canvas.h"
#include "StrokeUndoCommand.h"

Canvas::Canvas(sf::Vector2u size) {
    m_texture.resize(size);
    // Use transparent background to support alpha blending
    m_texture.clear(sf::Color(255, 255, 255, 0));
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

void Canvas::draw(const sf::Drawable& drawable, sf::Vector2f position, const sf::RenderStates& states) {
    // Only draw if a stroke is currently active
    if (!m_inStroke)
        return;

    // Use provided blend states
    m_texture.draw(drawable, states);
    m_texture.display();
}

void Canvas::undo() {
    // Cancel current stroke if one is in progress
    if (m_inStroke) {
        m_inStroke = false;
        // Restore the canvas to the state before the current stroke
        sf::Texture texture;
        if (texture.loadFromImage(m_strokeBackup)) {
            m_texture.clear(sf::Color::White);
            m_texture.draw(sf::Sprite(texture));
            m_texture.display();
        }
    }
    m_undoStack.undo(*this);
}

void Canvas::redo() {
    // Cancel current stroke if one is in progress
    if (m_inStroke) {
        m_inStroke = false;
        // Restore the canvas to the state before the current stroke
        sf::Texture texture;
        if (texture.loadFromImage(m_strokeBackup)) {
            m_texture.clear(sf::Color::White);
            m_texture.draw(sf::Sprite(texture));
            m_texture.display();
        }
    }
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
