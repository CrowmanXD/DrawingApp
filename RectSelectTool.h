#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>

class RectSelectTool : public Tool {
public:
    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;

private:
    sf::Vector2f m_startPos;
    bool m_isSelecting = false;
};
