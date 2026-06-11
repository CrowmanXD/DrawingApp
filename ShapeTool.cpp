#include "ShapeTool.h"
#include "Canvas.h"
#include <cmath>
#include <algorithm>

void ShapeTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    m_startPos = pos;
    m_isDrawing = true;
    canvas.beginStroke(); // Creates Undo backup
}

void ShapeTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isDrawing) return;
    canvas.restoreStrokeBackup(); // Clear the last frame
    renderShape(canvas, pos);     // Draw the new preview
}

void ShapeTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isDrawing) return;
    m_isDrawing = false;
    canvas.restoreStrokeBackup();
    renderShape(canvas, pos);
    canvas.endStroke(); // Push to the Undo Stack
}

// Automatically scales any coordinate array to perfectly fit inside a [-1, 1] box
std::vector<sf::Vector2f> ShapeTool::normalizePoints(std::vector<sf::Vector2f> pts) {
    float minX = 99999.f, maxX = -99999.f, minY = 99999.f, maxY = -99999.f;
    for (const auto& p : pts) {
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
    }
    float w = maxX - minX;
    float h = maxY - minY;
    if (w == 0) w = 1.0f;
    if (h == 0) h = 1.0f;

    for (auto& p : pts) {
        p.x = ((p.x - minX) / w) * 2.0f - 1.0f;
        p.y = ((p.y - minY) / h) * 2.0f - 1.0f;
    }
    return pts;
}

// Maps the normalized array to the actual mouse bounding box on the canvas
void ShapeTool::drawMappedPolygon(Canvas& canvas, const sf::FloatRect& bounds, const std::vector<sf::Vector2f>& normalizedPoints) {
    // Helper to scale math points onto the canvas
    auto mapToScreen = [&](sf::Vector2f p) {
        return sf::Vector2f(
            bounds.position.x + (p.x + 1.0f) * 0.5f * bounds.size.x,
            bounds.position.y + (p.y + 1.0f) * 0.5f * bounds.size.y
        );
        };

    if (m_isFilled) {
        // --- DRAW SOLID SHAPE ---
        sf::VertexArray arr(sf::PrimitiveType::TriangleFan);

        sf::Vector2f center(0.f, 0.f);
        for (const auto& p : normalizedPoints) { center += p; }
        center.x /= normalizedPoints.size();
        center.y /= normalizedPoints.size();

        arr.append(sf::Vertex(mapToScreen(center), m_color));
        for (const auto& p : normalizedPoints) {
            arr.append(sf::Vertex(mapToScreen(p), m_color));
        }
        arr.append(sf::Vertex(mapToScreen(normalizedPoints.front()), m_color));

        canvas.draw(arr, { 0, 0 });
    }
    else {
        // --- DRAW THICK OUTLINE (Using Pen Tool Logic) ---
        std::vector<sf::Vector2f> screenPts;
        for (const auto& p : normalizedPoints) {
            screenPts.push_back(mapToScreen(p));
        }

        for (size_t i = 0; i < screenPts.size(); ++i) {
            sf::Vector2f start = screenPts[i];
            sf::Vector2f end = screenPts[(i + 1) % screenPts.size()]; // Loop back to start

            sf::Vector2f diff = end - start;
            float length = std::sqrt(diff.x * diff.x + diff.y * diff.y);
            if (length == 0.0f) continue;

            // Draw the straight edge
            sf::RectangleShape line(sf::Vector2f(length, m_thickness));
            line.setOrigin(sf::Vector2f(0.0f, m_thickness / 2.0f));
            line.setPosition(start);
            line.setFillColor(m_color);
            line.setRotation(sf::radians(std::atan2(diff.y, diff.x))); // SFML 3 Syntax
            canvas.draw(line, start);

            // Draw round joints to prevent sharp corners from looking cracked
            sf::CircleShape joint(m_thickness / 2.0f);
            joint.setOrigin(sf::Vector2f(m_thickness / 2.0f, m_thickness / 2.0f));
            joint.setPosition(end);
            joint.setFillColor(m_color);
            canvas.draw(joint, end);
        }
    }
}

void ShapeTool::renderShape(Canvas& canvas, sf::Vector2f endPos) {
    sf::FloatRect bounds;
    bounds.position.x = std::min(m_startPos.x, endPos.x);
    bounds.position.y = std::min(m_startPos.y, endPos.y);
    bounds.size.x = std::abs(endPos.x - m_startPos.x);
    bounds.size.y = std::abs(endPos.y - m_startPos.y);

    if (bounds.size.x == 0 || bounds.size.y == 0) return;

    std::vector<sf::Vector2f> pts;

    if (m_shapeType == 0) { // Square
        pts = { {-1, -1}, {1, -1}, {1, 1}, {-1, 1} };
    }
    else if (m_shapeType == 1) { // Circle / Ellipse
        for (int i = 0; i < 60; ++i) {
            float angle = i * 2.0f * 3.14159265f / 60.0f;
            pts.push_back({ std::cos(angle), std::sin(angle) });
        }
    }
    else if (m_shapeType == 2) { // Diamond
        pts = { {0, -1}, {1, 0}, {0, 1}, {-1, 0} };
    }
    else if (m_shapeType == 3) { // Triangle
        pts = { {0, -1}, {1, 1}, {-1, 1} };
    }
    else if (m_shapeType == 4) { // Star
        for (int i = 0; i < 10; ++i) {
            float angle = i * 3.14159265f / 5.0f - 3.14159265f / 2.0f;
            float r = (i % 2 == 0) ? 1.0f : 0.4f;
            pts.push_back({ r * std::cos(angle), r * std::sin(angle) });
        }
        pts = normalizePoints(pts);
    }
    else if (m_shapeType == 5) { // Heart
        for (int i = 0; i < 50; ++i) {
            float t = i * 2.0f * 3.14159265f / 50.0f;
            float x = 16.0f * std::pow(std::sin(t), 3);
            float y = -(13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t));
            pts.push_back({ x, y });
        }
        pts = normalizePoints(pts);
    }

    drawMappedPolygon(canvas, bounds, pts);
}