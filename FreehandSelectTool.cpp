#include "FreehandSelectTool.h"
#include "Canvas.h"
#include "SelectionUndoCommand.h"
#include <algorithm>

void FreehandSelectTool::onMouseDown(Canvas& canvas, sf::Vector2f pos) {
    m_points.clear();
    m_points.push_back(pos);
    m_isSelecting = true;

    // Clear previous selection and start live preview
    canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
    canvas.setSelectionActive(false);
    canvas.setSelectionLive(true);
}

void FreehandSelectTool::onMouseMove(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isSelecting) return;

    // Optimization: Only record a point if the mouse moved at least 2 pixels. 
    // This keeps the algorithm fast without losing visual smoothness
    if (m_points.empty() || std::abs(m_points.back().x - pos.x) > 2.0f || std::abs(m_points.back().y - pos.y) > 2.0f) {
        m_points.push_back(pos);
    }

    // Draw the live preview outline of the lasso
    canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));

    if (m_points.size() > 1) {
        sf::VertexArray lines(sf::PrimitiveType::LineStrip);
        for (const auto& p : m_points) {
            lines.append(sf::Vertex(p, sf::Color::White));
        }

        canvas.getSelectionTexture().draw(lines, sf::RenderStates(sf::BlendNone));
    }
    canvas.getSelectionTexture().display();
}

void FreehandSelectTool::onMouseUp(Canvas& canvas, sf::Vector2f pos) {
    if (!m_isSelecting) return;
    m_isSelecting = false;
    canvas.setSelectionLive(false);

    // 1. Capture the exact state BEFORE filling
    auto beforeImage = std::make_unique<sf::Image>(canvas.getSelectionTextureConst().copyToImage());
    bool beforeHasSelection = canvas.hasSelection();

    // 2. Execute the mathematical fill onto the selection texture
    canvas.getSelectionTexture().clear(sf::Color(0, 0, 0, 0));
    fillPolygon(canvas);
    canvas.getSelectionTexture().display();

    canvas.setSelectionActive(m_points.size() > 2);

    // 3. Capture the state AFTER filling and push to UndoStack
    auto afterImage = std::make_unique<sf::Image>(canvas.getSelectionTextureConst().copyToImage());
    canvas.pushUndoCommand(std::make_unique<SelectionUndoCommand>(
        std::move(beforeImage), std::move(afterImage), beforeHasSelection, canvas.hasSelection()
    ));

    m_points.clear();
}

void FreehandSelectTool::fillPolygon(Canvas& canvas) {
    if (m_points.size() < 3) return;

    // 1. Find the Bounding Box of the drawn lasso
    float minY = m_points[0].y, maxY = m_points[0].y;
    float minX = m_points[0].x, maxX = m_points[0].x;

    for (const auto& p : m_points) {
        if (p.y < minY) minY = p.y;
        if (p.y > maxY) maxY = p.y;
        if (p.x < minX) minX = p.x;
        if (p.x > maxX) maxX = p.x;
    }

    // Update the Canvas's internal bounding box variable
    canvas.setSelectionBounds(sf::FloatRect({ minX, minY }, { maxX - minX, maxY - minY }));

    // 2. Scanline Polygon Fill Algorithm
    sf::VertexArray fillLines(sf::PrimitiveType::Lines);
    int startY = std::max(0, static_cast<int>(minY));
    int endY = std::min(static_cast<int>(canvas.getSize().y), static_cast<int>(maxY));

    for (int y = startY; y <= endY; ++y) {
        std::vector<float> intersections;

        // Raycast horizontally across the polygon
        for (size_t i = 0; i < m_points.size(); ++i) {
            size_t j = (i + 1) % m_points.size();
            sf::Vector2f p1 = m_points[i];
            sf::Vector2f p2 = m_points[j];

            if (p1.y > p2.y) std::swap(p1, p2);

            // If the ray intersects this edge
            if (y > p1.y && y <= p2.y) {
                float x = p1.x + (y - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);
                intersections.push_back(x);
            }
        }

        std::sort(intersections.begin(), intersections.end());

        // Draw solid horizontal lines between intersection pairs
        for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
            fillLines.append(sf::Vertex(sf::Vector2f(intersections[i], static_cast<float>(y)), sf::Color::White));
            fillLines.append(sf::Vertex(sf::Vector2f(intersections[i + 1], static_cast<float>(y)), sf::Color::White));
        }
    }

    // Blast all the generated lines onto the texture in a single batch call!
    canvas.getSelectionTexture().draw(fillLines, sf::RenderStates(sf::BlendNone));
}