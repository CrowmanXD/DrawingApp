#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>

class SelectionBrushTool : public Tool {
public:
    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;

    void setSize(float size) { m_size = size; }
    float getSize() const { return m_size; }

    void setEraser(bool eraser) { m_eraser = eraser; }
    bool isEraser() const { return m_eraser; }

private:
    sf::Vector2f m_lastPos;
    bool m_isDrawing = false;
    float m_size = 20.f;
    bool m_eraser = false;

    void drawLine(Canvas& canvas, sf::Vector2f start, sf::Vector2f end);
};