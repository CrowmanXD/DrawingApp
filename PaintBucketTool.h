#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>
#include <queue>
#include <vector>

class PaintBucketTool : public Tool {
public:
    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;

    void setColor(sf::Color color) { m_color = color; }

    // A tolerance of 0 means exact color match. 15-30 is great for smooth edges.
    void setTolerance(int tolerance) { m_tolerance = tolerance; }
    int getTolerance() const { return m_tolerance; }

private:
    sf::Color m_color = sf::Color::Black;
    int m_tolerance = 30;

    bool colorsMatch(const sf::Color& c1, const sf::Color& c2) const;
};