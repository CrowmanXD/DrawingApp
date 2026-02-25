#include "StrokeUndoCommand.h"
#include "Canvas.h"

StrokeUndoCommand::StrokeUndoCommand(
    std::unique_ptr<sf::Image> before,
    std::unique_ptr<sf::Image> after)
    : m_before(std::move(before)),
    m_after(std::move(after)) {
}

void StrokeUndoCommand::undo(Canvas& canvas) {
    if (m_before) {
        sf::Texture texture;
        if (texture.loadFromImage(*m_before)) {
            sf::Sprite sprite(texture);
            canvas.getTexture().clear(sf::Color::White);
            canvas.getTexture().draw(sprite);
            canvas.getTexture().display();
        }
    }
}

void StrokeUndoCommand::redo(Canvas& canvas) {
    if (m_after) {
        sf::Texture texture;
        if (texture.loadFromImage(*m_after)) {
            sf::Sprite sprite(texture);
            canvas.getTexture().clear(sf::Color::White);
            canvas.getTexture().draw(sprite);
            canvas.getTexture().display();
        }
    }
}
