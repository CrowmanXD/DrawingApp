#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>
#include <vector>

struct PathNode {
    sf::Vector2f pos;
    sf::Vector2f handleIn;
    sf::Vector2f handleOut;
    bool isSmooth;
};

class VectorPenTool : public Tool {
public:
    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;
    void onRightClick(Canvas& canvas, sf::Vector2f pos) override;
    void onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation = 0.f) override;
    void onKeyPress(Canvas& canvas, sf::Keyboard::Key key) override;

	float getSize() const { return m_size; }
    void setSize(float size) { m_size = size; }
    void setColor(sf::Color color) { m_color = color; }

private:
    std::vector<PathNode> m_nodes;
    float m_size = 5.0f;
    sf::Color m_color = sf::Color::Black;

    int m_dragIndex = -1;
    int m_dragType = 0; // 0=None, 1=Node, 2=HandleIn, 3=HandleOut
    float m_hitRadius = 8.0f; // Click detection radius

    int m_selectedNode = -1;
    int m_contextNode = -1;
    bool m_isClosed = false;

    void bakePath(Canvas& canvas);
    void drawThickLineSegment(Canvas& canvas, sf::Vector2f start, sf::Vector2f end);
};