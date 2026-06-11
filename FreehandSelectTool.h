#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>
#include <vector>

class FreehandSelectTool : public Tool {
public:
    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;

private:
    std::vector<sf::Vector2f> m_points;
    bool m_isSelecting = false;

    // Custom algorithm to fill complex, concave polygons
    void fillPolygon(Canvas& canvas);
};