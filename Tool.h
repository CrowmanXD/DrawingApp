#pragma once

#include <SFML/System/Vector2.hpp>

class Canvas;

class Tool {
public:
    virtual ~Tool() = default;

    virtual void onMouseDown(Canvas& canvas, sf::Vector2f position) = 0;
    virtual void onMouseMove(Canvas& canvas, sf::Vector2f position) = 0;
    virtual void onMouseUp(Canvas& canvas, sf::Vector2f position) = 0;
};
