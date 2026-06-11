#include "SelectionBrushTool.h"
#include "SelectionUndoCommand.h"
#include "Canvas.h"
#include <cmath>

void SelectionBrushTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    m_lastPos = pos;
    m_isDrawing = true;

    m_beforeImage = std::make_unique<sf::Image>(canvas.getSelectionTextureConst().copyToImage());
    m_beforeHasSelection = canvas.hasSelection();

    // Notice we do NOT clear the texture here
    // This allows you to click multiple times to paint a massive, complex selection
    canvas.setSelectionActive(true);
    canvas.setSelectionLive(true);

    drawLine(canvas, pos, pos);
}

void SelectionBrushTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isDrawing) return;
    drawLine(canvas, m_lastPos, pos);
    m_lastPos = pos;
}

void SelectionBrushTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isDrawing) return;
    m_isDrawing = false;
    canvas.setSelectionLive(false);

    auto afterImage = std::make_unique<sf::Image>(canvas.getSelectionTextureConst().copyToImage());
    canvas.pushUndoCommand(std::make_unique<SelectionUndoCommand>(
        std::move(m_beforeImage), std::move(afterImage), m_beforeHasSelection, canvas.hasSelection()
    ));
}

void SelectionBrushTool::drawLine(Canvas& canvas, sf::Vector2f start, sf::Vector2f end) {
    sf::Vector2f diff = end - start;
    float length = std::sqrt(diff.x * diff.x + diff.y * diff.y);

    // Normal mode paints solid white to expand the mask. Eraser paints transparent black to carve holes.
    sf::Color paintColor = m_eraser ? sf::Color(0, 0, 0, 0) : sf::Color::White;

    // Erase mode requires BlendNone to forcefully overwrite pixels to transparent
    sf::BlendMode blend = m_eraser ? sf::BlendNone : sf::BlendAlpha;

    if (length == 0.f) {
        sf::CircleShape circle(m_size / 2.f);
        circle.setOrigin(sf::Vector2f(m_size / 2.f, m_size / 2.f));
        circle.setPosition(start);
        circle.setFillColor(paintColor);
        canvas.getSelectionTexture().draw(circle, sf::RenderStates(blend));
    }
    else {
        float spacing = 1.0f;
        int steps = static_cast<int>(length / spacing);
        if (steps == 0) steps = 1;

        sf::CircleShape circle(m_size / 2.f);
        circle.setOrigin(sf::Vector2f(m_size / 2.f, m_size / 2.f));
        circle.setFillColor(paintColor);

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / steps;
            sf::Vector2f pos = start + diff * t;
            circle.setPosition(pos);
            canvas.getSelectionTexture().draw(circle, sf::RenderStates(blend));
        }
    }
    canvas.getSelectionTexture().display();
}