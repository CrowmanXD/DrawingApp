#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>

class CropTool : public Tool {
public:
    void onActivate(Canvas& canvas) override;
    void onDeactivate(Canvas& canvas) override;

    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;
    void onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation = 0.f) override;
    void onKeyPress(Canvas& canvas, sf::Keyboard::Key key) override;

private:
    sf::FloatRect m_cropRect;
    bool m_isActive = false;

    // -1: None | 0-7: The 8 border handles | 8: Inside the box (move whole box)
    int m_activeHandle = -1;
    sf::Vector2f m_dragStartPos;
    sf::FloatRect m_dragStartRect;
    float m_currentZoom = 1.0f;

    int getHoveredHandle(sf::Vector2f pos, float zoom);
};
