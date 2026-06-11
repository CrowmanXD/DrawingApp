#include "SelectionUndoCommand.h"
#include "Canvas.h"

SelectionUndoCommand::SelectionUndoCommand(
    std::unique_ptr<sf::Image> before,
    std::unique_ptr<sf::Image> after,
    bool beforeHasSelection,
    bool afterHasSelection)
    : m_before(std::move(before)),
    m_after(std::move(after)),
    m_beforeHasSelection(beforeHasSelection),
    m_afterHasSelection(afterHasSelection) {
}

void SelectionUndoCommand::undo(Canvas& canvas) {
    if (m_before) {
        sf::Texture texture;
        if (texture.loadFromImage(*m_before)) {
            canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
            canvas.getSelectionTexture().draw(sf::Sprite(texture), sf::RenderStates(sf::BlendNone));
            canvas.getSelectionTexture().display();
        }
    }
    canvas.setSelectionActive(m_beforeHasSelection);
}

void SelectionUndoCommand::redo(Canvas& canvas) {
    if (m_after) {
        sf::Texture texture;
        if (texture.loadFromImage(*m_after)) {
            canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
            canvas.getSelectionTexture().draw(sf::Sprite(texture), sf::RenderStates(sf::BlendNone));
            canvas.getSelectionTexture().display();
        }
    }
    canvas.setSelectionActive(m_afterHasSelection);
}