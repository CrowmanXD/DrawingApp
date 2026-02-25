#pragma once

#include <SFML/System/Vector2.hpp>

// Raw input point with all metadata
struct StrokePoint {
    sf::Vector2f position;
    float pressure = 1.0f;    // 0.0 - 1.0
    float speed = 0.0f;       // pixels per frame
    float tilt = 0.0f;        // 0.0 - 1.0 (future: angle)
    float time = 0.0f;        // timestamp
    
    StrokePoint() = default;
    StrokePoint(sf::Vector2f pos, float press = 1.0f)
        : position(pos), pressure(press) {}
};
