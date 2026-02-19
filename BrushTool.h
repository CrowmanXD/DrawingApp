#pragma once

#include "Tool.h"
#include <SFML/Graphics/Color.hpp>
#include <vector>

class BrushTool : public Tool {
public:
    BrushTool();

    void setColor(const sf::Color& color);
    void setSize(float size);

    void onMouseDown(Canvas& canvas, sf::Vector2f position) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f position) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f position) override;

private:
    // Drawing helpers (isolated for Undo / Replay / AI)
    void drawDot(Canvas& canvas, const sf::Vector2f& position);
    void drawLinearSegment(
        Canvas& canvas,
        const sf::Vector2f& from,
        const sf::Vector2f& to
    );
    void drawSmoothSegment(Canvas& canvas);
    void drawStrokeSegment(Canvas& canvas);

private:
    // Stroke data (important for future Undo / AI)
    std::vector<sf::Vector2f> m_points;

    sf::Color m_color;
    float m_size;
};