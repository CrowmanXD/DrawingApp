#include "BrushTool.h"
#include "Canvas.h"

#include <SFML/Graphics/CircleShape.hpp>
#include <cmath>

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

    // Catmull-Rom interpolation for smoother curves
    sf::Vector2f catmullRom(
        const sf::Vector2f& p0,
        const sf::Vector2f& p1,
        const sf::Vector2f& p2,
        const sf::Vector2f& p3,
        float t
    ) {
        float t2 = t * t;
        float t3 = t2 * t;

        float v0 = (p2.x - p0.x) * 0.5f;
        float v1 = (p3.x - p1.x) * 0.5f;
        float x = p1.x + v0 * t + (3.f * (p2.x - p1.x) - 2.f * v0 - v1) * t2 + (2.f * (p1.x - p2.x) + v0 + v1) * t3;

        v0 = (p2.y - p0.y) * 0.5f;
        v1 = (p3.y - p1.y) * 0.5f;
        float y = p1.y + v0 * t + (3.f * (p2.y - p1.y) - 2.f * v0 - v1) * t2 + (2.f * (p1.y - p2.y) + v0 + v1) * t3;

        return { x, y };
    }

    float distance(const sf::Vector2f& a, const sf::Vector2f& b) {
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
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

    sf::Vector2f lastPoint = m_points.back();
    float dist = distance(lastPoint, position);

    // Professional apps: interpolate intermediate points based on distance
    // This ensures no gaps regardless of cursor speed
    const float MAX_SEGMENT_DISTANCE = m_size * 1.5f; // Adjust based on brush size

    if (dist > MAX_SEGMENT_DISTANCE) {
        // Insert intermediate points
        int numInterpolated = static_cast<int>(dist / MAX_SEGMENT_DISTANCE);
        for (int i = 1; i <= numInterpolated; ++i) {
            float t = static_cast<float>(i) / (numInterpolated + 1);
            sf::Vector2f interpolatedPos = lastPoint * (1.f - t) + position * t;
            m_points.push_back(interpolatedPos);

            drawStrokeSegment(canvas);
        }
    }

    m_points.push_back(position);
    drawStrokeSegment(canvas);
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

void BrushTool::drawStrokeSegment(Canvas& canvas) {
    size_t n = m_points.size();

    if (n < 2) return;

    if (n == 2) {
        // Just two points: linear interpolation
        drawLinearSegment(canvas, m_points[0], m_points[1]);
    }
    else if (n == 3) {
        // Three points: simple smooth curve
        drawSmoothSegment(canvas);
    }
    else {
        // Four or more points: use Catmull-Rom for smoother results
        const int segments = 20;

        sf::CircleShape brush(m_size);
        brush.setFillColor(m_color);
        brush.setOrigin({ m_size, m_size });

        const auto& p0 = m_points[n - 4];
        const auto& p1 = m_points[n - 3];
        const auto& p2 = m_points[n - 2];
        const auto& p3 = m_points[n - 1];

        for (int i = 0; i <= segments; ++i) {
            float t = static_cast<float>(i) / segments;
            sf::Vector2f pos = catmullRom(p0, p1, p2, p3, t);

            brush.setPosition(pos);
            canvas.draw(brush, pos);
        }
    }
}
