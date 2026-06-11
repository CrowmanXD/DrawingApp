#include "RectSelectTool.h"
#include "SelectionUndoCommand.h"
#include "Canvas.h"
#include <cmath>
#include <algorithm>

void RectSelectTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    m_startPos = pos;
    m_isSelecting = true;

    m_beforeImage = std::make_unique<sf::Image>(canvas.getSelectionTextureConst().copyToImage());
    m_beforeHasSelection = canvas.hasSelection();

    canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
    canvas.setSelectionActive(false);

    // Start the live preview
    canvas.setSelectionLive(true);
    canvas.setSelectionBounds(sf::FloatRect({ pos.x, pos.y }, { 0.f, 0.f }));
}

void RectSelectTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isSelecting) return;

    // Update the mathematical bounds
    sf::FloatRect rect;
    rect.position.x = std::min(m_startPos.x, pos.x);
    rect.position.y = std::min(m_startPos.y, pos.y);
    rect.size.x = std::abs(pos.x - m_startPos.x);
    rect.size.y = std::abs(pos.y - m_startPos.y);
    canvas.setSelectionBounds(rect);

    canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));

    sf::RectangleShape shape;
    shape.setPosition(rect.position);
    shape.setSize(rect.size);
    shape.setFillColor(sf::Color::White);

    canvas.getSelectionTexture().draw(shape, sf::RenderStates(sf::BlendNone));
    canvas.getSelectionTexture().display();
}

void RectSelectTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isSelecting) return;
    m_isSelecting = false;
    canvas.setSelectionLive(false);

    sf::FloatRect rect;
    rect.position.x = std::min(m_startPos.x, pos.x);
    rect.position.y = std::min(m_startPos.y, pos.y);
    rect.size.x = std::abs(pos.x - m_startPos.x);
    rect.size.y = std::abs(pos.y - m_startPos.y);
    canvas.setSelectionBounds(rect);

    canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));

    sf::RectangleShape shape;
    shape.setPosition(rect.position);
    shape.setSize(rect.size);
    shape.setFillColor(sf::Color::White);

    canvas.getSelectionTexture().draw(shape, sf::RenderStates(sf::BlendNone));
    canvas.getSelectionTexture().display();

    canvas.setSelectionActive(rect.size.x > 1.f && rect.size.y > 1.f);
    auto afterImage = std::make_unique<sf::Image>(canvas.getSelectionTextureConst().copyToImage());
    canvas.pushUndoCommand(std::make_unique<SelectionUndoCommand>(
        std::move(m_beforeImage), std::move(afterImage), m_beforeHasSelection, canvas.hasSelection()
    ));
}