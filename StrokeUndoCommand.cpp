#include "StrokeUndoCommand.h"
#include "Canvas.h"

StrokeUndoCommand::StrokeUndoCommand(
    std::unique_ptr<sf::Image> before,
    std::unique_ptr<sf::Image> after,
    int layerIndex)
    : m_before(std::move(before)),
    m_after(std::move(after)),
    m_layerIndex(layerIndex) {
}

void StrokeUndoCommand::undo(Canvas& canvas) {
    if (m_before && m_layerIndex < canvas.getLayers().size()) {
        sf::Texture texture;
        if (texture.loadFromImage(*m_before)) {
            sf::Sprite sprite(texture);
            auto& layerTex = canvas.getLayers()[m_layerIndex]->texture;
            layerTex->clear(sf::Color(255, 255, 255, 0));
            layerTex->draw(sprite);
            layerTex->display();
        }
    }
}

void StrokeUndoCommand::redo(Canvas& canvas) {
    if (m_after && m_layerIndex < canvas.getLayers().size()) {
        sf::Texture texture;
        if (texture.loadFromImage(*m_after)) {
            sf::Sprite sprite(texture);
            auto& layerTex = canvas.getLayers()[m_layerIndex]->texture;
            layerTex->clear(sf::Color(255, 255, 255, 0));
            layerTex->draw(sprite);
            layerTex->display();
        }
    }
}
