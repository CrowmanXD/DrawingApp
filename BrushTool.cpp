#include "BrushTool.h"
#include "Canvas.h"

#include <SFML/Graphics/CircleShape.hpp>

namespace {
    sf::Vector2f bezier(
        const sf::Vector2f& p0,
        const sf::Vector2f& p1,
        const sf::Vector2f& p2,
        float t
    ) {
        float u = 1.f - t;
        return (u * u) * p0 + (2.f * u * t) * p1 + (t * t) * p2;
    }
}

BrushTool::BrushTool()
    : m_color(sf::Color::Black),
    m_size(5.f) {
}

void BrushTool::setColor(const sf::Color& color) {
    m_color = color;
}

void BrushTool::setSize(float size) {
    m_size = size;
}

void BrushTool::onMouseDown(Canvas& canvas, sf::Vector2f position) {
    m_points.clear();
    m_points.push_back(position);

    drawDot(canvas, position);
}

void BrushTool::onMouseMove(Canvas& canvas, sf::Vector2f position) {
    if (m_points.empty())
        return;

    m_points.push_back(position);

    if (m_points.size() == 2) {
        drawLinearSegment(canvas, m_points[0], m_points[1]);
    }
    else if (m_points.size() >= 3) {
        drawSmoothSegment(canvas);
    }
}

void BrushTool::onMouseUp(Canvas& canvas, sf::Vector2f position) {
    m_points.clear();
}

// ======================
// Drawing helpers
// ======================

void BrushTool::drawDot(Canvas& canvas, const sf::Vector2f& position) {
    sf::CircleShape brush(m_size);
    brush.setFillColor(m_color);
    brush.setOrigin({ m_size, m_size });
    brush.setPosition(position);

    canvas.draw(brush, position);
}

void BrushTool::drawLinearSegment(
    Canvas& canvas,
    const sf::Vector2f& from,
    const sf::Vector2f& to
) {
    const int segments = 10;

    sf::CircleShape brush(m_size);
    brush.setFillColor(m_color);
    brush.setOrigin({ m_size, m_size });

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        sf::Vector2f pos = from * (1.f - t) + to * t;

        brush.setPosition(pos);
        canvas.draw(brush, pos);
    }
}

void BrushTool::drawSmoothSegment(Canvas& canvas) {
    const int segments = 20;

    sf::CircleShape brush(m_size);
    brush.setFillColor(m_color);
    brush.setOrigin({ m_size, m_size });

    size_t n = m_points.size();
    const auto& p0 = m_points[n - 3];
    const auto& p1 = m_points[n - 2];
    const auto& p2 = m_points[n - 1];

    sf::Vector2f mid1 = (p0 + p1) * 0.5f;
    sf::Vector2f mid2 = (p1 + p2) * 0.5f;

    for (int i = 0; i <= segments; ++i) {
        float t = static_cast<float>(i) / segments;
        sf::Vector2f pos = bezier(mid1, p1, mid2, t);

        brush.setPosition(pos);
        canvas.draw(brush, pos);
    }
}
