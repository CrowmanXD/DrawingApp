#pragma once

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Window/Keyboard.hpp>
#include <SFML/System/Vector2.hpp>

class Canvas;

class Tool {
public:
    virtual ~Tool() = default;

    virtual void onMouseDown(Canvas& canvas, sf::Vector2f pos) {}
    virtual void onMouseMove(Canvas& canvas, sf::Vector2f pos) {}
    virtual void onMouseUp(Canvas& canvas, sf::Vector2f pos) {}
    virtual void onRightClick(Canvas& canvas, sf::Vector2f pos) {}

    // Allows tools to receive pressure data without breaking tool signatures
    virtual void setPenPressure(float pressure) {}

    // Allows the tool to draw UI handles over the screen without baking them into the canvas
    virtual void onDrawOverlay(Canvas& canvas, sf::RenderWindow& window, sf::Vector2f offset, float zoom, float workspaceRotation = 0.f) {}

    // Allows the tool to listen for the "Enter" or "Escape" key
    virtual void onKeyPress(Canvas& canvas, sf::Keyboard::Key key) {}

    virtual void onActivate(Canvas& canvas) {}
    virtual void onDeactivate(Canvas& canvas) {}
};
