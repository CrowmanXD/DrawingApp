#pragma once
#include "Tool.h"
#include <SFML/Graphics.hpp>

class TransformTool : public Tool {
public:
    void onActivate(Canvas& canvas) override;
    void onDeactivate(Canvas& canvas) override;
    bool isActive() const { return m_isActive; }

    void onMouseDown(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseMove(Canvas& canvas, sf::Vector2f pos) override;
    void onMouseUp(Canvas& canvas, sf::Vector2f pos) override;

    void onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation = 0.f) override;
    void onKeyPress(Canvas& canvas, sf::Keyboard::Key key) override;
    void onRightClick(Canvas& canvas, sf::Vector2f pos) override;

private:
    bool m_isActive = false;
    int m_activeHandle = -1;
    bool m_showContextMenu = false;

    float m_currentRotation = 0.f;
    float m_dragStartRotation = 0.f;
    float m_dragStartAngle = 0.f;
    bool m_isHoveringRotation = false; // Used to trigger the mouse cursor change

    sf::FloatRect m_currentBounds;
    sf::Vector2f m_dragStartPos;
    sf::FloatRect m_dragStartBounds;
    float m_currentZoom = 1.0f;

    void rotate90(bool clockwise);
    void flip(bool horizontal);

    sf::Image m_backupImage;           // Saves the original layer for Undo
    sf::Texture m_backgroundTexture;   // The layer WITH the pixels erased
    sf::Texture m_floatingTexture;     // ONLY the extracted pixels being moved

    int getHoveredHandle(sf::Vector2f pos, float zoom);
    void renderLivePreview(Canvas& canvas);
    void applyTransform(Canvas& canvas);
};