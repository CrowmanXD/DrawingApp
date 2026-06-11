#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>
#include <vector>

class ShapeTool : public Tool {
public:
    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;

    void setColor(sf::Color color) { m_color = color; }
    void setShapeType(int type) { m_shapeType = type; }
    int getShapeType() const { return m_shapeType; }

    void setFilled(bool filled) { m_isFilled = filled; }
    bool isFilled() const { return m_isFilled; }

    void setThickness(float thickness) { m_thickness = thickness; }

private:
    sf::Vector2f m_startPos;
    bool m_isDrawing = false;
    sf::Color m_color = sf::Color::Black;

    // 0=Square, 1=Circle, 2=Diamond, 3=Triangle, 4=Star, 5=Heart
    int m_shapeType = 0;
    bool m_isFilled = true;      
    float m_thickness = 5.0f;

    void renderShape(Canvas& canvas, sf::Vector2f endPos);

    // Core math algorithms to map and fill complex shapes
    void drawMappedPolygon(Canvas& canvas, const sf::FloatRect& bounds, const std::vector<sf::Vector2f>& normalizedPoints);
    std::vector<sf::Vector2f> normalizePoints(std::vector<sf::Vector2f> pts);
};